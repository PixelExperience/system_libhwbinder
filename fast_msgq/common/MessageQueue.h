/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HIDL_MQ_H
#define HIDL_MQ_H

#include <android-base/logging.h>
#include <cutils/ashmem.h>
#include <hidl/MQDescriptor.h>
#include <sys/mman.h>
#include <atomic>

namespace android {
namespace hardware {

template <typename T, MQFlavor flavor>
struct MessageQueue {
  /*
   * @param Desc MQDescriptor describing the FMQ.
   * @param resetPointers bool indicating whether the read/write pointers
   * should be reset or not.
   */
  MessageQueue(const MQDescriptor<flavor>& Desc, bool resetPointers = true);

  ~MessageQueue();

  /*
   * This constructor will use Ashmem shared memory to create an FMQ
   * that can contain a maximum of numElementsInQueue elements of type T.
   *
   * @param numElementsInQueue Capacity of the MessageQueue in terms of T.
   */
  MessageQueue(size_t numElementsInQueue);

  /*
   * @return Number of items of type T that can be written into the FMQ
   * without a read.
   */
  size_t availableToWrite() const;

  /*
   * @return Number of items of type T that are waiting to be read from the
   * FMQ.
   */
  size_t availableToRead() const;

  /*
   * Returns the size of type T.
   *
   * @param Size of T.
   */
  size_t getQuantumSize() const;

  /*
   * Returns the size of the FMQ in terms of the size of type T.
   *
   * @return Number of items of type T that will fit in the FMQ.
   */
  size_t getQuantumCount() const;

  /*
   * @return Whether the FMQ is configured correctly.
   */
  bool isValid() const;

  /*
   * @param data Pointer to the object of type T to be written into the FMQ.
   *
   * @return Whether the write was successful.
   */
  bool write(const T* data);

  /*
   * @param data Pointer to the memory where the object read from the FMQ is
   * copied to.
   *
   * @return Whether the read was successful.
   */
  bool read(T* data);

  /*
   * Write some data into the FMQ.
   *
   * @param data Pointer to the array of items of type T.
   * @param count Number of items in array.
   *
   * @return Whether the write was successful.
   */
  bool write(const T* data, size_t count);

  /*
   * Read some data from the FMQ.
   *
   * @param data Pointer to the array to which read data is to be written.
   * @param count Number of items to be read.
   *
   * @return Whether the read was successful.
   */
  bool read(T* data, size_t count);

  /*
   * Get a pointer to the MQDescriptor object that describes this FMQ.
   *
   * @return Pointer to the MQDescriptor associated with the FMQ.
   */
  const MQDescriptor<flavor>* getDesc() const { return mDesc.get(); }

 private:
  struct region {
    uint8_t* address;
    size_t length;
  };
  struct transaction {
    region first;
    region second;
  };

  size_t writeBytes(const uint8_t* data, size_t size);
  transaction beginWrite(size_t nBytesDesired) const;
  void commitWrite(size_t nBytesWritten);

  size_t readBytes(uint8_t* data, size_t size);
  transaction beginRead(size_t nBytesDesired) const;
  void commitRead(size_t nBytesRead);

  size_t availableToWriteBytes() const;
  size_t availableToReadBytes() const;

  MessageQueue(const MessageQueue& other) = delete;
  MessageQueue& operator=(const MessageQueue& other) = delete;
  MessageQueue();

  void* mapGrantorDescr(uint32_t grantor_idx);
  void unmapGrantorDescr(void* address, uint32_t grantor_idx);
  void initMemory(bool resetPointers);

  std::unique_ptr<MQDescriptor<flavor>> mDesc;
  uint8_t* mRing;
  /*
   * TODO(b/31550092): Change to 32 bit read and write pointer counters.
   */
  std::atomic<uint64_t>* mReadPtr;
  std::atomic<uint64_t>* mWritePtr;
};

template <typename T, MQFlavor flavor>
void MessageQueue<T, flavor>::initMemory(bool resetPointers) {
  /*
   * Verify that the the Descriptor contains the minimum number of grantors
   * the native_handle is valid and T matches quantum size.
   */
  if ((mDesc == nullptr) || !mDesc->isHandleValid() ||
      (mDesc->countGrantors() < MQDescriptor<flavor>::kMinGrantorCount) ||
      (mDesc->getQuantum() != sizeof(T))) {
    return;
  }

  if (flavor == kSynchronizedReadWrite) {
    mReadPtr =
        reinterpret_cast<std::atomic<uint64_t>*>
        (mapGrantorDescr(MQDescriptor<flavor>::READPTRPOS));
  } else {
    /*
     * The unsynchronized write flavor of the FMQ may have multiple readers
     * and each reader would have their own read pointer counter.
     */
    mReadPtr = new std::atomic<uint64_t>;
  }
  CHECK(mReadPtr != nullptr);

  mWritePtr =
      reinterpret_cast<std::atomic<uint64_t>*>
      (mapGrantorDescr(MQDescriptor<flavor>::WRITEPTRPOS));
  CHECK(mWritePtr != nullptr);

  if (resetPointers) {
    mReadPtr->store(0, std::memory_order_release);
    mWritePtr->store(0, std::memory_order_release);
  } else if (flavor != kSynchronizedReadWrite) {
    // Always reset the read pointer.
    mReadPtr->store(0, std::memory_order_release);
  }

  mRing = reinterpret_cast<uint8_t*>(mapGrantorDescr
                                     (MQDescriptor<flavor>::DATAPTRPOS));
  CHECK(mRing != nullptr);
}

template <typename T, MQFlavor flavor>
MessageQueue<T, flavor>::MessageQueue(
    const MQDescriptor<flavor>& Desc, bool resetPointers) {
  mDesc = std::unique_ptr<MQDescriptor<flavor>>(new MQDescriptor<flavor>(Desc));
  initMemory(resetPointers);
}

template <typename T, MQFlavor flavor>
MessageQueue<T, flavor>::MessageQueue(size_t numElementsInQueue) {
  size_t kQueueSizeBytes = numElementsInQueue * sizeof(T);
  /*
   * The FMQ needs to allocate memory for the ringbuffer as well as for the
   * read and write pointer counters. Also, Ashmem memory region size needs to
   * be specified in page-aligned bytes.
   */
  size_t kAshmemSizePageAligned =
      (kQueueSizeBytes + 2 * sizeof(android::hardware::RingBufferPosition) +
       PAGE_SIZE - 1) &
      ~(PAGE_SIZE - 1);

  /*
   * Create an ashmem region to map the memory for the ringbuffer,
   * read counter and write counter.
   */
  int ashmemFd = ashmem_create_region("MessageQueue", kAshmemSizePageAligned);
  ashmem_set_prot_region(ashmemFd, PROT_READ | PROT_WRITE);

  /*
   * The native handle will contain the fds to be mapped.
   */
  native_handle_t* mq_handle =
      native_handle_create(1 /* numFds */, 0 /* numInts */);
  if (mq_handle == nullptr) return;
  mq_handle->data[0] = ashmemFd;
  mDesc = std::unique_ptr<MQDescriptor<flavor>>(
      new MQDescriptor<flavor>(kQueueSizeBytes, mq_handle, sizeof(T)));
  initMemory(true);
}

template <typename T, MQFlavor flavor>
MessageQueue<T, flavor>::~MessageQueue() {
  if (flavor == kUnsynchronizedWrite) {
    delete mReadPtr;
  } else {
    unmapGrantorDescr(mReadPtr, MQDescriptor<flavor>::READPTRPOS);
  }
  if (mWritePtr) unmapGrantorDescr(mWritePtr,
                                   MQDescriptor<flavor>::WRITEPTRPOS);
  if (mRing) unmapGrantorDescr(mRing, MQDescriptor<flavor>::DATAPTRPOS);
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::write(const T* data) {
  return write(data, 1);
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::read(T* data) {
  return read(data, 1);
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::write(const T* data, size_t count) {
  /*
   * If read/write synchronization is not enabled, data in the queue
   * will be overwritten by a write operation when full.
   */
  if ((flavor == kSynchronizedReadWrite &&
       (availableToWriteBytes() < sizeof(T) * count)) ||
      (count > getQuantumCount()))
    return false;

  return (writeBytes(reinterpret_cast<const uint8_t*>(data),
                     sizeof(T) * count) == sizeof(T) * count);
}

template <typename T, MQFlavor flavor>
__attribute__((no_sanitize("integer")))
bool MessageQueue<T, flavor>::read(T* data, size_t count) {
  if (availableToReadBytes() < sizeof(T) * count) return false;
  /*
   * If it is detected that the data in the queue was overwritten
   * due to the reader process being too slow, the read pointer counter
   * is set to the same as the write pointer counter to indicate error
   * and the read returns false;
   */
  auto writePtr = mWritePtr->load(std::memory_order_relaxed);
  auto readPtr = mReadPtr->load(std::memory_order_relaxed);

  if (writePtr - readPtr > mDesc->getSize()) {
    mReadPtr->store(writePtr, std::memory_order_release);
    return false;
  }

  return readBytes(reinterpret_cast<uint8_t*>(data), sizeof(T) * count) ==
         sizeof(T) * count;
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::availableToWriteBytes() const {
  return mDesc->getSize() - availableToReadBytes();
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::availableToWrite() const {
  return availableToWriteBytes()/sizeof(T);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::availableToRead() const {
  return availableToReadBytes()/sizeof(T);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::writeBytes(const uint8_t* data, size_t size) {
  transaction tx = beginWrite(size);
  memcpy(tx.first.address, data, tx.first.length);
  memcpy(tx.second.address, data + tx.first.length, tx.second.length);
  size_t result = tx.first.length + tx.second.length;
  commitWrite(result);
  return result;
}

/*
 * The below method does not check for available space since it was already
 * checked by write() API which invokes writeBytes() which in turn calls
 * beginWrite().
 */
template <typename T, MQFlavor flavor>
typename MessageQueue<T, flavor>::transaction MessageQueue<T, flavor>::beginWrite(
    size_t nBytesDesired) const {
  transaction result;
  auto writePtr = mWritePtr->load(std::memory_order_relaxed);
  size_t writeOffset = writePtr % mDesc->getSize();
  size_t contiguous = mDesc->getSize() - writeOffset;
  if (contiguous < nBytesDesired) {
    result = {{mRing + writeOffset, contiguous},
              {mRing, nBytesDesired - contiguous}};
  } else {
    result = {
        {mRing + writeOffset, nBytesDesired}, {0, 0},
    };
  }
  return result;
}

template <typename T, MQFlavor flavor>
__attribute__((no_sanitize("integer")))
void MessageQueue<T, flavor>::commitWrite(size_t nBytesWritten) {
  auto writePtr = mWritePtr->load(std::memory_order_relaxed);
  writePtr += nBytesWritten;
  mWritePtr->store(writePtr, std::memory_order_release);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::availableToReadBytes() const {
  /*
   * Doing relaxed loads here because these accesses don't carry dependencies.
   * Dependent accesses won't happen until after a call to beginWrite or
   * beginRead
   * which do proper acquire/release.
   */
  return mWritePtr->load(std::memory_order_relaxed) -
         mReadPtr->load(std::memory_order_relaxed);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::readBytes(uint8_t* data, size_t size) {
  transaction tx = beginRead(size);
  memcpy(data, tx.first.address, tx.first.length);
  memcpy(data + tx.first.length, tx.second.address, tx.second.length);
  size_t result = tx.first.length + tx.second.length;
  commitRead(result);
  return result;
}

/*
 * The below method does not check whether nBytesDesired bytes are available
 * to read because the check is performed in the read() method before
 * readBytes() is invoked.
 */
template <typename T, MQFlavor flavor>
typename MessageQueue<T, flavor>::transaction MessageQueue<T, flavor>::beginRead(
    size_t nBytesDesired) const {
  transaction result;
  auto readPtr = mReadPtr->load(std::memory_order_relaxed);
  size_t readOffset = readPtr % mDesc->getSize();
  size_t contiguous = mDesc->getSize() - readOffset;

  if (contiguous < nBytesDesired) {
    result = {{mRing + readOffset, contiguous},
              {mRing, nBytesDesired - contiguous}};
  } else {
    result = {
        {mRing + readOffset, nBytesDesired}, {0, 0},
    };
  }

  return result;
}

template <typename T, MQFlavor flavor>
__attribute__((no_sanitize("integer")))
void MessageQueue<T, flavor>::commitRead(size_t nBytesRead) {
  auto readPtr = mReadPtr->load(std::memory_order_relaxed);
  readPtr += nBytesRead;
  mReadPtr->store(readPtr, std::memory_order_release);
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::getQuantumSize() const {
  return mDesc->getQuantum();
}

template <typename T, MQFlavor flavor>
size_t MessageQueue<T, flavor>::getQuantumCount() const {
  return mDesc->getSize() / mDesc->getQuantum();
}

template <typename T, MQFlavor flavor>
bool MessageQueue<T, flavor>::isValid() const {
  return mRing != nullptr && mReadPtr != nullptr && mWritePtr != nullptr;
}

template <typename T, MQFlavor flavor>
void* MessageQueue<T, flavor>::mapGrantorDescr(uint32_t grantor_idx) {
  const native_handle_t* handle = mDesc->getNativeHandle()->handle();
  auto mGrantors = mDesc->getGrantors();
  if ((handle == nullptr) || (grantor_idx >= mGrantors.size())) {
    return nullptr;
  }

  int fdIndex = mGrantors[grantor_idx].fdIndex;
  /*
   * Offset for mmap must be a multiple of PAGE_SIZE.
   */
  int mapOffset = (mGrantors[grantor_idx].offset / PAGE_SIZE) * PAGE_SIZE;
  int mapLength =
      mGrantors[grantor_idx].offset - mapOffset + mGrantors[grantor_idx].extent;

  void* address = mmap(0, mapLength, PROT_READ | PROT_WRITE, MAP_SHARED,
                       handle->data[fdIndex], mapOffset);
  return (address == MAP_FAILED)
             ? nullptr
             : reinterpret_cast<uint8_t*>(address) +
                   (mGrantors[grantor_idx].offset - mapOffset);
}

template <typename T, MQFlavor flavor>
void MessageQueue<T, flavor>::unmapGrantorDescr(void* address,
                                                uint32_t grantor_idx) {
  auto mGrantors = mDesc->getGrantors();
  if ((address == nullptr) || (grantor_idx >= mGrantors.size())) {
    return;
  }

  int mapOffset = (mGrantors[grantor_idx].offset / PAGE_SIZE) * PAGE_SIZE;
  int mapLength =
      mGrantors[grantor_idx].offset - mapOffset + mGrantors[grantor_idx].extent;
  void* baseAddress = reinterpret_cast<uint8_t*>(address) -
                      (mGrantors[grantor_idx].offset - mapOffset);
  if (baseAddress) munmap(baseAddress, mapLength);
}

}  // namespace hardware
}  // namespace android
#endif  // HIDL_MQ_H
