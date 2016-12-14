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
#include <gtest/gtest.h>
#include <atomic>
#include <cstdlib>
#include <sstream>
#include <thread>
#include <MessageQueue.h>
#include <EventFlag.h>

enum EventFlagBits : uint32_t {
  kFmqNotEmpty = 1 << 0,
  kFmqNotFull = 1 << 1,
};

class SynchronizedReadWrites : public ::testing::Test {
 protected:
  virtual void TearDown() {
      delete mQueue;
  }

  virtual void SetUp() {
    static constexpr size_t kNumElementsInQueue = 2048;
    mQueue = new (std::nothrow) android::hardware::MessageQueue<uint8_t,
          android::hardware::kSynchronizedReadWrite>(kNumElementsInQueue);
    ASSERT_NE(nullptr, mQueue);
    ASSERT_TRUE(mQueue->isValid());
    mNumMessagesMax = mQueue->getQuantumCount();
    ASSERT_EQ(kNumElementsInQueue, mNumMessagesMax);
  }

  android::hardware::MessageQueue<uint8_t,
      android::hardware::kSynchronizedReadWrite>* mQueue = nullptr;
  size_t mNumMessagesMax = 0;
};

class UnsynchronizedWrite : public ::testing::Test {
 protected:
  virtual void TearDown() {
      delete mQueue;
  }

  virtual void SetUp() {
    static constexpr size_t kNumElementsInQueue = 2048;
    mQueue = new (std::nothrow) android::hardware::MessageQueue<
        uint8_t, android::hardware::kUnsynchronizedWrite>(kNumElementsInQueue);
    ASSERT_NE(nullptr, mQueue);
    ASSERT_TRUE(mQueue->isValid());
    mNumMessagesMax = mQueue->getQuantumCount();
    ASSERT_EQ(kNumElementsInQueue, mNumMessagesMax);
  }

  android::hardware::MessageQueue<uint8_t,
      android::hardware::kUnsynchronizedWrite>* mQueue = nullptr;
  size_t mNumMessagesMax = 0;
};

class BlockingReadWrites : public ::testing::Test {
 protected:
  virtual void TearDown() {
    delete mQueue;
  }
  virtual void SetUp() {
    static constexpr size_t kNumElementsInQueue = 2048;
    mQueue = new (std::nothrow) android::hardware::MessageQueue<
        uint8_t, android::hardware::kSynchronizedReadWrite>(kNumElementsInQueue);
    ASSERT_NE(nullptr, mQueue);
    ASSERT_TRUE(mQueue->isValid());
    mNumMessagesMax = mQueue->getQuantumCount();
    ASSERT_EQ(kNumElementsInQueue, mNumMessagesMax);
  }

  android::hardware::MessageQueue<
      uint8_t, android::hardware::kSynchronizedReadWrite>* mQueue;
  std::atomic<uint32_t> fw;
  size_t mNumMessagesMax = 0;
};

/*
 * This thread will attempt to read and block. When wait returns
 * it checks if the kFmqNotEmpty bit is actually set.
 * If the read is succesful, it signals Wake to kFmqNotFull.
 */
void ReaderThreadBlocking(
        android::hardware::MessageQueue<uint8_t,
        android::hardware::kSynchronizedReadWrite>* fmsg_queue, std::atomic<uint32_t>* fw_addr) {
  const size_t data_len = 64;
  uint8_t data[data_len];
  android::hardware::EventFlag* ef_group = nullptr;
  android::status_t status = android::hardware::EventFlag::createEventFlag(fw_addr, &ef_group);
  ASSERT_EQ(android::NO_ERROR, status);
  ASSERT_NE(nullptr, ef_group);

  while (true) {
    uint32_t ef_state = 0;
    android::status_t ret = ef_group->wait(kFmqNotEmpty, &ef_state, NULL);
    ASSERT_EQ(android::NO_ERROR, ret);
    if ((ef_state & kFmqNotEmpty) && fmsg_queue->read(data, data_len)) {
      ef_group->wake(kFmqNotFull);
      break;
    }
  }

  status = android::hardware::EventFlag::deleteEventFlag(&ef_group);
  ASSERT_EQ(android::NO_ERROR, status);
}

/*
 * Test that basic blocking works.
 */
TEST_F(BlockingReadWrites, SmallInputTest1) {
  const size_t data_len = 64;
  uint8_t data[data_len] = {0};
  /*
   * This is the initial state for the futex word
   * that will create the Event Flag group.
   */
  std::atomic_fetch_or(&fw, static_cast<uint32_t>(kFmqNotFull));

  android::hardware::EventFlag* ef_group = nullptr;
  android::status_t status = android::hardware::EventFlag::createEventFlag(&fw, &ef_group);

  ASSERT_EQ(android::NO_ERROR, status);
  ASSERT_NE(nullptr, ef_group);

  /*
   * Start a thread that will try to read and block on kFmqNotEmpty.
   */
  std::thread Reader(ReaderThreadBlocking, mQueue, &fw);
  struct timespec wait_time = {0, 100 * 1000000};
  ASSERT_EQ(0, nanosleep(&wait_time, NULL));

  /*
   * After waiting for some time write into the FMQ
   * and call Wake on kFmqNotEmpty.
   */
  ASSERT_TRUE(mQueue->write(data, data_len));
  status = ef_group->wake(kFmqNotEmpty);
  ASSERT_EQ(android::NO_ERROR, status);

  ASSERT_EQ(0, nanosleep(&wait_time, NULL));
  Reader.join();

  status = android::hardware::EventFlag::deleteEventFlag(&ef_group);
  ASSERT_EQ(android::NO_ERROR, status);
}

/*
 * Verify that a few bytes of data can be successfully written and read.
 */
TEST_F(SynchronizedReadWrites, SmallInputTest1) {
  const size_t data_len = 16;
  ASSERT_LE(data_len, mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_EQ(0, memcmp(data, read_data, data_len));
}

/*
 * Verify that read() returns false when trying to read from an empty queue.
 */
TEST_F(SynchronizedReadWrites, ReadWhenEmpty) {
  ASSERT_EQ(0UL, mQueue->availableToRead());
  const size_t data_len = 2;
  ASSERT_LE(data_len, mNumMessagesMax);
  uint8_t read_data[data_len];
  ASSERT_FALSE(mQueue->read(read_data, data_len));
}

/*
 * Write the queue until full. Verify that another write is unsuccessful.
 * Verify that availableToWrite() returns 0 as expected.
 */

TEST_F(SynchronizedReadWrites, WriteWhenFull) {
  ASSERT_EQ(0UL, mQueue->availableToRead());
  std::vector<uint8_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_EQ(0UL, mQueue->availableToWrite());
  ASSERT_FALSE(mQueue->write(&data[0], 1));

  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_TRUE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_EQ(data, read_data);
}

/*
 * Write a chunk of data equal to the queue size.
 * Verify that the write is successful and the subsequent read
 * returns the expected data.
 */
TEST_F(SynchronizedReadWrites, LargeInputTest1) {
  std::vector<uint8_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_TRUE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_EQ(data, read_data);
}

/*
 * Attempt to write a chunk of data larger than the queue size.
 * Verify that it fails. Verify that a subsequent read fails and
 * the queue is still empty.
 */
TEST_F(SynchronizedReadWrites, LargeInputTest2) {
  ASSERT_EQ(0UL, mQueue->availableToRead());
  const size_t data_len = 4096;
  ASSERT_GT(data_len, mNumMessagesMax);
  std::vector<uint8_t> data(data_len);
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_FALSE(mQueue->write(&data[0], data_len));
  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_FALSE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_NE(data, read_data);
  ASSERT_EQ(0UL, mQueue->availableToRead());
}

/*
 * After the queue is full, try to write more data. Verify that
 * the attempt returns false. Verify that the attempt did not
 * affect the pre-existing data in the queue.
 */
TEST_F(SynchronizedReadWrites, LargeInputTest3) {
  std::vector<uint8_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_FALSE(mQueue->write(&data[0], 1));
  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_TRUE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_EQ(data, read_data);
}

/*
 * Verify that multiple reads one after the other return expected data.
 */
TEST_F(SynchronizedReadWrites, MultipleRead) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_LE(data_len, mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  uint8_t read_data[data_len] = {};
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->read(read_data + i * chunkSize, chunkSize));
  }
  ASSERT_EQ(0, memcmp(read_data, data, data_len));
}

/*
 * Verify that multiple writes one after the other happens correctly.
 */
TEST_F(SynchronizedReadWrites, MultipleWrite) {
  const int chunkSize = 100;
  const int chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_LE(data_len, mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  for (unsigned int i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->write(data + i * chunkSize, chunkSize));
  }
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_EQ(0, memcmp(read_data, data, data_len));
}

/*
 * Write enough messages into the FMQ to fill half of it
 * and read back the same.
 * Write mNumMessagesMax messages into the queue. This will cause a
 * wrap around. Read and verify the data.
 */
TEST_F(SynchronizedReadWrites, ReadWriteWrapAround) {
  size_t numMessages = mNumMessagesMax / 2;
  std::vector<uint8_t> data(mNumMessagesMax);
  std::vector<uint8_t> read_data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], numMessages));
  ASSERT_TRUE(mQueue->read(&read_data[0], numMessages));
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_EQ(data, read_data);
}

/*
 * Verify that a few bytes of data can be successfully written and read.
 */
TEST_F(UnsynchronizedWrite, SmallInputTest1) {
  const size_t data_len = 16;
  ASSERT_LE(data_len, mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_EQ(0, memcmp(data, read_data, data_len));
}

/*
 * Verify that read() returns false when trying to read from an empty queue.
 */
TEST_F(UnsynchronizedWrite, ReadWhenEmpty) {
  ASSERT_EQ(0UL, mQueue->availableToRead());
  const size_t data_len = 2;
  ASSERT_TRUE(data_len < mNumMessagesMax);
  uint8_t read_data[data_len];
  ASSERT_FALSE(mQueue->read(read_data, data_len));
}

/*
 * Write the queue when full. Verify that a subsequent writes is succesful.
 * Verify that availableToWrite() returns 0 as expected.
 */

TEST_F(UnsynchronizedWrite, WriteWhenFull) {
  ASSERT_EQ(0UL, mQueue->availableToRead());
  std::vector<uint8_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_EQ(0UL, mQueue->availableToWrite());
  ASSERT_TRUE(mQueue->write(&data[0], 1));

  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_FALSE(mQueue->read(&read_data[0], mNumMessagesMax));
}

/*
 * Write a chunk of data equal to the queue size.
 * Verify that the write is successful and the subsequent read
 * returns the expected data.
 */
TEST_F(UnsynchronizedWrite, LargeInputTest1) {
  std::vector<uint8_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_TRUE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_EQ(data, read_data);
}

/*
 * Attempt to write a chunk of data larger than the queue size.
 * Verify that it fails. Verify that a subsequent read fails and
 * the queue is still empty.
 */
TEST_F(UnsynchronizedWrite, LargeInputTest2) {
  ASSERT_EQ(0UL, mQueue->availableToRead());
  const size_t data_len = 4096;
  ASSERT_GT(data_len, mNumMessagesMax);
  std::vector<uint8_t> data(data_len);
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_FALSE(mQueue->write(&data[0], data_len));
  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_FALSE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_NE(data, read_data);
  ASSERT_EQ(0UL, mQueue->availableToRead());
}

/*
 * After the queue is full, try to write more data. Verify that
 * the attempt is succesful. Verify that the read fails
 * as expected.
 */
TEST_F(UnsynchronizedWrite, LargeInputTest3) {
  std::vector<uint8_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->write(&data[0], 1));
  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_FALSE(mQueue->read(&read_data[0], mNumMessagesMax));
}

/*
 * Verify that multiple reads one after the other return expected data.
 */
TEST_F(UnsynchronizedWrite, MultipleRead) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_LE(data_len, mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  uint8_t read_data[data_len] = {};
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->read(read_data + i * chunkSize, chunkSize));
  }
  ASSERT_EQ(0, memcmp(read_data, data, data_len));
}

/*
 * Verify that multiple writes one after the other happens correctly.
 */
TEST_F(UnsynchronizedWrite, MultipleWrite) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_LE(data_len, mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->write(data + i * chunkSize, chunkSize));
  }
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_EQ(0, memcmp(read_data, data, data_len));
}

/*
 * Write enough messages into the FMQ to fill half of it
 * and read back the same.
 * Write mNumMessagesMax messages into the queue. This will cause a
 * wrap around. Read and verify the data.
 */
TEST_F(UnsynchronizedWrite, ReadWriteWrapAround) {
  size_t numMessages = mNumMessagesMax / 2;
  std::vector<uint8_t> data(mNumMessagesMax);
  std::vector<uint8_t> read_data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], numMessages));
  ASSERT_TRUE(mQueue->read(&read_data[0], numMessages));
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_EQ(data, read_data);
}
