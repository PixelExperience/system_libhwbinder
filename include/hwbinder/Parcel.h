/*
 * Copyright (C) 2005 The Android Open Source Project
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

#ifndef ANDROID_HARDWARE_PARCEL_H
#define ANDROID_HARDWARE_PARCEL_H

#include <string>
#include <vector>

#include <android-base/unique_fd.h>
#include <cutils/native_handle.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/String16.h>
#include <utils/Vector.h>

#include <linux/android/binder.h>

#include <hwbinder/IInterface.h>

// ---------------------------------------------------------------------------
namespace android {
class String8;
namespace hardware {

class IBinder;
class IPCThreadState;
class ProcessState;
class TextOutput;

class Parcel {
    friend class IPCThreadState;
public:
    class ReadableBlob;
    class WritableBlob;

                        Parcel();
                        ~Parcel();

    const uint8_t*      data() const;
    size_t              dataSize() const;
    size_t              dataAvail() const;
    size_t              dataPosition() const;
    size_t              dataCapacity() const;

    status_t            setDataSize(size_t size);
    void                setDataPosition(size_t pos) const;
    status_t            setDataCapacity(size_t size);

    status_t            setData(const uint8_t* buffer, size_t len);

    status_t            appendFrom(const Parcel *parcel,
                                   size_t start, size_t len);

    bool                allowFds() const;
    bool                pushAllowFds(bool allowFds);
    void                restoreAllowFds(bool lastValue);

    bool                hasFileDescriptors() const;

    // Writes the RPC header.
    status_t            writeInterfaceToken(const String16& interface);

    // Parses the RPC header, returning true if the interface name
    // in the header matches the expected interface from the caller.
    //
    // Additionally, enforceInterface does part of the work of
    // propagating the StrictMode policy mask, populating the current
    // IPCThreadState, which as an optimization may optionally be
    // passed in.
    bool                enforceInterface(const String16& interface,
                                         IPCThreadState* threadState = NULL) const;
    bool                checkInterface(IBinder*) const;

    void                freeData();

private:
    const binder_size_t* objects() const;

public:
    size_t              objectsCount() const;

    status_t            errorCheck() const;
    void                setError(status_t err);

    status_t            write(const void* data, size_t len);
    void*               writeInplace(size_t len);
    status_t            writeUnpadded(const void* data, size_t len);
    status_t            writeInt8(int8_t val);
    status_t            writeUint8(uint8_t val);
    status_t            writeInt16(int16_t val);
    status_t            writeUint16(uint16_t val);
    status_t            writeInt32(int32_t val);
    status_t            writeUint32(uint32_t val);
    status_t            writeInt64(int64_t val);
    status_t            writeUint64(uint64_t val);
    status_t            writeFloat(float val);
    status_t            writeDouble(double val);
    status_t            writeCString(const char* str);
    status_t            writeString8(const String8& str);
    status_t            writeString16(const String16& str);
    status_t            writeString16(const std::unique_ptr<String16>& str);
    status_t            writeString16(const char16_t* str, size_t len);
    status_t            writeStrongBinder(const sp<IBinder>& val);
    status_t            writeWeakBinder(const wp<IBinder>& val);
    status_t            writeInt32Array(size_t len, const int32_t *val);
    status_t            writeByteArray(size_t len, const uint8_t *val);
    status_t            writeBool(bool val);
    status_t            writeChar(char16_t val);
    status_t            writeByte(int8_t val);

    // Take a UTF8 encoded string, convert to UTF16, write it to the parcel.
    status_t            writeUtf8AsUtf16(const std::string& str);
    status_t            writeUtf8AsUtf16(const std::unique_ptr<std::string>& str);

    status_t            writeByteVector(const std::unique_ptr<std::vector<int8_t>>& val);
    status_t            writeByteVector(const std::vector<int8_t>& val);
    status_t            writeByteVector(const std::unique_ptr<std::vector<uint8_t>>& val);
    status_t            writeByteVector(const std::vector<uint8_t>& val);
    status_t            writeInt32Vector(const std::unique_ptr<std::vector<int32_t>>& val);
    status_t            writeInt32Vector(const std::vector<int32_t>& val);
    status_t            writeInt64Vector(const std::unique_ptr<std::vector<int64_t>>& val);
    status_t            writeInt64Vector(const std::vector<int64_t>& val);
    status_t            writeFloatVector(const std::unique_ptr<std::vector<float>>& val);
    status_t            writeFloatVector(const std::vector<float>& val);
    status_t            writeDoubleVector(const std::unique_ptr<std::vector<double>>& val);
    status_t            writeDoubleVector(const std::vector<double>& val);
    status_t            writeBoolVector(const std::unique_ptr<std::vector<bool>>& val);
    status_t            writeBoolVector(const std::vector<bool>& val);
    status_t            writeCharVector(const std::unique_ptr<std::vector<char16_t>>& val);
    status_t            writeCharVector(const std::vector<char16_t>& val);
    status_t            writeString16Vector(
                            const std::unique_ptr<std::vector<std::unique_ptr<String16>>>& val);
    status_t            writeString16Vector(const std::vector<String16>& val);
    status_t            writeUtf8VectorAsUtf16Vector(
                            const std::unique_ptr<std::vector<std::unique_ptr<std::string>>>& val);
    status_t            writeUtf8VectorAsUtf16Vector(const std::vector<std::string>& val);

    status_t            writeStrongBinderVector(const std::unique_ptr<std::vector<sp<IBinder>>>& val);
    status_t            writeStrongBinderVector(const std::vector<sp<IBinder>>& val);

    // Place a native_handle into the parcel (the native_handle's file-
    // descriptors are dup'ed, so it is safe to delete the native_handle
    // when this function returns).
    // Doesn't take ownership of the native_handle.
    status_t            writeNativeHandle(const native_handle* handle);

    // Place a file descriptor into the parcel.  The given fd must remain
    // valid for the lifetime of the parcel.
    // The Parcel does not take ownership of the given fd unless you ask it to.
    status_t            writeFileDescriptor(int fd, bool takeOwnership = false);

    // Place a file descriptor into the parcel.  A dup of the fd is made, which
    // will be closed once the parcel is destroyed.
    status_t            writeDupFileDescriptor(int fd);

    // Place a file descriptor into the parcel.  This will not affect the
    // semantics of the smart file descriptor. A new descriptor will be
    // created, and will be closed when the parcel is destroyed.
    status_t            writeUniqueFileDescriptor(
                            const base::unique_fd& fd);

    // Place a vector of file desciptors into the parcel. Each descriptor is
    // dup'd as in writeDupFileDescriptor
    status_t            writeUniqueFileDescriptorVector(
                            const std::unique_ptr<std::vector<base::unique_fd>>& val);
    status_t            writeUniqueFileDescriptorVector(
                            const std::vector<base::unique_fd>& val);

    // Writes a blob to the parcel.
    // If the blob is small, then it is stored in-place, otherwise it is
    // transferred by way of an anonymous shared memory region.  Prefer sending
    // immutable blobs if possible since they may be subsequently transferred between
    // processes without further copying whereas mutable blobs always need to be copied.
    // The caller should call release() on the blob after writing its contents.
    status_t            writeBlob(size_t len, bool mutableCopy, WritableBlob* outBlob);

    // Write an existing immutable blob file descriptor to the parcel.
    // This allows the client to send the same blob to multiple processes
    // as long as it keeps a dup of the blob file descriptor handy for later.
    status_t            writeDupImmutableBlobFileDescriptor(int fd);

    template<typename T>
    status_t            writeObject(const T& val, bool nullMetaData);

    status_t            writeBuffer(const void *buffer, size_t length, size_t *handle);
    status_t            writeEmbeddedBuffer(const void *buffer, size_t length, size_t *handle,
                            size_t parent_buffer_handle, size_t parent_offset);
private:
    status_t            writeBufferWithFlags(const void *buffer, size_t length,
                                             size_t *handle, uint32_t flags);
    status_t            writeEmbeddedBufferWithFlags(const void *buffer,
                            size_t length, size_t *handle, uint32_t flags,
                            size_t parent_buffer_handle, size_t parent_offset);
public:
    status_t            writeReference(size_t *handle,
                                       size_t child_buffer_handle, size_t child_offset);
    status_t            writeEmbeddedReference(size_t *handle,
                                               size_t child_buffer_handle, size_t child_offset,
                                               size_t parent_buffer_handle, size_t parent_offset);
    status_t            writeNullReference(size_t *handle);
    status_t            writeEmbeddedNullReference(size_t *handle,
                                                   size_t parent_buffer_handle, size_t parent_offset);


    status_t            writeEmbeddedNativeHandle(const native_handle_t *handle,
                            size_t parent_buffer_handle, size_t parent_offset);
    status_t            writeNativeHandleNoDup(const native_handle* handle);

    void                remove(size_t start, size_t amt);

    status_t            read(void* outData, size_t len) const;
    const void*         readInplace(size_t len) const;
    status_t            readInt8(int8_t *pArg) const;
    status_t            readUint8(uint8_t *pArg) const;
    status_t            readInt16(int16_t *pArg) const;
    status_t            readUint16(uint16_t *pArg) const;
    int32_t             readInt32() const;
    status_t            readInt32(int32_t *pArg) const;
    uint32_t            readUint32() const;
    status_t            readUint32(uint32_t *pArg) const;
    int64_t             readInt64() const;
    status_t            readInt64(int64_t *pArg) const;
    uint64_t            readUint64() const;
    status_t            readUint64(uint64_t *pArg) const;
    float               readFloat() const;
    status_t            readFloat(float *pArg) const;
    double              readDouble() const;
    status_t            readDouble(double *pArg) const;
    intptr_t            readIntPtr() const;
    status_t            readIntPtr(intptr_t *pArg) const;
    bool                readBool() const;
    status_t            readBool(bool *pArg) const;
    char16_t            readChar() const;
    status_t            readChar(char16_t *pArg) const;
    int8_t              readByte() const;
    status_t            readByte(int8_t *pArg) const;

    // Read a UTF16 encoded string, convert to UTF8
    status_t            readUtf8FromUtf16(std::string* str) const;
    status_t            readUtf8FromUtf16(std::unique_ptr<std::string>* str) const;

    const char*         readCString() const;
    String8             readString8() const;
    String16            readString16() const;
    status_t            readString16(String16* pArg) const;
    status_t            readString16(std::unique_ptr<String16>* pArg) const;
    const char16_t*     readString16Inplace(size_t* outLen) const;
    sp<IBinder>         readStrongBinder() const;
    status_t            readStrongBinder(sp<IBinder>* val) const;
    status_t            readNullableStrongBinder(sp<IBinder>* val) const;
    wp<IBinder>         readWeakBinder() const;

    template<typename T>
    status_t            readStrongBinder(sp<T>* val) const;

    template<typename T>
    status_t            readNullableStrongBinder(sp<T>* val) const;

    status_t            readStrongBinderVector(std::unique_ptr<std::vector<sp<IBinder>>>* val) const;
    status_t            readStrongBinderVector(std::vector<sp<IBinder>>* val) const;

    status_t            readByteVector(std::unique_ptr<std::vector<int8_t>>* val) const;
    status_t            readByteVector(std::vector<int8_t>* val) const;
    status_t            readByteVector(std::unique_ptr<std::vector<uint8_t>>* val) const;
    status_t            readByteVector(std::vector<uint8_t>* val) const;
    status_t            readInt32Vector(std::unique_ptr<std::vector<int32_t>>* val) const;
    status_t            readInt32Vector(std::vector<int32_t>* val) const;
    status_t            readInt64Vector(std::unique_ptr<std::vector<int64_t>>* val) const;
    status_t            readInt64Vector(std::vector<int64_t>* val) const;
    status_t            readFloatVector(std::unique_ptr<std::vector<float>>* val) const;
    status_t            readFloatVector(std::vector<float>* val) const;
    status_t            readDoubleVector(std::unique_ptr<std::vector<double>>* val) const;
    status_t            readDoubleVector(std::vector<double>* val) const;
    status_t            readBoolVector(std::unique_ptr<std::vector<bool>>* val) const;
    status_t            readBoolVector(std::vector<bool>* val) const;
    status_t            readCharVector(std::unique_ptr<std::vector<char16_t>>* val) const;
    status_t            readCharVector(std::vector<char16_t>* val) const;
    status_t            readString16Vector(
                            std::unique_ptr<std::vector<std::unique_ptr<String16>>>* val) const;
    status_t            readString16Vector(std::vector<String16>* val) const;
    status_t            readUtf8VectorFromUtf16Vector(
                            std::unique_ptr<std::vector<std::unique_ptr<std::string>>>* val) const;
    status_t            readUtf8VectorFromUtf16Vector(std::vector<std::string>* val) const;

    // Retrieve native_handle from the parcel. This returns a copy of the
    // parcel's native_handle (the caller takes ownership). The caller
    // must free the native_handle with native_handle_close() and
    // native_handle_delete().
    native_handle*     readNativeHandle() const;


    // Retrieve a file descriptor from the parcel.  This returns the raw fd
    // in the parcel, which you do not own -- use dup() to get your own copy.
    int                 readFileDescriptor() const;

    // Retrieve a smart file descriptor from the parcel.
    status_t            readUniqueFileDescriptor(
                            base::unique_fd* val) const;


    // Retrieve a vector of smart file descriptors from the parcel.
    status_t            readUniqueFileDescriptorVector(
                            std::unique_ptr<std::vector<base::unique_fd>>* val) const;
    status_t            readUniqueFileDescriptorVector(
                            std::vector<base::unique_fd>* val) const;

    // Reads a blob from the parcel.
    // The caller should call release() on the blob after reading its contents.
    status_t            readBlob(size_t len, ReadableBlob* outBlob) const;

    template<typename T>
    const T*            readObject(bool nullMetaData) const;

    const void*         readBuffer(size_t *buffer_handle) const;
    const void*         readEmbeddedBuffer(size_t *buffer_handle,
                           size_t parent_buffer_handle, size_t parent_offset) const;
    status_t            readReference(void const* *bufptr,
                                      size_t *buffer_handle, bool *isRef) const;
    status_t            readEmbeddedReference(void const* *bufptr, size_t *buffer_handle,
                                              size_t parent_buffer_handle, size_t parent_offset,
                                              bool *isRef) const;
    const native_handle_t* readEmbeddedNativeHandle(size_t parent_buffer_handle,
                           size_t parent_offset) const;
    const native_handle_t* readNativeHandleNoDup() const;
    // Explicitly close all file descriptors in the parcel.
    void                closeFileDescriptors();

    // Debugging: get metrics on current allocations.
    static size_t       getGlobalAllocSize();
    static size_t       getGlobalAllocCount();

private:
    // Below is a cache that records some information about all actual buffers
    // in this parcel.
    struct BufferInfo {
        size_t index;
        binder_uintptr_t buffer;
        binder_uintptr_t bufend; // buffer + length
    };
    // value of mObjectSize when mBufCache is last updated.
    mutable size_t                  mBufCachePos;
    mutable std::vector<BufferInfo> mBufCache;
    // clear mBufCachePos and mBufCache.
    void                clearCache() const;
    // update mBufCache for all objects between mBufCachePos and mObjectsSize
    void                updateCache() const;
public:

    // The following two methods attempt to find if a chunk of memory ("buffer")
    // is written / read before (by (read|write)(Embedded)?Buffer methods. )
    // 1. Call findBuffer if the chunk of memory could be a small part of a larger
    //    buffer written before (for example, an element of a hidl_vec). The
    //    method will also ensure that the end address (ptr + length) is also
    //    within the buffer.
    // 2. Call quickFindBuffer if the buffer could only be written previously
    //    by itself (for example, the mBuffer field of a hidl_vec). No lengths
    //    are checked.
    status_t            findBuffer(const void *ptr,
                                   size_t length,
                                   bool *found,
                                   size_t *handle,
                                   size_t *offset // valid if found
                                  ) const;
    status_t            quickFindBuffer(const void *ptr,
                                        size_t *handle // valid if found
                                       ) const;

private:
    status_t            incrementNumReferences();
    bool                validateBufferChild(size_t child_buffer_handle,
                                            size_t child_offset) const;
    bool                validateBufferParent(size_t parent_buffer_handle,
                                             size_t parent_offset) const;

private:
    typedef void        (*release_func)(Parcel* parcel,
                                        const uint8_t* data, size_t dataSize,
                                        const binder_size_t* objects, size_t objectsSize,
                                        void* cookie);

    uintptr_t           ipcData() const;
    size_t              ipcDataSize() const;
    uintptr_t           ipcObjects() const;
    size_t              ipcObjectsCount() const;
    size_t              ipcBufferSize() const;
    void                ipcSetDataReference(const uint8_t* data, size_t dataSize,
                                            const binder_size_t* objects, size_t objectsCount,
                                            release_func relFunc, void* relCookie);

public:
    void                print(TextOutput& to, uint32_t flags = 0) const;

private:
                        Parcel(const Parcel& o);
    Parcel&             operator=(const Parcel& o);

    status_t            finishWrite(size_t len);
    void                releaseObjects();
    void                acquireObjects();
    status_t            growData(size_t len);
    status_t            restartWrite(size_t desired);
    status_t            continueWrite(size_t desired);
    status_t            writePointer(uintptr_t val);
    status_t            readPointer(uintptr_t *pArg) const;
    uintptr_t           readPointer() const;
    void                freeDataNoInit();
    void                initState();
    void                scanForFds() const;

    template<class T>
    status_t            readAligned(T *pArg) const;

    template<class T>   T readAligned() const;

    template<class T>
    status_t            writeAligned(T val);

    template<typename T, typename U>
    status_t            unsafeReadTypedVector(std::vector<T>* val,
                                              status_t(Parcel::*read_func)(U*) const) const;
    template<typename T>
    status_t            readNullableTypedVector(std::unique_ptr<std::vector<T>>* val,
                                                status_t(Parcel::*read_func)(T*) const) const;
    template<typename T>
    status_t            readTypedVector(std::vector<T>* val,
                                        status_t(Parcel::*read_func)(T*) const) const;
    template<typename T, typename U>
    status_t            unsafeWriteTypedVector(const std::vector<T>& val,
                                               status_t(Parcel::*write_func)(U));
    template<typename T>
    status_t            writeNullableTypedVector(const std::unique_ptr<std::vector<T>>& val,
                                                 status_t(Parcel::*write_func)(const T&));
    template<typename T>
    status_t            writeNullableTypedVector(const std::unique_ptr<std::vector<T>>& val,
                                                 status_t(Parcel::*write_func)(T));
    template<typename T>
    status_t            writeTypedVector(const std::vector<T>& val,
                                         status_t(Parcel::*write_func)(const T&));
    template<typename T>
    status_t            writeTypedVector(const std::vector<T>& val,
                                         status_t(Parcel::*write_func)(T));

    status_t            mError;
    uint8_t*            mData;
    size_t              mDataSize;
    size_t              mDataCapacity;
    mutable size_t      mDataPos;
    binder_size_t*      mObjects;
    size_t              mObjectsSize;
    size_t              mObjectsCapacity;
    mutable size_t      mNextObjectHint;
    size_t              mNumRef;

    mutable bool        mFdsKnown;
    mutable bool        mHasFds;
    bool                mAllowFds;

    release_func        mOwner;
    void*               mOwnerCookie;

    class Blob {
    public:
        Blob();
        ~Blob();

        void clear();
        void release();
        inline size_t size() const { return mSize; }
        inline int fd() const { return mFd; }
        inline bool isMutable() const { return mMutable; }

    protected:
        void init(int fd, void* data, size_t size, bool isMutable);

        int mFd; // owned by parcel so not closed when released
        void* mData;
        size_t mSize;
        bool mMutable;
    };

public:
    class ReadableBlob : public Blob {
        friend class Parcel;
    public:
        inline const void* data() const { return mData; }
        inline void* mutableData() { return isMutable() ? mData : NULL; }
    };

    class WritableBlob : public Blob {
        friend class Parcel;
    public:
        inline void* data() { return mData; }
    };

private:
    size_t mOpenAshmemSize;

public:
    // TODO: Remove once ABI can be changed.
    size_t getBlobAshmemSize() const;
    size_t getOpenAshmemSize() const;
};

// ---------------------------------------------------------------------------

template<typename T>
status_t Parcel::readStrongBinder(sp<T>* val) const {
    sp<IBinder> tmp;
    status_t ret = readStrongBinder(&tmp);

    if (ret == OK) {
        *val = interface_cast<T>(tmp);

        if (val->get() == nullptr) {
            return UNKNOWN_ERROR;
        }
    }

    return ret;
}

template<typename T>
status_t Parcel::readNullableStrongBinder(sp<T>* val) const {
    sp<IBinder> tmp;
    status_t ret = readNullableStrongBinder(&tmp);

    if (ret == OK) {
        *val = interface_cast<T>(tmp);

        if (val->get() == nullptr) {
            return UNKNOWN_ERROR;
        }
    }
}

template<typename T, typename U>
status_t Parcel::unsafeReadTypedVector(
        std::vector<T>* val,
        status_t(Parcel::*read_func)(U*) const) const {
    int32_t size;
    status_t status = this->readInt32(&size);

    if (status != OK) {
        return status;
    }

    if (size < 0) {
        return UNEXPECTED_NULL;
    }

    val->resize(size);

    for (auto& v: *val) {
        status = (this->*read_func)(&v);

        if (status != OK) {
            return status;
        }
    }

    return OK;
}

template<typename T>
status_t Parcel::readTypedVector(std::vector<T>* val,
                                 status_t(Parcel::*read_func)(T*) const) const {
    return unsafeReadTypedVector(val, read_func);
}

template<typename T>
status_t Parcel::readNullableTypedVector(std::unique_ptr<std::vector<T>>* val,
                                         status_t(Parcel::*read_func)(T*) const) const {
    const size_t start = dataPosition();
    int32_t size;
    status_t status = readInt32(&size);
    val->reset();

    if (status != OK || size < 0) {
        return status;
    }

    setDataPosition(start);
    val->reset(new std::vector<T>());

    status = unsafeReadTypedVector(val->get(), read_func);

    if (status != OK) {
        val->reset();
    }

    return status;
}

template<typename T, typename U>
status_t Parcel::unsafeWriteTypedVector(const std::vector<T>& val,
                                        status_t(Parcel::*write_func)(U)) {
    if (val.size() > std::numeric_limits<int32_t>::max()) {
        return BAD_VALUE;
    }

    status_t status = this->writeInt32(val.size());

    if (status != OK) {
        return status;
    }

    for (const auto& item : val) {
        status = (this->*write_func)(item);

        if (status != OK) {
            return status;
        }
    }

    return OK;
}

template<typename T>
status_t Parcel::writeTypedVector(const std::vector<T>& val,
                                  status_t(Parcel::*write_func)(const T&)) {
    return unsafeWriteTypedVector(val, write_func);
}

template<typename T>
status_t Parcel::writeTypedVector(const std::vector<T>& val,
                                  status_t(Parcel::*write_func)(T)) {
    return unsafeWriteTypedVector(val, write_func);
}

template<typename T>
status_t Parcel::writeNullableTypedVector(const std::unique_ptr<std::vector<T>>& val,
                                          status_t(Parcel::*write_func)(const T&)) {
    if (val.get() == nullptr) {
        return this->writeInt32(-1);
    }

    return unsafeWriteTypedVector(*val, write_func);
}

template<typename T>
status_t Parcel::writeNullableTypedVector(const std::unique_ptr<std::vector<T>>& val,
                                          status_t(Parcel::*write_func)(T)) {
    if (val.get() == nullptr) {
        return this->writeInt32(-1);
    }

    return unsafeWriteTypedVector(*val, write_func);
}

// ---------------------------------------------------------------------------

inline TextOutput& operator<<(TextOutput& to, const Parcel& parcel)
{
    parcel.print(to);
    return to;
}

// ---------------------------------------------------------------------------

// Generic acquire and release of objects.
void acquire_object(const sp<ProcessState>& proc,
                    const flat_binder_object& obj, const void* who);
void release_object(const sp<ProcessState>& proc,
                    const flat_binder_object& obj, const void* who);

void flatten_binder(const sp<ProcessState>& proc,
                    const sp<IBinder>& binder, flat_binder_object* out);
void flatten_binder(const sp<ProcessState>& proc,
                    const wp<IBinder>& binder, flat_binder_object* out);
status_t unflatten_binder(const sp<ProcessState>& proc,
                          const flat_binder_object& flat, sp<IBinder>* out);
status_t unflatten_binder(const sp<ProcessState>& proc,
                          const flat_binder_object& flat, wp<IBinder>* out);

}; // namespace hardware
}; // namespace android

// ---------------------------------------------------------------------------

#endif // ANDROID_HARDWARE_PARCEL_H
