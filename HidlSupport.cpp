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

#include <hwbinder/HidlSupport.h>

namespace android {
namespace hardware {

static const char *const kEmptyString = "";

hidl_string::hidl_string()
    : buffer(const_cast<char *>(kEmptyString)),
      length(0) {
}

hidl_string::~hidl_string() {
    clear();
}

hidl_string::hidl_string(const hidl_string &other)
    : buffer(const_cast<char *>(kEmptyString)),
      length(0) {
    setTo(other.c_str(), other.size());
}

hidl_string &hidl_string::operator=(const hidl_string &other) {
    if (this != &other) {
        setTo(other.c_str(), other.size());
    }

    return *this;
}

hidl_string &hidl_string::operator=(const char *s) {
    return setTo(s, strlen(s));
}

hidl_string &hidl_string::setTo(const char *data, size_t size) {
    clear();

    buffer = (char *)malloc(size + 1);
    memcpy(buffer, data, size);
    buffer[size] = '\0';

    length = size;

    return *this;
}

void hidl_string::clear() {
    if (buffer != kEmptyString) {
        free(buffer);
    }

    buffer = const_cast<char *>(kEmptyString);
    length = 0;
}

const char *hidl_string::c_str() const {
    return buffer ? buffer : "";
}

size_t hidl_string::size() const {
    return length;
}

bool hidl_string::empty() const {
    return length == 0;
}

status_t hidl_string::readEmbeddedFromParcel(
        const Parcel &parcel, size_t parentHandle, size_t parentOffset) {
    const void *ptr = parcel.readEmbeddedBuffer(
            nullptr /* buffer_handle */,
            parentHandle,
            parentOffset + offsetof(hidl_string, buffer));

    return ptr != NULL ? OK : UNKNOWN_ERROR;
}

status_t hidl_string::writeEmbeddedToParcel(
        Parcel *parcel, size_t parentHandle, size_t parentOffset) const {
    return parcel->writeEmbeddedBuffer(
            buffer,
            length < 0 ? strlen(buffer) + 1 : length + 1,
            nullptr /* handle */,
            parentHandle,
            parentOffset + offsetof(hidl_string, buffer));
}

}  // namespace hardware
}  // namespace android


