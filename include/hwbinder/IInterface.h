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

//
#ifndef ANDROID_HARDWARE_IINTERFACE_H
#define ANDROID_HARDWARE_IINTERFACE_H

#include <hwbinder/Binder.h>

namespace android {
namespace hardware {
// ----------------------------------------------------------------------

class IInterface : public virtual RefBase
{
public:
            IInterface();
            static sp<IBinder>  asBinder(const IInterface*);
            static sp<IBinder>  asBinder(const sp<IInterface>&);

protected:
    virtual                     ~IInterface();
    virtual IBinder*            onAsBinder() = 0;
};

// ----------------------------------------------------------------------

template<typename INTERFACE>
inline sp<INTERFACE> interface_cast(const sp<IBinder>& obj)
{
    return INTERFACE::asInterface(obj);
}

// ----------------------------------------------------------------------

template<typename INTERFACE, typename HWINTERFACE>
class BnInterface : public HWINTERFACE, public BBinder
{
public:
                                BnInterface(const sp<INTERFACE>& impl);
    virtual sp<IInterface>      queryLocalInterface(const String16& _descriptor);
    virtual const String16&     getInterfaceDescriptor() const;
protected:
    const sp<INTERFACE>         mImpl;
    virtual IBinder*            onAsBinder();
};

template<typename INTERFACE, typename HWINTERFACE>
inline BnInterface<INTERFACE, HWINTERFACE>::BnInterface(
        const sp<INTERFACE>& impl) : mImpl(impl)
{
}
// ----------------------------------------------------------------------

template<typename INTERFACE>
class BpInterface : public INTERFACE, public BpRefBase
{
public:
                                BpInterface(const sp<IBinder>& remote);

protected:
    virtual IBinder*            onAsBinder();
};

// ----------------------------------------------------------------------

#define DECLARE_HWBINDER_META_INTERFACE(INTERFACE)                          \
    static const ::android::String16 descriptor;                            \
    static ::android::sp<IHw##INTERFACE> asInterface(                       \
            const ::android::sp<::android::hardware::IBinder>& obj);        \
    virtual const ::android::String16& getInterfaceDescriptor() const;      \


#define IMPLEMENT_HWBINDER_META_INTERFACE(INTERFACE, NAME)                  \
    const ::android::String16 IHw##INTERFACE::descriptor(NAME);             \
    const ::android::String16&                                              \
            IHw##INTERFACE::getInterfaceDescriptor() const {                \
        return IHw##INTERFACE::descriptor;                                  \
    }                                                                       \
    ::android::sp<IHw##INTERFACE> IHw##INTERFACE::asInterface(              \
            const ::android::sp<::android::hardware::IBinder>& obj)         \
    {                                                                       \
        ::android::sp<IHw##INTERFACE> intr;                                 \
        if (obj != NULL) {                                                  \
            /* Check if local interface */                                  \
            intr = static_cast<IHw##INTERFACE*>(                            \
                obj->queryLocalInterface(                                   \
                        IHw##INTERFACE::descriptor).get());                 \
            if (intr == NULL) {                                             \
                intr = new Bp##INTERFACE(obj);                              \
            }                                                               \
        }                                                                   \
        return intr;                                                        \
    }

// ----------------------------------------------------------------------
// No user-serviceable parts after this...

template<typename INTERFACE, typename HWINTERFACE>
inline sp<IInterface> BnInterface<INTERFACE, HWINTERFACE>::queryLocalInterface(
        const String16& _descriptor)
{
    if (_descriptor == HWINTERFACE::descriptor) return this;
    return NULL;
}

template<typename INTERFACE, typename HWINTERFACE>
inline const String16& BnInterface<INTERFACE, HWINTERFACE>::getInterfaceDescriptor() const
{
    return HWINTERFACE::descriptor;
}

template<typename INTERFACE, typename HWINTERFACE>
IBinder* BnInterface<INTERFACE, HWINTERFACE>::onAsBinder()
{
    return this;
}

template<typename INTERFACE>
inline BpInterface<INTERFACE>::BpInterface(const sp<IBinder>& remote)
    : BpRefBase(remote)
{
}

template<typename INTERFACE>
inline IBinder* BpInterface<INTERFACE>::onAsBinder()
{
    return remote();
}

// ----------------------------------------------------------------------

}; // namespace hardware
}; // namespace android

#endif // ANDROID_HARDWARE_IINTERFACE_H
