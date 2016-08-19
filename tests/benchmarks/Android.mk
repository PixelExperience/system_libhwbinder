#
# Copyright (C) 2016 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libhwbinder_benchmark

LOCAL_MODULE_TAGS := eng tests

LOCAL_SRC_FILES := Benchmark.cpp
LOCAL_SHARED_LIBRARIES := libhwbinder libutils android.hardware.tests.libhwbinder@1.0
LOCAL_C_INCLUDES := system/libhwbinder/include

LOCAL_STATIC_LIBRARIES := libtestUtil
LOCAL_COMPATIBILITY_SUITE := vts

include $(BUILD_NATIVE_BENCHMARK)
