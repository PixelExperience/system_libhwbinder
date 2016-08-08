# Copyright (C) 2009 The Android Open Source Project
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

# we have the common sources, plus some device-specific stuff
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libhwbinder
LOCAL_SHARED_LIBRARIES := libbase liblog libcutils libutils
LOCAL_EXPORT_SHARED_LIBRARY_HEADERS := libbase libutils

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include

LOCAL_CLANG := true
LOCAL_SANITIZE := integer
LOCAL_SRC_FILES := \
	Binder.cpp \
	BpBinder.cpp \
	BufferedTextOutput.cpp \
	Debug.cpp \
	HidlSupport.cpp \
	IInterface.cpp \
	IPCThreadState.cpp \
	IServiceManager.cpp \
	Parcel.cpp \
	ProcessState.cpp \
	Static.cpp \
	Status.cpp \
	TextOutput.cpp

ifneq ($(TARGET_USES_64_BIT_BINDER),true)
ifneq ($(TARGET_IS_64_BIT),true)
LOCAL_CFLAGS += -DBINDER_IPC_32BIT=1
endif
endif
LOCAL_CFLAGS += -Werror
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_MULTILIB := both
LOCAL_COMPATIBILITY_SUITE := vts
include $(BUILD_SHARED_LIBRARY)
-include test/vts/tools/build/Android.packaging_sharedlib.mk

-include $(LOCAL_PATH)/tests/benchmarks/Android.mk $(LOCAL_PATH)/fast_msgq/Android.mk
