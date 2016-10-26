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

#include <android-base/logging.h>
#include <cutils/ashmem.h>
#include <gtest/gtest.h>
#ifndef GTEST_IS_THREADSAFE
#error "GTest did not detect pthread library."
#endif

#include <utils/StrongPointer.h>

#include <android/hardware/tests/msgq/1.0/ITestMsgQ.h>
#include "../common/MessageQueue.h"

// libutils:
using android::OK;
using android::sp;
using android::status_t;

// generated
using android::hardware::tests::msgq::V1_0::ITestMsgQ;

// libhidl
using android::hardware::kSynchronizedReadWrite;
using android::hardware::kUnsynchronizedWrite;
using android::hardware::MessageQueue;
using android::hardware::MQDescriptorSync;
using android::hardware::MQDescriptorUnsync;

namespace android {
namespace hardware {
namespace tests {
namespace client {

const char kServiceName[] = "android.hardware.tests.msgq@1.0::ITestMsgQ";

}  // namespace client
}  // namespace tests
}  // namespace hardware
}  // namespace android

class SynchronizedReadWriteClient : public ::testing::Test {
 protected:
  virtual void TearDown() {
      delete mQueue;
  }

  virtual void SetUp() {
    namespace client_tests = android::hardware::tests::client;

    mService = ITestMsgQ::getService(client_tests::kServiceName);
    ASSERT_NE(mService, nullptr);
    mService->configureFmqSyncReadWrite([this](
        bool ret, const MQDescriptorSync& in) {
      ASSERT_TRUE(ret);
      mQueue = new MessageQueue<uint16_t, kSynchronizedReadWrite>(in);
    });
    ASSERT_TRUE(mQueue != nullptr);
    ASSERT_TRUE(mQueue->isValid());
    mNumMessagesMax = mQueue->getQuantumCount();
  }
  sp<ITestMsgQ> mService;
  MessageQueue<uint16_t, kSynchronizedReadWrite>* mQueue = nullptr;
  size_t mNumMessagesMax = 0;
};

class UnsynchronizedWriteClient : public ::testing::Test {
 protected:
  virtual void TearDown() {
      delete mQueue;
  }

  virtual void SetUp() {
    namespace client_tests = android::hardware::tests::client;

    mService = ITestMsgQ::getService(client_tests::kServiceName);
    ASSERT_NE(mService, nullptr);
    mService->configureFmqUnsyncWrite(
        [this](bool ret, const MQDescriptorUnsync& in) {
          ASSERT_TRUE(ret);
          mQueue = new MessageQueue<uint16_t, kUnsynchronizedWrite>(in);
        });
    ASSERT_TRUE(mQueue != nullptr);
    ASSERT_TRUE(mQueue->isValid());
    mNumMessagesMax = mQueue->getQuantumCount();
  }
  sp<ITestMsgQ> mService;
  MessageQueue<uint16_t, kUnsynchronizedWrite>* mQueue = nullptr;
  size_t mNumMessagesMax = 0;
};

/*
 * Utility function to verify data read from the fast message queue.
 */
bool verifyData(uint16_t* data, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (data[i] != i) return false;
  }
  return true;
}

/* Request mService to write a small number of messages
 * to the FMQ. Read and verify data.
 */
TEST_F(SynchronizedReadWriteClient, SmallInputReaderTest1) {
  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  bool ret = mService->requestWriteFmqSync(data_len);
  ASSERT_TRUE(ret);
  uint16_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_TRUE(verifyData(read_data, data_len));
}

/*
 * Write a small number of messages to FMQ. Request
 * mService to read and verify that the write was succesful.
 */
TEST_F(SynchronizedReadWriteClient, SmallInputWriterTest1) {
  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  size_t original_count = mQueue->availableToWrite();
  uint16_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  bool ret = mService->requestReadFmqSync(data_len);
  ASSERT_TRUE(ret);
  size_t available_count = mQueue->availableToWrite();
  ASSERT_EQ(original_count, available_count);
}

/*
 * Verify that the FMQ is empty and read fails when it is empty.
 */
TEST_F(SynchronizedReadWriteClient, ReadWhenEmpty) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  const size_t numMessages = 2;
  ASSERT_TRUE(numMessages <= mNumMessagesMax);
  uint16_t read_data[numMessages];
  ASSERT_FALSE(mQueue->read(read_data, numMessages));
}

/*
 * Verify FMQ is empty.
 * Write enough messages to fill it.
 * Verify availableToWrite() method returns is zero.
 * Try writing another message and verify that
 * the attempted write was unsuccesful. Request mService
 * to read and verify the messages in the FMQ.
 */

TEST_F(SynchronizedReadWriteClient, WriteWhenFull) {
  std::vector<uint16_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->availableToWrite() == 0);
  ASSERT_FALSE(mQueue->write(&data[0], 1));
  bool ret = mService->requestReadFmqSync(mNumMessagesMax);
  ASSERT_TRUE(ret);
}

/*
 * Verify FMQ is empty.
 * Request mService to write data equal to queue size.
 * Read and verify data in mQueue.
 */
TEST_F(SynchronizedReadWriteClient, LargeInputTest1) {
  bool ret = mService->requestWriteFmqSync(mNumMessagesMax);
  ASSERT_TRUE(ret);
  std::vector<uint16_t> read_data(mNumMessagesMax);
  ASSERT_TRUE(mQueue->read(&read_data[0], mNumMessagesMax));
  ASSERT_TRUE(verifyData(&read_data[0], mNumMessagesMax));
}

/*
 * Request mService to write more than maximum number of messages to the FMQ.
 * Verify that the write fails. Verify that availableToRead() method
 * still returns 0 and verify that attempt to read fails.
 */
TEST_F(SynchronizedReadWriteClient, LargeInputTest2) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  const size_t numMessages = 2048;
  ASSERT_TRUE(numMessages > mNumMessagesMax);
  bool ret = mService->requestWriteFmqSync(numMessages);
  ASSERT_FALSE(ret);
  uint16_t read_data;
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  ASSERT_FALSE(mQueue->read(&read_data, 1));
}

/*
 * Write until FMQ is full.
 * Verify that the number of messages available to write
 * is equal to mNumMessagesMax.
 * Verify that another write attempt fails.
 * Request mService to read. Verify read count.
 */

TEST_F(SynchronizedReadWriteClient, LargeInputTest3) {
  std::vector<uint16_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i;
  }

  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->availableToWrite() == 0);
  ASSERT_FALSE(mQueue->write(&data[0], 1));

  bool ret = mService->requestReadFmqSync(mNumMessagesMax);
  ASSERT_TRUE(ret);
}

/*
 * Confirm that the FMQ is empty. Request mService to write to FMQ.
 * Do multiple reads to empty FMQ and verify data.
 */
TEST_F(SynchronizedReadWriteClient, MultipleRead) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t numMessages = chunkSize * chunkNum;
  ASSERT_TRUE(numMessages <= mNumMessagesMax);
  size_t availableToRead = mQueue->availableToRead();
  size_t expected_count = 0;
  ASSERT_EQ(availableToRead, expected_count);
  bool ret = mService->requestWriteFmqSync(numMessages);
  ASSERT_TRUE(ret);
  uint16_t read_data[numMessages] = {};
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->read(read_data + i * chunkSize, chunkSize));
  }
  ASSERT_TRUE(verifyData(read_data, numMessages));
}

/*
 * Write to FMQ in bursts.
 * Request mService to read data. Verify the read was successful.
 */
TEST_F(SynchronizedReadWriteClient, MultipleWrite) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t numMessages = chunkSize * chunkNum;
  ASSERT_TRUE(numMessages <= mNumMessagesMax);
  uint16_t data[numMessages];
  for (size_t i = 0; i < numMessages; i++) {
    data[i] = i;
  }
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->write(data + i * chunkSize, chunkSize));
  }
  bool ret = mService->requestReadFmqSync(numMessages);
  ASSERT_TRUE(ret);
}

/*
 * Write enough messages into the FMQ to fill half of it.
 * Request mService to read back the same.
 * Write mNumMessagesMax messages into the queue. This should cause a
 * wrap around. Request mService to read and verify the data.
 */
TEST_F(SynchronizedReadWriteClient, ReadWriteWrapAround) {
  size_t numMessages = mNumMessagesMax / 2;
  std::vector<uint16_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(mQueue->write(&data[0], numMessages));
  bool ret = mService->requestReadFmqSync(numMessages);
  ASSERT_TRUE(ret);
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ret = mService->requestReadFmqSync(mNumMessagesMax);
  ASSERT_TRUE(ret);
}

/* Request mService to write a small number of messages
 * to the FMQ. Read and verify data.
 */
TEST_F(UnsynchronizedWriteClient, SmallInputReaderTest1) {
  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  bool ret = mService->requestWriteFmqUnsync(data_len);
  ASSERT_TRUE(ret);
  uint16_t read_data[data_len] = {};
  ASSERT_TRUE(mQueue->read(read_data, data_len));
  ASSERT_TRUE(verifyData(read_data, data_len));
}

/*
 * Write a small number of messages to FMQ. Request
 * mService to read and verify that the write was succesful.
 */
TEST_F(UnsynchronizedWriteClient, SmallInputWriterTest1) {
  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= mNumMessagesMax);
  size_t original_count = mQueue->availableToWrite();
  uint16_t data[data_len];
  for (size_t i = 0; i < data_len; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(mQueue->write(data, data_len));
  bool ret = mService->requestReadFmqUnsync(data_len);
  ASSERT_TRUE(ret);
}

/*
 * Verify that the FMQ is empty and read fails when it is empty.
 */
TEST_F(UnsynchronizedWriteClient, ReadWhenEmpty) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  const size_t numMessages = 2;
  ASSERT_TRUE(numMessages <= mNumMessagesMax);
  uint16_t read_data[numMessages];
  ASSERT_FALSE(mQueue->read(read_data, numMessages));
}

/*
 * Verify FMQ is empty.
 * Write enough messages to fill it.
 * Verify availableToWrite() method returns is zero.
 * Try writing another message and verify that
 * the attempted write was successful. Request mService
 * to read the messages in the FMQ and verify that it is unsuccesful.
 */

TEST_F(UnsynchronizedWriteClient, WriteWhenFull) {
  std::vector<uint16_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->availableToWrite() == 0);
  ASSERT_TRUE(mQueue->write(&data[0], 1));
  bool ret = mService->requestReadFmqUnsync(mNumMessagesMax);
  ASSERT_FALSE(ret);
}

/*
 * Verify FMQ is empty.
 * Request mService to write data equal to queue size.
 * Read and verify data in mQueue.
 */
TEST_F(UnsynchronizedWriteClient, LargeInputTest1) {
  bool ret = mService->requestWriteFmqUnsync(mNumMessagesMax);
  ASSERT_TRUE(ret);
  std::vector<uint16_t> data(mNumMessagesMax);
  ASSERT_TRUE(mQueue->read(&data[0], mNumMessagesMax));
  ASSERT_TRUE(verifyData(&data[0], mNumMessagesMax));
}

/*
 * Request mService to write more than maximum number of messages to the FMQ.
 * Verify that the write fails. Verify that availableToRead() method
 * still returns 0 and verify that attempt to read fails.
 */
TEST_F(UnsynchronizedWriteClient, LargeInputTest2) {
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  const size_t numMessages = mNumMessagesMax + 1;
  bool ret = mService->requestWriteFmqUnsync(numMessages);
  ASSERT_FALSE(ret);
  uint16_t read_data;
  ASSERT_TRUE(mQueue->availableToRead() == 0);
  ASSERT_FALSE(mQueue->read(&read_data, 1));
}

/*
 * Write until FMQ is full.
 * Verify that the number of messages available to write
 * is equal to mNumMessagesMax.
 * Verify that another write attempt is succesful.
 * Request mService to read. Verify that read is unsuccessful.
 * Perform another write and verify that the read is succesful
 * to check if the reader process can recover from the error condition.
 */
TEST_F(UnsynchronizedWriteClient, LargeInputTest3) {
  std::vector<uint16_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i;
  }

  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ASSERT_TRUE(mQueue->availableToWrite() == 0);
  ASSERT_TRUE(mQueue->write(&data[0], 1));

  bool ret = mService->requestReadFmqUnsync(mNumMessagesMax);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));

  ret = mService->requestReadFmqUnsync(mNumMessagesMax);
  ASSERT_TRUE(ret);
}

/*
 * Confirm that the FMQ is empty. Request mService to write to FMQ.
 * Do multiple reads to empty FMQ and verify data.
 */
TEST_F(UnsynchronizedWriteClient, MultipleRead) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t numMessages = chunkSize * chunkNum;
  ASSERT_TRUE(numMessages <= mNumMessagesMax);
  size_t availableToRead = mQueue->availableToRead();
  size_t expected_count = 0;
  ASSERT_EQ(availableToRead, expected_count);
  bool ret = mService->requestWriteFmqUnsync(numMessages);
  ASSERT_TRUE(ret);
  uint16_t read_data[numMessages] = {};
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->read(read_data + i * chunkSize, chunkSize));
  }
  ASSERT_TRUE(verifyData(read_data, numMessages));
}

/*
 * Write to FMQ in bursts.
 * Request mService to read data, verify that it was successful.
 */
TEST_F(UnsynchronizedWriteClient, MultipleWrite) {
  const size_t chunkSize = 100;
  const size_t chunkNum = 5;
  const size_t numMessages = chunkSize * chunkNum;
  ASSERT_TRUE(numMessages <= mNumMessagesMax);
  uint16_t data[numMessages];
  for (size_t i = 0; i < numMessages; i++) {
    data[i] = i;
  }
  for (size_t i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(mQueue->write(data + i * chunkSize, chunkSize));
  }
  bool ret = mService->requestReadFmqUnsync(numMessages);
  ASSERT_TRUE(ret);
}

/*
 * Write enough messages into the FMQ to fill half of it.
 * Request mService to read back the same.
 * Write mNumMessagesMax messages into the queue. This should cause a
 * wrap around. Request mService to read and verify the data.
 */
TEST_F(UnsynchronizedWriteClient, ReadWriteWrapAround) {
  size_t numMessages = mNumMessagesMax / 2;
  std::vector<uint16_t> data(mNumMessagesMax);
  for (size_t i = 0; i < mNumMessagesMax; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(mQueue->write(&data[0], numMessages));
  bool ret = mService->requestReadFmqUnsync(numMessages);
  ASSERT_TRUE(ret);
  ASSERT_TRUE(mQueue->write(&data[0], mNumMessagesMax));
  ret = mService->requestReadFmqUnsync(mNumMessagesMax);
  ASSERT_TRUE(ret);
}

/*
 * Request mService to write a small number of messages
 * to the FMQ. Read and verify data from two threads configured
 * as readers to the FMQ.
 */
TEST_F(UnsynchronizedWriteClient, SmallInputMultipleReaderTest) {
  auto desc = mQueue->getDesc();
  std::unique_ptr<MessageQueue<uint16_t, kUnsynchronizedWrite>> mQueue2(
      new MessageQueue<uint16_t, kUnsynchronizedWrite>(*desc));
  ASSERT_NE(mQueue2.get(), nullptr);

  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= mNumMessagesMax);

  bool ret = mService->requestWriteFmqUnsync(data_len);
  ASSERT_TRUE(ret);

  pid_t pid;
  if ((pid = fork()) == 0) {
    /* child process */
    uint16_t read_data[data_len] = {};
    ASSERT_TRUE(mQueue2->read(read_data, data_len));
    ASSERT_TRUE(verifyData(read_data, data_len));
    exit(0);
  } else {
    ASSERT_GT(pid,
              0 /* parent should see PID greater than 0 for a good fork */);
    uint16_t read_data[data_len] = {};
    ASSERT_TRUE(mQueue->read(read_data, data_len));
    ASSERT_TRUE(verifyData(read_data, data_len));
  }
}

/*
 * Request mService to write into the FMQ until it is full.
 * Request mService to do another write and verify it is successful.
 * Use two reader threads to read and verify that both fail.
 */
TEST_F(UnsynchronizedWriteClient, MultipleReadersAfterOverflow1) {
  auto desc = mQueue->getDesc();
  std::unique_ptr<MessageQueue<uint16_t, kUnsynchronizedWrite>> mQueue2(
      new MessageQueue<uint16_t, kUnsynchronizedWrite>(*desc));
  ASSERT_NE(mQueue2.get(), nullptr);

  bool ret = mService->requestWriteFmqUnsync(mNumMessagesMax);
  ASSERT_TRUE(ret);
  ret = mService->requestWriteFmqUnsync(1);
  ASSERT_TRUE(ret);

  pid_t pid;
  if ((pid = fork()) == 0) {
    /* child process */
    std::vector<uint16_t> read_data(mNumMessagesMax);
    ASSERT_FALSE(mQueue2->read(&read_data[0], mNumMessagesMax));
    exit(0);
  } else {
    ASSERT_GT(pid, 0/* parent should see PID greater than 0 for a good fork */);
    std::vector<uint16_t> read_data(mNumMessagesMax);
    ASSERT_FALSE(mQueue->read(&read_data[0], mNumMessagesMax));
  }
}

/*
 * Request mService to write into the FMQ until it is full.
 * Request mService to do another write and verify it is successful.
 * Use two reader threads to read and verify that both fail.
 * Request mService to write more data into the Queue and verify that both
 * readers are able to recover from the overflow and read successfully.
 */
TEST_F(UnsynchronizedWriteClient, MultipleReadersAfterOverflow2) {
  auto desc = mQueue->getDesc();
  std::unique_ptr<MessageQueue<uint16_t, kUnsynchronizedWrite>> mQueue2(
      new MessageQueue<uint16_t, kUnsynchronizedWrite>(*desc));
  ASSERT_NE(mQueue2.get(), nullptr);

  bool ret = mService->requestWriteFmqUnsync(mNumMessagesMax);
  ASSERT_TRUE(ret);
  ret = mService->requestWriteFmqUnsync(1);
  ASSERT_TRUE(ret);

  const size_t data_len = 16;
  ASSERT_TRUE(data_len <= mNumMessagesMax);

  pid_t pid;
  if ((pid = fork()) == 0) {
    /* child process */
    std::vector<uint16_t> read_data(mNumMessagesMax);
    ASSERT_FALSE(mQueue2->read(&read_data[0], mNumMessagesMax));
    ret = mService->requestWriteFmqUnsync(data_len);
    ASSERT_TRUE(ret);
    ASSERT_TRUE(mQueue2->read(&read_data[0], data_len));
    ASSERT_TRUE(verifyData(&read_data[0], data_len));
    exit(0);
  } else {
    ASSERT_GT(pid, 0/* parent should see PID greater than 0 for a good fork */);

    std::vector<uint16_t> read_data(mNumMessagesMax);
    ASSERT_FALSE(mQueue->read(&read_data[0], mNumMessagesMax));

    int status;
    /*
     * Wait for child process to return before proceeding since the final write
     * is requested by the child.
     */
    ASSERT_EQ(pid, waitpid(pid, &status, 0 /* options */));

    ASSERT_TRUE(mQueue->read(&read_data[0], data_len));
    ASSERT_TRUE(verifyData(&read_data[0], data_len));
  }
}
