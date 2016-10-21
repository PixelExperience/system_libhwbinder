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
#include <cstdlib>
#include <sstream>
#include "../common/MessageQueue.h"

class SynchronizedReadWrites : public ::testing::Test {
 protected:
  virtual void TearDown() {
      delete mQueue;
  }

  virtual void SetUp() {
    static constexpr size_t kNumElementsInQueue = 2048;
    mQueue = new android::hardware::MessageQueue<uint8_t,
          android::hardware::kSynchronizedReadWrite>(kNumElementsInQueue);
    ASSERT_TRUE(mQueue != nullptr);
    ASSERT_TRUE(mQueue->isValid());
    mNumMessagesMax = mQueue->getQuantumCount();
    ASSERT_EQ(mNumMessagesMax, kNumElementsInQueue);
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
    mQueue = new android::hardware::MessageQueue<
        uint8_t, android::hardware::kUnsynchronizedWrite>(kNumElementsInQueue);
    ASSERT_TRUE(mQueue != nullptr);
    ASSERT_TRUE(mQueue->isValid());
    mNumMessagesMax = mQueue->getQuantumCount();
    ASSERT_EQ(mNumMessagesMax, kNumElementsInQueue);
  }

  android::hardware::MessageQueue<uint8_t,
      android::hardware::kUnsynchronizedWrite>* mQueue = nullptr;
  size_t mNumMessagesMax = 0;
};

/*
 * Verify that a few bytes of data can be successfully written and read.
 */
TEST_F(SynchronizedReadWrites, SmallInputTest1) {
  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_TRUE(memcmp(data, read_data, data_len) == 0);
}

/*
 * Verify that read() returns false when trying to read from an empty queue.
 */
TEST_F(SynchronizedReadWrites, ReadWhenEmpty) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  const size_t data_len = 2;
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  uint8_t read_data[data_len];
  ASSERT_FALSE(mQueue->read(read_data, data_len));
}

/*
 * Write the queue until full. Verify that another write is unsuccessful.
 * Verify that availableToWrite() returns 0 as expected.
 */

TEST_F(SynchronizedReadWrites, WriteWhenFull) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  std::vector<uint8_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->availableToWrite() == 0);
  ASSERT_FALSE(mQueue->write(&data[0], 1));

  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_TRUE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_TRUE(data == read_data);
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
  ASSERT_TRUE(data == read_data);
}

/*
 * Attempt to write a chunk of data larger than the queue size.
 * Verify that it fails. Verify that a subsequent read fails and
 * the queue is still empty.
 */
TEST_F(SynchronizedReadWrites, LargeInputTest2) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  const size_t data_len = 4096;
  ASSERT_TRUE(data_len > mNumMessagesMax);
  std::vector<uint8_t> data(data_len);
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_FALSE(mQueue->write(&data[0], data_len));
  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_FALSE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_FALSE(data == read_data);
  ASSERT_TRUE(mQueue->availableToRead() == 0);
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
  ASSERT_TRUE(read_data == data);
}

/*
 * Verify that multiple reads one after the other return expected data.
 */
TEST_F(SynchronizedReadWrites, MultipleRead) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_TRUE(data_len <=  mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  uint8_t read_data[data_len] = {};
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->read(read_data + i * chunkSize, chunkSize));
  }
  ASSERT_TRUE(memcmp(read_data, data, data_len) == 0);
}

/*
 * Verify that multiple writes one after the other happens correctly.
 */
TEST_F(SynchronizedReadWrites, MultipleWrite) {
  const int chunkSize = 100;
  const int chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_TRUE(data_len <=  mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  for (unsigned int i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->write(data + i * chunkSize, chunkSize));
  }
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_TRUE(memcmp(read_data, data, data_len) == 0);
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
  ASSERT_TRUE(data == read_data);
}

/*
 * Verify that a few bytes of data can be successfully written and read.
 */
TEST_F(UnsynchronizedWrite, SmallInputTest1) {
  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_TRUE(memcmp(data, read_data, data_len) == 0);
}

/*
 * Verify that read() returns false when trying to read from an empty queue.
 */
TEST_F(UnsynchronizedWrite, ReadWhenEmpty) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
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
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  std::vector<uint8_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->availableToWrite() == 0);
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
  ASSERT_TRUE(data == read_data);
}

/*
 * Attempt to write a chunk of data larger than the queue size.
 * Verify that it fails. Verify that a subsequent read fails and
 * the queue is still empty.
 */
TEST_F(UnsynchronizedWrite, LargeInputTest2) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  const size_t data_len = 4096;
  ASSERT_TRUE(data_len > mNumMessagesMax);
  std::vector<uint8_t> data(data_len);
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_FALSE(mQueue->write(&data[0], data_len));
  std::vector<uint8_t> read_data(mNumMessagesMax);
  ASSERT_FALSE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_FALSE(data == read_data);
  ASSERT_TRUE(mQueue->availableToRead() == 0);
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
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  uint8_t read_data[data_len] = {};
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->read(read_data + i * chunkSize, chunkSize));
  }
  ASSERT_TRUE(memcmp(read_data, data, data_len) == 0);
}

/*
 * Verify that multiple writes one after the other happens correctly.
 */
TEST_F(UnsynchronizedWrite, MultipleWrite) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t data_len = chunkSize * chunkNum;
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  uint8_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i & 0xFF;
  }
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->write(data + i * chunkSize, chunkSize));
  }
  uint8_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_TRUE(memcmp(read_data, data, data_len) == 0);
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
  ASSERT_TRUE(data == read_data);
}
