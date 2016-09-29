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
#include <hidl/IServiceManager.h>
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
using android::hardware::MQDescriptorSync;
using android::hardware::MessageQueue;

static int numMessagesMax;

namespace android {
namespace hardware {
namespace tests {
namespace client {

const char kServiceName[] = "android.hardware.tests.msgq@1.0::ITestMsgQ";

}  // namespace client
}  // namespace tests
}  // namespace hardware
}  // namespace android

class MQTestClient : public ::testing::Test {
 protected:
  virtual void TearDown() {
    if (fmsg_queue) {
      delete fmsg_queue;
    }
  }

  virtual void SetUp() {
    namespace client_tests = android::hardware::tests::client;

    service = ITestMsgQ::getService(client_tests::kServiceName);
    if (service == nullptr) return;
    service->configureFmqSyncReadWrite([this](
        int32_t bad, const MQDescriptorSync& in) {
      if (!bad) {
        fmsg_queue = new MessageQueue<uint16_t, kSynchronizedReadWrite>(in);
      }
    });
    ASSERT_TRUE(fmsg_queue != nullptr);
    ASSERT_TRUE(fmsg_queue->isValid());
    numMessagesMax = fmsg_queue->getQuantumCount();
  }
  sp<ITestMsgQ> service;
  MessageQueue<uint16_t, kSynchronizedReadWrite>* fmsg_queue = nullptr;
};

/*
 * Utility function to verify data read from the fast message queue.
 */
bool verifyData(uint16_t* data, int count) {
  for (int i = 0; i < count; i++) {
    if (data[i] != i) return false;
  }
  return true;
}

/* Request service to write a small number of messages
 * to the FMQ. Read and verify data.
 */
TEST_F(MQTestClient, SmallInputReaderTest1) {
  const int data_len = 16;
  ASSERT_TRUE(data_len <= numMessagesMax);
  int write_count = service->requestWrite(data_len);
  ASSERT_EQ(write_count, data_len);
  uint16_t read_data[data_len] = {};
  ASSERT_TRUE(fmsg_queue->read(read_data, data_len));
  ASSERT_TRUE(verifyData(read_data, data_len));
}

/*
 * Write a small number of messages to FMQ. Request
 * service to read and verify that the write was succesful.
 */
TEST_F(MQTestClient, SmallInputWriterTest1) {
  const int data_len = 16;
  ASSERT_TRUE(data_len <= numMessagesMax);
  size_t original_count = fmsg_queue->availableToWrite();
  uint16_t data[data_len];
  for (int i = 0; i < data_len; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(fmsg_queue->write(data, data_len));
  int read_count = service->requestRead(data_len);
  ASSERT_EQ(read_count, data_len);
  size_t available_count = fmsg_queue->availableToWrite();
  ASSERT_EQ(original_count, available_count);
}

/*
 * Verify that the FMQ is empty and read fails when it is empty.
 */
TEST_F(MQTestClient, ReadWhenEmpty) {
  ASSERT_TRUE(fmsg_queue->availableToRead() == 0);
  const int numMessages = 2;
  ASSERT_TRUE(numMessages <= numMessagesMax);
  uint16_t read_data[numMessages];
  ASSERT_FALSE(fmsg_queue->read(read_data, numMessages));
}

/*
 * Verify FMQ is empty.
 * Write enough messages to fill it.
 * Verify availableToWrite() method returns is zero.
 * Try writing another message and verify that
 * the attempted write was unsuccesful. Request service
 * to read and verify the messages in the FMQ.
 */

TEST_F(MQTestClient, WriteWhenFull) {
  uint16_t* data = new uint16_t[numMessagesMax];
  for (int i = 0; i < numMessagesMax; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(fmsg_queue->write(data, numMessagesMax));
  ASSERT_TRUE(fmsg_queue->availableToWrite() == 0);
  ASSERT_FALSE(fmsg_queue->write(data, 1));
  int read_count = service->requestRead(numMessagesMax);
  ASSERT_EQ(read_count, numMessagesMax);
  delete[] data;
}

/*
 * Verify FMQ is empty.
 * Request service to write data equal to queue size.
 * Read and verify data in fmsg_queue.
 */
TEST_F(MQTestClient, LargeInputTest1) {
  int write_count = service->requestWrite(numMessagesMax);
  ASSERT_EQ(write_count, numMessagesMax);
  uint16_t* read_data = new uint16_t[numMessagesMax]();
  ASSERT_TRUE(fmsg_queue->read(read_data, numMessagesMax));
  ASSERT_TRUE(verifyData(read_data, numMessagesMax));
  delete[] read_data;
}

/*
 * Request service to write more than maximum number of messages to the FMQ.
 * Verify that the write fails. Verify that availableToRead() method
 * still returns 0 and verify that attempt to read fails.
 */
TEST_F(MQTestClient, LargeInputTest2) {
  ASSERT_TRUE(fmsg_queue->availableToRead() == 0);
  const int numMessages = 2048;
  ASSERT_TRUE(numMessages > numMessagesMax);
  int write_count = service->requestWrite(numMessages);
  int expected_count = 0;
  ASSERT_EQ(write_count, expected_count);
  uint16_t read_data;
  ASSERT_TRUE(fmsg_queue->availableToRead() == 0);
  ASSERT_FALSE(fmsg_queue->read(&read_data, 1));
}

/*
 * Write until FMQ is full.
 * Verify that the number of messages available to write
 * is equal to numMessagesMax.
 * Verify that another write attempt fails.
 * Request service to read. Verify read count.
 */
TEST_F(MQTestClient, LargeInputTest3) {
  uint16_t* data = new uint16_t[numMessagesMax];
  for (int i = 0; i < numMessagesMax; i++) {
    data[i] = i;
  }

  ASSERT_TRUE(fmsg_queue->write(data, numMessagesMax));
  ASSERT_TRUE(fmsg_queue->availableToWrite() == 0);
  ASSERT_FALSE(fmsg_queue->write(data, 1));

  int read_count = service->requestRead(numMessagesMax);
  ASSERT_EQ(read_count, numMessagesMax);
  delete[] data;
}

/*
 * Confirm that the FMQ is empty. Request service to write to FMQ.
 * Do multiple reads to empty FMQ and verify data.
 */
TEST_F(MQTestClient, MultipleRead) {
  const int chunkSize = 100;
  const int chunkNum = 5;
  const int numMessages = chunkSize * chunkNum;
  ASSERT_TRUE(numMessages <= numMessagesMax);
  int availableToRead = fmsg_queue->availableToRead();
  int expected_count = 0;
  ASSERT_EQ(availableToRead, expected_count);
  int write_count = service->requestWrite(numMessages);
  ASSERT_EQ(write_count, numMessages);
  uint16_t read_data[numMessages] = {};
  for (int i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(fmsg_queue->read(read_data + i * chunkSize, chunkSize));
  }
  ASSERT_TRUE(verifyData(read_data, numMessages));
}

/*
 * Write to FMQ in bursts.
 * Request service to read data. Verify read_count.
 */
TEST_F(MQTestClient, MultipleWrite) {
  const int chunkSize = 100;
  const int chunkNum = 5;
  const int numMessages = chunkSize * chunkNum;
  ASSERT_TRUE(numMessages <= numMessagesMax);
  uint16_t data[numMessages];
  for (int i = 0; i < numMessages; i++) {
    data[i] = i;
  }
  for (int i = 0; i < chunkNum; i++) {
    ASSERT_TRUE(fmsg_queue->write(data + i * chunkSize, chunkSize));
  }
  int read_count = service->requestRead(numMessages);
  ASSERT_TRUE(read_count == numMessages);
}

/*
 * Write enough messages into the FMQ to fill half of it.
 * Request service to read back the same.
 * Write numMessagesMax messages into the queue. This should cause a
 * wrap around. Request service to read and verify the data.
 */
TEST_F(MQTestClient, ReadWriteWrapAround) {
  int numMessages = numMessagesMax / 2;
  uint16_t* data = new uint16_t[numMessagesMax];
  for (int i = 0; i < numMessagesMax; i++) {
    data[i] = i;
  }
  ASSERT_TRUE(fmsg_queue->write(data, numMessages));
  int read_count = service->requestRead(numMessages);
  ASSERT_TRUE(read_count == numMessages);
  ASSERT_TRUE(fmsg_queue->write(data, numMessagesMax));
  read_count = service->requestRead(numMessagesMax);
  ASSERT_TRUE(read_count == numMessagesMax);
  delete[] data;
}
