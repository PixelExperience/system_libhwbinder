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

//
#ifndef ANDROID_HIDL_HIDL_H
#define ANDROID_HIDL_HIDL_H


namespace android {
namespace hidl {
// ----------------------------------------------------------------------
typedef uint32_t hidl_version;

inline android::hidl::hidl_version make_hidl_version(uint16_t major, uint16_t minor) {
    return (uint32_t) major << 16 | minor;
}

inline uint16_t get_major_hidl_version(android::hidl::hidl_version version) {
    return version >> 16;
}

inline uint16_t get_minor_hidl_version(android::hidl::hidl_version version) {
    return version & 0x0000FFFF;
}

// ----------------------------------------------------------------------

}; // namespace hidl
}; // namespace android

#endif // ANDROID_HIDL_HIDL_H
