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

#include <../common/MessageQueue.h>
#include <android/hardware/tests/msgq/1.0/ITestMsgQ.h>
#include <hwbinder/IInterface.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>

// libutils:
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;

// libhwbinder:
using android::hardware::BnInterface;
using android::hardware::defaultServiceManager;
using android::hardware::IInterface;
using android::hardware::IPCThreadState;
using android::hardware::Parcel;
using android::hardware::ProcessState;
using android::hardware::Return;
using android::hardware::Void;

// Standard library
using std::string;
using std::vector;

// libhidl
using android::hardware::kSynchronizedReadWrite;
using android::hardware::kUnsynchronizedWrite;
using android::hardware::MQDescriptorSync;
using android::hardware::MQDescriptorUnsync;

using android::hardware::MessageQueue;

// Generated HIDL files
using android::hardware::tests::msgq::V1_0::ITestMsgQ;

const char kServiceName[] = "android.hardware.tests.msgq@1.0::ITestMsgQ";

namespace {

class BinderCallback : public LooperCallback {
 public:
  BinderCallback() {}
  ~BinderCallback() override {}

  int handleEvent(int /* fd */, int /* events */, void* /* data */) override {
    IPCThreadState::self()->handlePolledCommands();
    return 1;  // Continue receiving callbacks.
  }
};

class TestMsgQ : public ITestMsgQ {
 public:
  TestMsgQ()
      : fmsg_queue_synchronized(nullptr), fmsg_queue_unsynchronized(nullptr) {}
  virtual ~TestMsgQ() {
      delete fmsg_queue_synchronized;
      delete fmsg_queue_unsynchronized;
  }

  virtual Return<bool> requestWriteFmqSync(int count) {
    vector<uint16_t> data(count);
    for (int i = 0; i < count; i++) {
      data[i] = i;
    }
    bool result = fmsg_queue_synchronized->write(&data[0], count);
    return result;
  }

  virtual Return<bool> requestReadFmqSync(int count) {
    vector<uint16_t> data(count);
    bool result = fmsg_queue_synchronized->read(&data[0], count)
                    && verifyData(&data[0], count);
    return result;
  }

  virtual Return<bool> requestWriteFmqUnsync(int count) {
    vector<uint16_t> data(count);
    for (int i = 0; i < count; i++) {
      data[i] = i;
    }
    bool result = fmsg_queue_unsynchronized->write(&data[0], count);
    return result;
  }

  virtual Return<bool> requestReadFmqUnsync(int count) {
    vector<uint16_t> data(count);
    bool result =
        fmsg_queue_unsynchronized->read(&data[0], count) && verifyData(&data[0], count);
    return result;
  }

  virtual Return<void> configureFmqSyncReadWrite(
      ITestMsgQ::configureFmqSyncReadWrite_cb callback) {
    static constexpr size_t kNumElementsInQueue = 1024;
    fmsg_queue_synchronized =
        new MessageQueue<uint16_t, kSynchronizedReadWrite>(kNumElementsInQueue);
    if ((fmsg_queue_synchronized == nullptr) || (fmsg_queue_synchronized->isValid() == false)) {
      callback(false /* ret */, MQDescriptorSync(
                   std::vector<android::hardware::GrantorDescriptor>(),
                   nullptr /* nhandle */, 0 /* size */));
    } else {
      callback(true /* ret */, *fmsg_queue_synchronized->getDesc());
    }
    return Void();
  }

  virtual Return<void> configureFmqUnsyncWrite(
      ITestMsgQ::configureFmqUnsyncWrite_cb callback) {
    static constexpr size_t kNumElementsInQueue = 1024;
    fmsg_queue_unsynchronized =
        new MessageQueue<uint16_t, kUnsynchronizedWrite>(
            kNumElementsInQueue);
    if ((fmsg_queue_unsynchronized == nullptr) ||
        (fmsg_queue_unsynchronized->isValid() == false)) {
      callback(false /* ret */,
               MQDescriptorUnsync(
                   std::vector<android::hardware::GrantorDescriptor>(),
                   nullptr /* nhandle */, 0 /* size */));
    } else {
      callback(true /* ret */, *fmsg_queue_unsynchronized->getDesc());
    }
    return Void();
  }

  android::hardware::MessageQueue<uint16_t,
      android::hardware::kSynchronizedReadWrite>* fmsg_queue_synchronized;
  android::hardware::MessageQueue<uint16_t,
      android::hardware::kUnsynchronizedWrite>* fmsg_queue_unsynchronized;

 private:
  /*
   * Utility function to verify data read from the fast message queue.
   */
  bool verifyData(uint16_t* data, int count) {
    for (int i = 0; i < count; i++) {
      if (data[i] != i) return false;
    }

    return true;
  }
};

int Run() {
  android::sp<TestMsgQ> service = new TestMsgQ;
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
  service->registerAsService(kServiceName);

  ALOGI("Entering loop");
  while (true) {
    const int result = looper->pollAll(-1 /* timeoutMillis */);
    ALOGI("Looper returned %d", result);
  }
  return 0;
}

}  // namespace

int main(int /* argc */, char* /* argv */ []) { return Run(); }
