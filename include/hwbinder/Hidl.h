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

#include <cutils/ashmem.h>
#include <sys/mman.h>

namespace android {
namespace hidl {
// ----------------------------------------------------------------------
// Version functions
// TODO probably nicer to make this a struct
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

// ----------------------------------------------------------------------
// Helper functions for ref<>
template <typename T>
int gen_ref(int *fd, T** ptr) {
    // Setup an ashmem region for the object
    // TODO is sizeof() will only work if we're dealing with the correct
    // object type (eg not a base class).
    size_t len = sizeof(T);
    *fd = ashmem_create_region("HIDL Ref", len);
    if (fd < 0) return NO_MEMORY;

    int result = ashmem_set_prot_region(*fd, PROT_READ | PROT_WRITE);
    if (result < 0) {
       return result;
    }
    void* mapped_ptr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    if (mapped_ptr == MAP_FAILED) {
        return -1;
    } else {
        *ptr = (T*) mapped_ptr;
        return 0;
    }
}

template <typename T>
int from_ref(int fd, T** ptr) {
    size_t len = sizeof(T);
    void* mapped_ptr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) return NO_MEMORY;

    *ptr = (T*) mapped_ptr;

    return 0;
}

}; // namespace hidl
}; // namespace android

#endif // ANDROID_HIDL_HIDL_H
