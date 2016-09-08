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

#include <benchmark/benchmark.h>
#include <hidl/Status.h>
#include <hwbinder/ProcessState.h>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utils/StrongPointer.h>

#include <android/hardware/tests/libhwbinder/1.0/IBenchmark.h>

// libutils:
using android::OK;
using android::sp;
using android::status_t;

// libhwbinder:
using android::hardware::BnInterface;
using android::hardware::defaultServiceManager;
using android::hardware::ProcessState;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_vec;

// Standard library
using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::unique_ptr;
using std::vector;

// Generated HIDL files
using android::hardware::tests::libhwbinder::V1_0::IBenchmark;

const char gServiceName[] = "android.hardware.tests.libhwbinder.IBenchmark";

class BenchmarkService : public IBenchmark {
public:
    BenchmarkService() {}
    virtual ~BenchmarkService() = default;
    Return<void> sendVec(const ::android::hardware::hidl_vec<uint8_t>& data, sendVec_cb _hidl_cb) override {
          _hidl_cb(data);
          return Void();
     };
};

static bool startServer() {
    BenchmarkService *service = new BenchmarkService();
    service->registerAsService(gServiceName);
    ProcessState::self()->startThreadPool();
    return 0;
}

static void BM_sendVec(benchmark::State& state) {
    sp<IBenchmark> service;
    // Prepare data to IPC
    hidl_vec<uint8_t> data_vec;
    data_vec.resize(state.range_x());
    for (int i = 0; i < state.range_x(); i++) {
       data_vec[i] = i % 256;
    }
    // getService automatically retries
    service = IBenchmark::getService(gServiceName);
    if (service == nullptr) {
        state.SkipWithError("Failed to retrieve benchmark service.");
    }
    // Start running
    while (state.KeepRunning()) {
       service->sendVec(data_vec, [&] (auto /*res*/) {
               });
    }
}
BENCHMARK(BM_sendVec)->RangeMultiplier(2)->Range(4, 65536);

int main(int argc, char* argv []) {
    ::benchmark::Initialize(&argc, argv);

    pid_t pid = fork();
    if (pid == 0) {
        // Child, start benchmarks
        ::benchmark::RunSpecifiedBenchmarks();
    } else {
        int stat;
        startServer();
        while (true) {
            int stat, retval;
            retval = wait(&stat);
            if (retval == -1 && errno == ECHILD) {
                break;
            }
        }
    };
}
