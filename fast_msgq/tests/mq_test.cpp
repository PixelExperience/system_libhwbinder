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

#include <asm-generic/mman.h>
#include <cutils/ashmem.h>
#include <gtest/gtest.h>
#include <cstdlib>
#include <sstream>
#include "../common/MessageQueue.h"


class MQTests : public ::testing::Test {
 protected:
  virtual void TearDown() {
      delete fmsgq;
  }

  virtual void SetUp() {
    static constexpr size_t kNumElementsInQueue = 2048;
    static constexpr size_t kQueueSizeBytes =
        kNumElementsInQueue * sizeof(uint8_t);
    /*
     * The FMQ needs to allocate memory for the ringbuffer as well as for the
     * read and write pointer counters. Also, Ashmem memory region size needs to
     * be specified in page-aligned bytes.
     */
    static constexpr size_t kAshmemSizePageAligned =
        (kQueueSizeBytes + 2 * sizeof(android::hardware::RingBufferPosition) +
         PAGE_SIZE - 1) &
        ~(PAGE_SIZE - 1);
    /*
     * Create an ashmem region to map the memory for the ringbuffer,
     * read counter and write counter.
     */
    int ashmemFd = ashmem_create_region("MessageQueue", kAshmemSizePageAligned);
    ashmem_set_prot_region(ashmemFd, PROT_READ | PROT_WRITE);
    ASSERT_TRUE(ashmemFd >= 0);
    native_handle_t* mq_handle = native_handle_create(1 /* numFds */,
                                                      0 /* numInts */);
    ASSERT_TRUE(mq_handle != nullptr);
    /*
     * The native handle will contain the fds to be
     * mapped.
     */
    mq_handle->data[0] = ashmemFd;
    /*
     * The FMQ described by this descriptor can hold a maximum of
     * kQueueSizeBytes bytes or kNumElementsInQueue items of type uint8_t.
     */
    android::hardware::MQDescriptorSync mydesc(kQueueSizeBytes, mq_handle,
                                               sizeof(uint8_t));
    fmsgq = new android::hardware::MessageQueue<uint8_t,
          android::hardware::kSynchronizedReadWrite>(mydesc);
    ASSERT_TRUE(fmsgq != nullptr);
    ASSERT_TRUE(fmsgq->isValid());
    numMessagesMax = fmsgq->getQuantumCount();
    ASSERT_EQ(numMessagesMax, kNumElementsInQueue);
  }

  android::hardware::MessageQueue<uint8_t,
      android::hardware::kSynchronizedReadWrite>* fmsgq = nullptr;
  size_t numMessagesMax = 0;
};

/*
 * Verify that a few bytes of data can be successfully written and read.
 */
TEST_F(MQTests, SmallInputTest1) {
  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= numMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(fmsgq->write(data, data_len));
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(fmsgq->read(read_data, data_len));
  ASSERT_TRUE(memcmp(data, read_data, data_len) == 0);
}

/*
 * Verify that read() returns false when trying to read from an empty queue.
 */
TEST_F(MQTests, ReadWhenEmpty) {
  ASSERT_TRUE(fmsgq->availableToRead() == 0);
  const size_t data_len = 2;
  ASSERT_TRUE(data_len <= numMessagesMax);
  uint8_t read_data[data_len];
  ASSERT_FALSE(fmsgq->read(read_data, data_len));
}

/*
 * Write the queue when full. Verify that subsequent writes fail.
 * Verify that availableToWrite() returns 0 as expected.
 */

TEST_F(MQTests, WriteWhenFull) {
  ASSERT_TRUE(fmsgq->availableToRead() == 0);
  std::vector<uint8_t> data(numMessagesMax);
  for (size_t i = 0; i < numMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(fmsgq->write(&data[0], numMessagesMax));
  ASSERT_TRUE(fmsgq->availableToWrite() == 0);
  ASSERT_FALSE(fmsgq->write(&data[0], 1));

  std::vector<uint8_t> read_data(numMessagesMax);
  ASSERT_TRUE(fmsgq->read(&read_data[0], numMessagesMax));
  ASSERT_TRUE(data == read_data);
}

/*
 * Write a chunk of data equal to the queue size.
 * Verify that the write is successful and the subsequent read
 * returns the expected data.
 */
TEST_F(MQTests, LargeInputTest1) {
  std::vector<uint8_t> data(numMessagesMax);
  for (size_t i = 0; i < numMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(fmsgq->write(&data[0], numMessagesMax));
  std::vector<uint8_t> read_data(numMessagesMax);
  ASSERT_TRUE(fmsgq->read(&read_data[0], numMessagesMax));
  ASSERT_TRUE(data == read_data);
}

/*
 * Attempt to write a chunk of data larger than the queue size.
 * Verify that it fails. Verify that a subsequent read fails and
 * the queue is still empty.
 */
TEST_F(MQTests, LargeInputTest2) {
  ASSERT_TRUE(fmsgq->availableToRead() == 0);
  const size_t data_len = 4096;
  ASSERT_TRUE(data_len > numMessagesMax);
  std::vector<uint8_t> data(data_len);
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_FALSE(fmsgq->write(&data[0], data_len));
  std::vector<uint8_t> read_data(numMessagesMax);
  ASSERT_FALSE(fmsgq->read(&read_data[0], numMessagesMax));
  ASSERT_FALSE(data == read_data);
  ASSERT_TRUE(fmsgq->availableToRead() == 0);
}

/*
 * After the queue is full, try to write more data. Verify that
 * the attempt returns false. Verify that the attempt did not
 * affect the pre-existing data in the queue.
 */
TEST_F(MQTests, LargeInputTest3) {
  std::vector<uint8_t> data(numMessagesMax);
  for (size_t i = 0; i < numMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(fmsgq->write(&data[0], numMessagesMax));
  ASSERT_FALSE(fmsgq->write(&data[0], 1));
  std::vector<uint8_t> read_data(numMessagesMax);
  ASSERT_TRUE(fmsgq->read(&read_data[0], numMessagesMax));
  ASSERT_TRUE(read_data == data);
}

/*
 * Verify that multiple reads one after the other return expected data.
 */
TEST_F(MQTests, MultipleRead) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_TRUE(data_len <= numMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(fmsgq->write(data, data_len));
  uint8_t read_data[data_len] = {};
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(fmsgq->read(read_data + i * chunkSize, chunkSize));
  }
  ASSERT_TRUE(memcmp(read_data, data, data_len) == 0);
}

/*
 * Verify that multiple writes one after the other happens correctly.
 */
TEST_F(MQTests, MultipleWrite) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_TRUE(data_len <= numMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(fmsgq->write(data + i * chunkSize, chunkSize));
  }
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(fmsgq->read(read_data, data_len));
  ASSERT_TRUE(memcmp(read_data, data, data_len) == 0);
}

/*
 * Write enough messages into the FMQ to fill half of it
 * and read back the same.
 * Write numMessagesMax messages into the queue. This will cause a
 * wrap around. Read and verify the data.
 */
TEST_F(MQTests, ReadWriteWrapAround) {
  size_t numMessages = numMessagesMax / 2;
  std::vector<uint8_t> data(numMessagesMax);
  std::vector<uint8_t> read_data(numMessagesMax);
  for (size_t i = 0; i < numMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(fmsgq->write(&data[0], numMessages));
  ASSERT_TRUE(fmsgq->read(&read_data[0], numMessages));
  ASSERT_TRUE(fmsgq->write(&data[0], numMessagesMax));
  ASSERT_TRUE(fmsgq->read(&read_data[0], numMessagesMax));
  ASSERT_TRUE(data == read_data);
}
