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

#ifndef ANDROID_HIDL_SUPPORT_H
#define ANDROID_HIDL_SUPPORT_H

#include <hwbinder/Parcel.h>

namespace android {
namespace hardware {

struct hidl_string {
    hidl_string();
    ~hidl_string();

    hidl_string(const hidl_string &);
    hidl_string &operator=(const hidl_string &);

    const char *c_str() const;
    size_t size() const;
    bool empty() const;

    hidl_string &operator=(const char *s);
    void clear();

    status_t readEmbeddedFromParcel(
            const Parcel &parcel, size_t parentHandle, size_t parentOffset);

    status_t writeEmbeddedToParcel(
            Parcel *parcel, size_t parentHandle, size_t parentOffset) const;

private:
    char *buffer;
    ptrdiff_t length;

    hidl_string &setTo(const char *data, size_t size);
};

template<typename T>
struct hidl_vec {
    hidl_vec()
        : mBuffer(NULL),
          mSize(0),
          mOwnsBuffer(true) {
    }

    hidl_vec(const hidl_vec<T> &other)
        : mBuffer(NULL),
          mSize(0),
          mOwnsBuffer(true) {
        *this = other;
    }

    ~hidl_vec() {
        if (mOwnsBuffer) {
            delete[] mBuffer;
        }
        mBuffer = NULL;
    }

    // Reference an existing array _WITHOUT_ taking ownership. It is the
    // caller's responsibility to ensure that the underlying memory stays
    // valid for the lifetime of this hidl_vec.
    void setTo(T *data, size_t size) {
        if (mOwnsBuffer) {
            delete [] mBuffer;
        }
        mBuffer = data;
        mSize = size;
        mOwnsBuffer = false;
    }

    hidl_vec &operator=(const hidl_vec &other) {
        if (this != &other) {
            if (mOwnsBuffer) {
                delete[] mBuffer;
            }
            mBuffer = NULL;
            mSize = other.mSize;
            mOwnsBuffer = true;
            if (mSize > 0) {
                mBuffer = new T[mSize];
                for (size_t i = 0; i < mSize; ++i) {
                    mBuffer[i] = other.mBuffer[i];
                }
            }
        }

        return *this;
    }

    size_t size() const {
        return mSize;
    }

    T &operator[](size_t index) {
        return mBuffer[index];
    }

    const T &operator[](size_t index) const {
        return mBuffer[index];
    }

    void resize(size_t size) {
        T *newBuffer = new T[size];

        for (size_t i = 0; i < std::min(size, mSize); ++i) {
            newBuffer[i] = mBuffer[i];
        }

        if (mOwnsBuffer) {
            delete[] mBuffer;
        }
        mBuffer = newBuffer;

        mSize = size;
        mOwnsBuffer = true;
    }

    status_t readEmbeddedFromParcel(
            const Parcel &parcel,
            size_t parentHandle,
            size_t parentOffset,
            size_t *handle);

    status_t writeEmbeddedToParcel(
            Parcel *parcel,
            size_t parentHandle,
            size_t parentOffset,
            size_t *handle) const;

private:
    T *mBuffer;
    size_t mSize;
    bool mOwnsBuffer;
};

template<typename T>
status_t hidl_vec<T>::readEmbeddedFromParcel(
        const Parcel &parcel,
        size_t parentHandle,
        size_t parentOffset,
        size_t *handle) {
    const void *ptr = parcel.readEmbeddedBuffer(
            handle,
            parentHandle,
            parentOffset + offsetof(hidl_vec<T>, mBuffer));

    return ptr != NULL ? OK : UNKNOWN_ERROR;
}

template<typename T>
status_t hidl_vec<T>::writeEmbeddedToParcel(
        Parcel *parcel,
        size_t parentHandle,
        size_t parentOffset,
        size_t *handle) const {
    return parcel->writeEmbeddedBuffer(
            mBuffer,
            sizeof(T) * mSize,
            handle,
            parentHandle,
            parentOffset + offsetof(hidl_vec<T>, mBuffer));
}

}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HIDL_SUPPORT_H

