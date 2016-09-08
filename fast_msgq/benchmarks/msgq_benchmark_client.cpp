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
#include <android/hardware/benchmarks/msgq/1.0/IBenchmarkMsgQ.h>
#include <asm-generic/mman.h>
#include <cutils/ashmem.h>
#include <fmq/MessageQueue.h>
#include <gtest/gtest.h>
#include <hidl/IServiceManager.h>
#include <utils/StrongPointer.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include "cutils/trace.h"

// libutils:
using android::OK;
using android::sp;
using android::status_t;

// generated
using android::hardware::benchmarks::msgq::V1_0::IBenchmarkMsgQ;
using std::cerr;
using std::cout;
using std::endl;

using namespace android::hardware;

/*
 * All the benchmark cases will be performed on an FMQ of size kQueueSize.
 */
static const int32_t kQueueSize = 1024 * 16;

/*
 * The number of iterations for each experiment.
 */
static const uint32_t kNumIterations = 1000;

/*
 * The various packet sizes used are as follows.
 */
enum PacketSizes {
  kPacketSize64 = 64,
  kPacketSize128 = 128,
  kPacketSize256 = 256,
  kPacketSize512 = 512,
  kPacketSize1024 = 1024
};

const char kServiceName[] =
    "android.hardware.benchmarks.msgq@1.0::IBenchmarkMsgQ";

class MQTestClient : public ::testing::Test {
 protected:
  virtual void TearDown() {
    if (fmsg_queue_inbox_) delete fmsg_queue_inbox_;
    if (fmsg_queue_outbox_) delete fmsg_queue_outbox_;
  }

  virtual void SetUp() {
    service = IBenchmarkMsgQ::getService(kServiceName);
    if (service == nullptr) return;
    /*
     * Request service to configure the client inbox queue.
     */
    service->ConfigureClientInbox(
        [this](const IBenchmarkMsgQ::WireMQDescriptor& in) {
          CreateMQFromWireMQDescriptor(&this->fmsg_queue_inbox_, &in);
        });
    ASSERT_TRUE(fmsg_queue_inbox_ != nullptr);
    ASSERT_TRUE(fmsg_queue_inbox_->isValid());
    /*
     * Reqeust service to configure the client outbox queue.
     */
    service->ConfigureClientOutbox(
        [this](const IBenchmarkMsgQ::WireMQDescriptor& out) {
          CreateMQFromWireMQDescriptor(&this->fmsg_queue_outbox_, &out);
        });

    ASSERT_TRUE(fmsg_queue_outbox_ != nullptr);
    ASSERT_TRUE(fmsg_queue_outbox_->isValid());
  }
  sp<IBenchmarkMsgQ> service;
  android::hardware::MessageQueue<uint8_t>* fmsg_queue_inbox_ = nullptr;
  android::hardware::MessageQueue<uint8_t>* fmsg_queue_outbox_ = nullptr;

 private:
  /*
   * TODO:The following code will move into the MessageQueue class
   * shortly.
   */
  void CreateMQFromWireMQDescriptor(
      android::hardware::MessageQueue<uint8_t>** fmsg_queue,
      const IBenchmarkMsgQ::WireMQDescriptor* wmq_desc) {
    if ((wmq_desc == nullptr) || (wmq_desc->mq_handle == nullptr) ||
        (wmq_desc->mq_handle == nullptr)) {
      std::cout << "Bad WireMQDescriptor" << std::endl;
      return;
    }
    /*
     * Create a copy of the native handle from the WireMQDescriptor.
     */
    native_handle_t* mq_handle = native_handle_create(
        wmq_desc->mq_handle->numFds, wmq_desc->mq_handle->numInts);
    if (mq_handle == nullptr) return;
    for (int i = 0; i < wmq_desc->mq_handle->numFds; i++)
      mq_handle->data[i] = wmq_desc->mq_handle->data[i];
    memcpy(&mq_handle->data[mq_handle->numFds],
           &wmq_desc->mq_handle->data[mq_handle->numFds],
           mq_handle->numInts * sizeof(int));
    /*
     * Create the vector of GrantorDescriptors.
     */
    std::vector<android::hardware::GrantorDescriptor> Grantors(
        MINIMUM_GRANTOR_COUNT);
    Grantors[android::hardware::READPTRPOS] = {
        0, wmq_desc->grantors[android::hardware::READPTRPOS].shm.fdIndex,
        wmq_desc->grantors[android::hardware::READPTRPOS].shm.offset,
        wmq_desc->grantors[android::hardware::READPTRPOS].shm.extent};
    Grantors[android::hardware::WRITEPTRPOS] = {
        0, wmq_desc->grantors[android::hardware::WRITEPTRPOS].shm.fdIndex,
        wmq_desc->grantors[android::hardware::WRITEPTRPOS].shm.offset,
        wmq_desc->grantors[android::hardware::WRITEPTRPOS].shm.extent};
    Grantors[android::hardware::DATAPTRPOS] = {
        0, wmq_desc->grantors[android::hardware::DATAPTRPOS].shm.fdIndex,
        wmq_desc->grantors[android::hardware::DATAPTRPOS].shm.offset,
        wmq_desc->grantors[android::hardware::DATAPTRPOS].shm.extent};
    android::hardware::MQDescriptor mq_desc(Grantors, mq_handle, 0,
                                            sizeof(uint8_t));
    *fmsg_queue = new android::hardware::MessageQueue<uint8_t>(mq_desc);
  }
};

/*
 * Client writes a 64 byte packet into the outbox queue, service reads the
 * same and
 * writes the packet into the client's inbox queue. Client reads the packet. The
 * average time taken for the cycle is measured.
 */
TEST_F(MQTestClient, BenchMarkMeasurePingPongTransfer) {
  uint8_t* data = new uint8_t[kPacketSize64];
  ASSERT_TRUE(data != nullptr);
  int64_t accumulated_time = 0;
  size_t num_round_trips = 0;
  /*
   * This method requests the service to create a thread which reads
   * from fmsg_queue_outbox_ and writes into fmsg_queue_inbox_.
   */
  service->BenchmarkPingPong(kNumIterations);
  std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
      std::chrono::high_resolution_clock::now();
  while (num_round_trips < kNumIterations) {
    while (fmsg_queue_outbox_->write(data, kPacketSize64) == 0) {
    }
    while (fmsg_queue_inbox_->read(data, kPacketSize64) == 0) {
    }
    num_round_trips++;
  }
  std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
      std::chrono::high_resolution_clock::now();
  accumulated_time +=
      static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               time_end - time_start)
                               .count());
  accumulated_time /= kNumIterations;

  cout << "Round trip time for " << kPacketSize64
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Measure the average time taken to read 64 bytes from the queue.
 */
TEST_F(MQTestClient, BenchMarkMeasureRead64Bytes) {
  uint8_t* data = new uint8_t[kPacketSize64];
  ASSERT_TRUE(data != nullptr);

  uint32_t num_loops = kQueueSize / kPacketSize64;
  uint64_t accumulated_time = 0;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    int32_t write_count = service->RequestWrite(kQueueSize);
    ASSERT_TRUE(write_count == kQueueSize);
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
        std::chrono::high_resolution_clock::now();
    /*
    * The read() method returns true only if the the correct number of bytes
    * were succesfully read from the queue.
    */
    for (uint32_t j = 0; j < num_loops; j++) {
      ASSERT_TRUE(fmsg_queue_inbox_->read(data, kPacketSize64));
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
        std::chrono::high_resolution_clock::now();
    accumulated_time += (time_end - time_start).count();
  }
  accumulated_time /= (num_loops * kNumIterations);
  cout << "Average time to read" << kPacketSize64
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Measure the average time taken to read 128 bytes.
 */
TEST_F(MQTestClient, BenchMarkMeasureRead128Bytes) {
  uint8_t* data = new uint8_t[kPacketSize128];
  ASSERT_TRUE(data != nullptr);

  uint32_t num_loops = kQueueSize / kPacketSize128;
  uint64_t accumulated_time = 0;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    int32_t write_count = service->RequestWrite(kQueueSize);
    ASSERT_TRUE(write_count == kQueueSize);
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
        std::chrono::high_resolution_clock::now();

    /*
     * The read() method returns true only if the the correct number of bytes
     * were succesfully read from the queue.
     */
    for (uint32_t j = 0; j < num_loops; j++) {
      ASSERT_TRUE(fmsg_queue_inbox_->read(data, kPacketSize128));
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
        std::chrono::high_resolution_clock::now();
    accumulated_time += (time_end - time_start).count();
  }
  accumulated_time /= (num_loops * kNumIterations);
  cout << "Average time to read" << kPacketSize128
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Measure the average time taken to read 256 bytes from the queue.
 */
TEST_F(MQTestClient, BenchMarkMeasureRead256Bytes) {
  uint8_t* data = new uint8_t[kPacketSize256];
  ASSERT_TRUE(data != nullptr);
  uint32_t num_loops = kQueueSize / kPacketSize256;
  uint64_t accumulated_time = 0;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    int32_t write_count = service->RequestWrite(kQueueSize);
    ASSERT_TRUE(write_count == kQueueSize);
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
        std::chrono::high_resolution_clock::now();
    /*
     * The read() method returns true only if the the correct number of bytes
     * were succesfully read from the queue.
     */
    for (uint32_t j = 0; j < num_loops; j++) {
      ASSERT_TRUE(fmsg_queue_inbox_->read(data, kPacketSize256));
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
        std::chrono::high_resolution_clock::now();
    accumulated_time += (time_end - time_start).count();
  }
  accumulated_time /= (num_loops * kNumIterations);
  cout << "Average time to read" << kPacketSize256
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Measure the average time taken to read 512 bytes from the queue.
 */
TEST_F(MQTestClient, BenchMarkMeasureRead512Bytes) {
  uint8_t* data = new uint8_t[kPacketSize512];
  ASSERT_TRUE(data != nullptr);
  uint32_t num_loops = kQueueSize / kPacketSize512;
  uint64_t accumulated_time = 0;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    int32_t write_count = service->RequestWrite(kQueueSize);
    ASSERT_TRUE(write_count == kQueueSize);
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
        std::chrono::high_resolution_clock::now();
    /*
     * The read() method returns true only if the the correct number of bytes
     * were succesfully read from the queue.
     */
    for (uint32_t j = 0; j < num_loops; j++) {
      ASSERT_TRUE(fmsg_queue_inbox_->read(data, kPacketSize512));
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
        std::chrono::high_resolution_clock::now();
    accumulated_time += (time_end - time_start).count();
  }
  accumulated_time /= (num_loops * kNumIterations);
  cout << "Average time to read" << kPacketSize512
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Measure the average time taken to write 64 bytes into the queue.
 */
TEST_F(MQTestClient, BenchMarkMeasureWrite64Bytes) {
  uint8_t* data = new uint8_t[kPacketSize64];
  ASSERT_TRUE(data != nullptr);
  uint32_t num_loops = kQueueSize / kPacketSize64;
  uint64_t accumulated_time = 0;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
        std::chrono::high_resolution_clock::now();
    /*
     * Write until the queue is full and request service to empty the queue.
     */
    for (uint32_t j = 0; j < num_loops; j++) {
      bool result = fmsg_queue_outbox_->write(data, kPacketSize64);
      ASSERT_TRUE(result);
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
        std::chrono::high_resolution_clock::now();
    accumulated_time += (time_end - time_start).count();

    int32_t read_count = service->RequestRead(kQueueSize);
    ASSERT_TRUE(read_count == kQueueSize);
  }
  accumulated_time /= (num_loops * kNumIterations);
  cout << "Average time to write " << kPacketSize64
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Measure the average time taken to write 128 bytes into the queue.
 */
TEST_F(MQTestClient, BenchMarkMeasureWrite128Bytes) {
  uint8_t* data = new uint8_t[kPacketSize128];
  ASSERT_TRUE(data != nullptr);
  uint32_t num_loops = kQueueSize / kPacketSize128;
  uint64_t accumulated_time = 0;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
        std::chrono::high_resolution_clock::now();
    /*
     * Write until the queue is full and request service to empty the queue.
     */
    for (uint32_t j = 0; j < num_loops; j++) {
      ASSERT_TRUE(fmsg_queue_outbox_->write(data, kPacketSize128));
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
        std::chrono::high_resolution_clock::now();
    accumulated_time += (time_end - time_start).count();

    int32_t read_count = service->RequestRead(kQueueSize);
    ASSERT_TRUE(read_count == kQueueSize);
  }
  accumulated_time /= (num_loops * kNumIterations);
  cout << "Average time to write " << kPacketSize128
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Measure the average time taken to write 256 bytes into the queue.
 */
TEST_F(MQTestClient, BenchMarkMeasureWrite256Bytes) {
  uint8_t* data = new uint8_t[kPacketSize256];
  ASSERT_TRUE(data != nullptr);
  uint32_t num_loops = kQueueSize / kPacketSize256;
  uint64_t accumulated_time = 0;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
        std::chrono::high_resolution_clock::now();
    /*
     * Write until the queue is full and request service to empty the queue.
     */
    for (uint32_t j = 0; j < num_loops; j++) {
      ASSERT_TRUE(fmsg_queue_outbox_->write(data, kPacketSize256));
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
        std::chrono::high_resolution_clock::now();
    accumulated_time += (time_end - time_start).count();

    int32_t read_count = service->RequestRead(kQueueSize);
    ASSERT_TRUE(read_count == kQueueSize);
  }
  accumulated_time /= (num_loops * kNumIterations);
  cout << "Average time to write " << kPacketSize256
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Measure the average time taken to write 512 bytes into the queue.
 */
TEST_F(MQTestClient, BenchMarkMeasureWrite512Bytes) {
  uint8_t* data = new uint8_t[kPacketSize512];
  ASSERT_TRUE(data != nullptr);
  uint32_t num_loops = kQueueSize / kPacketSize512;
  uint64_t accumulated_time = 0;
  for (uint32_t i = 0; i < kNumIterations; i++) {
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start =
        std::chrono::high_resolution_clock::now();
    /*
     * Write until the queue is full and request service to empty the queue.
     * The write() method returns true only if the specified number of bytes
     * were succesfully written.
     */
    for (uint32_t j = 0; j < num_loops; j++) {
      ASSERT_TRUE(fmsg_queue_outbox_->write(data, kPacketSize512));
    }
    std::chrono::time_point<std::chrono::high_resolution_clock> time_end =
        std::chrono::high_resolution_clock::now();
    accumulated_time += (time_end - time_start).count();

    int32_t read_count = service->RequestRead(kQueueSize);
    ASSERT_TRUE(read_count == kQueueSize);
  }
  accumulated_time /= (num_loops * kNumIterations);
  cout << "Average time to write " << kPacketSize512
       << "bytes: " << accumulated_time << "ns" << endl;
  delete[] data;
}

/*
 * Service continuously writes a packet of 64 bytes into the client's inbox
 * queue
 * of size 16K. Client keeps reading from the inbox queue. The average write to
 * read delay is calculated.
 */
TEST_F(MQTestClient, BenchMarkMeasureServiceWriteClientRead) {
  uint8_t* data = new uint8_t[kPacketSize64];
  ASSERT_TRUE(data != nullptr);
  /*
   * This method causes the service to create a thread which writes
   * into the fmsg_queue_inbox_ queue kNumIterations packets.
   */
  service->BenchmarkServiceWriteClientRead(kNumIterations);
  hidl_vec<int64_t> client_rcv_time_array;
  client_rcv_time_array.resize(kNumIterations);
  for (uint32_t i = 0; i < kNumIterations; i++) {
    do {
      client_rcv_time_array[i] =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();
    } while (fmsg_queue_inbox_->read(data, kPacketSize64) == 0);
  }
  service->SendTimeData(client_rcv_time_array);
  delete[] data;
}
