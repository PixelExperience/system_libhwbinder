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

#include <android/hardware/benchmarks/msgq/1.0/IBenchmarkMsgQ.h>
#include <cutils/ashmem.h>
#include <fmq/MessageQueue.h>
#include <hidl/IServiceManager.h>
#include <hwbinder/IInterface.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <hwbinder/Status.h>
#include <unistd.h>
#include <utils/Errors.h>
#include <utils/Log.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>
#include <iostream>
#include <thread>

// libutils:
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;
using android::String16;

// libhwbinder:
using android::hardware::defaultServiceManager;
using android::hardware::IInterface;
using android::hardware::IPCThreadState;
using android::hardware::Parcel;
using android::hardware::ProcessState;
using android::hardware::hidl_version;
using android::hardware::make_hidl_version;
using android::hardware::Status;
using android::hardware::Return;
// Standard library
using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::unique_ptr;
using std::vector;

// Generated HIDL files

using android::hardware::benchmarks::msgq::V1_0::IBenchmarkMsgQ;

typedef uint64_t RingBufferPosition;
/*
 * All benchmark test will per performed on a FMQ of size kQueueSize
 */
const size_t kQueueSize = 16 * 1024;

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

/*
 * This is the size of ashmem region that will be created for the FMQ.
 */

const size_t kAshmemSize = 20 * 1024;

const char kServiceName[] =
    "android.hardware.benchmarks.msgq@1.0::IBenchmarkMsgQ";

namespace {
/*
 * This method writes numIter packets into the fmsg_queue_outbox_ queue
 * and notes the time before each write in the time_data_ array. It will
 * be used to calculate the average server to client write to read delay.
 */
void QueueWriter(android::hardware::MessageQueue<uint8_t>* fmsg_queue_outbox_,
                 int64_t* time_data_, uint32_t numIter) {
  uint8_t data[kPacketSize64];
  uint32_t num_writes = 0;

  while (num_writes < numIter) {
    do {
      time_data_[num_writes] =
          std::chrono::high_resolution_clock::now().time_since_epoch().count();
    } while (fmsg_queue_outbox_->write(data, kPacketSize64) == false);
    num_writes++;
  }
}

/*
 * The method reads a packet from the inbox queue and writes the same
 * into the outbox queue. The client will calculate the average time taken
 * for each iteration which consists of two write and two read operations.
 */
void QueuePairReadWrite(
    android::hardware::MessageQueue<uint8_t>* fmsg_queue_inbox_,
    android::hardware::MessageQueue<uint8_t>* fmsg_queue_outbox_,
    uint32_t numIter) {
  uint8_t data[kPacketSize64];
  uint32_t num_round_trips = 0;

  while (num_round_trips < numIter) {
    while (fmsg_queue_inbox_->read(data, kPacketSize64) == false)
      ;
    while (fmsg_queue_outbox_->write(data, kPacketSize64) == false)
      ;
    num_round_trips++;
  }
}
class BinderCallback : public LooperCallback {
 public:
  BinderCallback() {}
  ~BinderCallback() override {}

  int handleEvent(int /* fd */, int /* events */, void* /* data */) override {
    IPCThreadState::self()->handlePolledCommands();
    return 1;  // Continue receiving callbacks.
  }
};

class BenchmarkMsgQ : public IBenchmarkMsgQ {
 public:
  BenchmarkMsgQ()
      : fmsg_queue_inbox_(nullptr),
        fmsg_queue_outbox_(nullptr),
        time_data_(nullptr) {}
  virtual ~BenchmarkMsgQ() {
    if (fmsg_queue_inbox_) delete fmsg_queue_inbox_;
    if (fmsg_queue_outbox_) delete fmsg_queue_outbox_;
    if (time_data_) delete[] time_data_;
  }
  virtual Status BenchmarkPingPong(uint32_t numIter) {
    std::thread(QueuePairReadWrite, fmsg_queue_inbox_, fmsg_queue_outbox_,
                numIter)
        .detach();
    return Status::ok();
  }
  virtual Status BenchmarkServiceWriteClientRead(uint32_t numIter) {
    if (time_data_) delete[] time_data_;
    time_data_ = new int64_t[numIter];
    std::thread(QueueWriter, fmsg_queue_outbox_, time_data_, numIter).detach();
    return Status::ok();
  }
  // TODO:: Change callback argument to bool.
  virtual Return<int32_t> RequestWrite(int count) {
    uint8_t* data = new uint8_t[count];
    for (int i = 0; i < count; i++) {
      data[i] = i;
    }
    if (fmsg_queue_outbox_->write(data, count)) {
      delete[] data;
      return count;
    }
    delete[] data;
    return 0;
  }
  // TODO:: Change callback argument to bool.
  virtual Return<int32_t> RequestRead(int count) {
    uint8_t* data = new uint8_t[count];
    if (fmsg_queue_inbox_->read(data, count)) {
      delete[] data;
      return count;
    }
    delete[] data;
    return 0;
  }
  /*
   * This method is used by the client to send the server timestamps to
   * calculate the server to client write to read delay.
   */
  virtual Status SendTimeData(
      const android::hardware::hidl_vec<int64_t>& client_rcv_time_array) {
    int64_t accumulated_time = 0;
    for (uint32_t i = 0; i < client_rcv_time_array.size(); i++) {
      std::chrono::time_point<std::chrono::high_resolution_clock>
          client_rcv_time((std::chrono::high_resolution_clock::duration(
              client_rcv_time_array[i])));
      std::chrono::time_point<std::chrono::high_resolution_clock>
          server_send_time(
              (std::chrono::high_resolution_clock::duration(time_data_[i])));
      accumulated_time += static_cast<int64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(client_rcv_time -
                                                               server_send_time)
              .count());
    }
    accumulated_time /= client_rcv_time_array.size();
    cout << "Average service to client write to read delay::"
         << accumulated_time << "ns" << endl;
    return Status::ok();
  }
  /*
   * Utility function to create an MQ given an fd and the queue_size.
   * The read pointer counter, write pointer counter and the
   * data buffer would be mapped from various offsets of the fd.
   * TODO: Create a constructor for MessageQueue that is able to
   * take these parameters.
   */
  android::hardware::MessageQueue<uint8_t>* CreateMessageQueue(
      int fd, uint32_t queue_size) {
    native_handle_t* mq_handle = native_handle_create(1, 0);
    if (!mq_handle) {
      ALOGE("Unable to create native_handle_t");
      return nullptr;
    }

    std::vector<android::hardware::GrantorDescriptor> Grantors(
        MINIMUM_GRANTOR_COUNT);

    mq_handle->data[0] = fd;

    /*
     * Create Grantor Descriptors for read, write pointers and the data buffer.
     */
    Grantors[android::hardware::READPTRPOS] = {0, 0, 0,
                                               sizeof(RingBufferPosition)};
    Grantors[android::hardware::WRITEPTRPOS] = {
        0, 0, sizeof(RingBufferPosition), sizeof(RingBufferPosition)};
    Grantors[android::hardware::DATAPTRPOS] = {
        0, 0, 2 * sizeof(RingBufferPosition), queue_size};

    android::hardware::MQDescriptor mydesc(Grantors, mq_handle, 0,
                                           sizeof(uint8_t));
    return new android::hardware::MessageQueue<uint8_t>(mydesc);
  }
  /*
   * This method requests the service to configure the client's outbox queue.
   */
  virtual Status ConfigureClientOutbox(
      IBenchmarkMsgQ::ConfigureClientOutbox_cb callback) {
    int ashmemFd =
        ashmem_create_region("MessageQueueClientOutbox", kAshmemSize);
    ashmem_set_prot_region(ashmemFd, PROT_READ | PROT_WRITE);
    if (fmsg_queue_inbox_) delete fmsg_queue_inbox_;
    fmsg_queue_inbox_ = CreateMessageQueue(ashmemFd, kQueueSize);

    IBenchmarkMsgQ::WireMQDescriptor* wmsgq_desc =
        CreateWireMQDescriptor(*fmsg_queue_inbox_->getDesc());
    callback(*wmsgq_desc);
    delete wmsgq_desc;
    return Status::ok();
  }
  /*
   * This method requests the service to configure the client's inbox queue.
   */
  virtual Status ConfigureClientInbox(
      IBenchmarkMsgQ::ConfigureClientInbox_cb callback) {
    int ashmemFd = ashmem_create_region("MessageQueueClientInbox", kAshmemSize);
    ashmem_set_prot_region(ashmemFd, PROT_READ | PROT_WRITE);

    if (fmsg_queue_outbox_) delete fmsg_queue_outbox_;
    fmsg_queue_outbox_ = CreateMessageQueue(ashmemFd, kQueueSize);

    IBenchmarkMsgQ::WireMQDescriptor* wmsgq_desc =
        CreateWireMQDescriptor(*fmsg_queue_outbox_->getDesc());
    callback(*wmsgq_desc);
    delete wmsgq_desc;
    return Status::ok();
  }

  android::hardware::MessageQueue<uint8_t>* fmsg_queue_inbox_;
  android::hardware::MessageQueue<uint8_t>* fmsg_queue_outbox_;
  int64_t* time_data_;

 private:
  /*
   * Create WireMQDescriptor from MQDescriptor.
   * TODO: This code will move into the MessageQueue class shortly.
   */
  IBenchmarkMsgQ::WireMQDescriptor* CreateWireMQDescriptor(
      const android::hardware::MQDescriptor& rb_desc) {
    IBenchmarkMsgQ::WireMQDescriptor* wmq_desc =
        new IBenchmarkMsgQ::WireMQDescriptor;
    const vector<android::hardware::GrantorDescriptor>& vec_gd =
        rb_desc.getGrantors();
    wmq_desc->grantors.resize(vec_gd.size());
    for (size_t i = 0; i < vec_gd.size(); i++) {
      wmq_desc->grantors[i] = {
          0, {vec_gd[i].fdIndex, vec_gd[i].offset, vec_gd[i].extent}};
    }

    wmq_desc->mq_handle = (rb_desc.getHandle())->handle();
    wmq_desc->quantum = rb_desc.getQuantum();
    wmq_desc->flags = rb_desc.getFlags();

    return wmq_desc;
  }
};

int Run() {
  android::sp<BenchmarkMsgQ> service = new BenchmarkMsgQ;
  sp<Looper> looper(Looper::prepare(0 /* opts */));
  int binder_fd = -1;
  ProcessState::self()->setThreadPoolMaxThreadCount(0);
  IPCThreadState::self()->disableBackgroundScheduling(true);
  IPCThreadState::self()->setupPolling(&binder_fd);
  if (binder_fd < 0) return -1;

  sp<BinderCallback> cb(new BinderCallback);
  if (looper->addFd(binder_fd, Looper::POLL_CALLBACK, Looper::EVENT_INPUT, cb,
                    nullptr) != 1) {
    ALOGE("Failed to add binder FD to Looper");
    return -1;
  }
  hidl_version version = android::hardware::make_hidl_version(4, 0);
  service->registerAsService(String16(kServiceName), version);

  ALOGI("Entering loop");
  while (true) {
    const int result = looper->pollAll(-1 /* timeoutMillis */);
  }
  return 0;
}

}  // namespace

int main(int /* argc */, char* /* argv */ []) { return Run(); }
