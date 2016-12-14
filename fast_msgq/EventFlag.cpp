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

#include "EventFlag.h"
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <utils/Log.h>
#include <new>

namespace android {
namespace hardware {

status_t EventFlag::createEventFlag(int fd, off_t offset, EventFlag** flag) {
    if (flag == nullptr) {
        return BAD_VALUE;
    }

    status_t status = NO_MEMORY;
    *flag = nullptr;

    EventFlag* evFlag = new (std::nothrow) EventFlag(fd, offset, &status);
    if (evFlag != nullptr) {
        if (status == NO_ERROR) {
            *flag = evFlag;
        } else {
            delete evFlag;
        }
    }

    return status;
}

status_t EventFlag::createEventFlag(std::atomic<uint32_t>* fwAddr,
                                    EventFlag** flag) {
    if (flag == nullptr) {
        return BAD_VALUE;
    }

    status_t status = NO_MEMORY;
    *flag  = nullptr;

    EventFlag* evFlag = new (std::nothrow) EventFlag(fwAddr, &status);
    if (evFlag != nullptr) {
        if (status == NO_ERROR) {
            *flag = evFlag;
        } else {
            delete evFlag;
        }
    }

    return status;
}

/*
 * mmap memory for the futex word
 */
EventFlag::EventFlag(int fd, off_t offset, status_t* status) {
    mEfWordPtr = static_cast<std::atomic<uint32_t>*>(mmap(NULL,
                                                  sizeof(std::atomic<uint32_t>),
                                                  PROT_READ | PROT_WRITE,
                                                  MAP_SHARED, fd, offset));
    mEfWordNeedsUnmapping = true;
    if (mEfWordPtr != MAP_FAILED) {
        *status = NO_ERROR;
    } else {
        *status = -errno;
        ALOGE("Attempt to mmap event flag word failed: %s\n", strerror(errno));
    }
}

/*
 * Use this constructor if we already know where the futex word for
 * the EventFlag group lives.
 */
EventFlag::EventFlag(std::atomic<uint32_t>* fwAddr, status_t* status) {
    *status = NO_ERROR;
    if (fwAddr == nullptr) {
        *status = BAD_VALUE;
    } else {
        mEfWordPtr = fwAddr;
    }
}

/*
 * Set the specified bits of the futex word here and wake up any
 * thread waiting on any of the bits.
 */
status_t EventFlag::wake(uint32_t bitmask) {
    /*
     * Return early if there are no set bits in bitmask.
     */
    if (bitmask == 0) {
        return NO_ERROR;
    }

    status_t status = NO_ERROR;
    uint32_t old = std::atomic_fetch_or(mEfWordPtr, bitmask);
    /*
     * No need to call FUTEX_WAKE_BITSET if there is a deferred wake
     * already available for any bit in the bitmask.
     */
    if ((~old & bitmask) != 0) {
        int ret = syscall(__NR_futex, mEfWordPtr, FUTEX_WAKE_BITSET,
                           INT_MAX, NULL, NULL, bitmask);
        if (ret == -1) {
            status = -errno;
            ALOGE("Error in event flag wake attempt: %s\n", strerror(errno));
        }
    }
    return status;
}

/*
 * Wait for any of the bits in the bitmask to be set
 * and return which bits caused the return.
 */
status_t EventFlag::wait(uint32_t bitmask, uint32_t* efState, const struct timespec* timeout) {
    /*
     * Return early if there are no set bits in bitmask.
     */
    if (bitmask == 0 || efState == nullptr) {
        return BAD_VALUE;
    }

    status_t status = NO_ERROR;
    uint32_t old = std::atomic_fetch_and(mEfWordPtr, ~bitmask);
    uint32_t setBits = old & bitmask;
    /*
     * If there was a deferred wake available, no need to call FUTEX_WAIT_BITSET.
     */
    if (setBits != 0) {
        *efState = setBits;
        return status;
    }

    uint32_t efWord = old & ~bitmask;
    /*
     * The syscall will put the thread to sleep only
     * if the futex word still contains the expected
     * value i.e. efWord. If the futex word contents have
     * changed, it fails with the error EAGAIN.
     */
    int ret = syscall(__NR_futex, mEfWordPtr, FUTEX_WAIT_BITSET, efWord, timeout, NULL, bitmask);
    if (ret == -1) {
        status = -errno;
        if (status != -EAGAIN) {
            ALOGE("Event flag wait was unsuccessful: %s\n", strerror(errno));
        }
        *efState = 0;
    } else {
        *efState = efWord & bitmask;
    }
    return status;
}

status_t EventFlag::unmapEventFlagWord(std::atomic<uint32_t>* efWordPtr,
                                       bool* efWordNeedsUnmapping) {
    status_t status = NO_ERROR;
    if (*efWordNeedsUnmapping) {
        int ret = munmap(efWordPtr, sizeof(std::atomic<uint32_t>));
        if (ret != 0) {
            status = -errno;
            ALOGE("Error in deleting event flag group: %s\n", strerror(errno));
        }
        *efWordNeedsUnmapping = false;
    }
    return status;
}

status_t EventFlag::deleteEventFlag(EventFlag** evFlag) {
    if (evFlag == nullptr || *evFlag == nullptr) {
        return BAD_VALUE;
    }

    status_t status = unmapEventFlagWord((*evFlag)->mEfWordPtr,
                                         &(*evFlag)->mEfWordNeedsUnmapping);
    delete *evFlag;
    *evFlag = nullptr;

    return status;
}

EventFlag::~EventFlag() {
    unmapEventFlagWord(mEfWordPtr, &mEfWordNeedsUnmapping);
}

}  // namespace hardware
}  // namespace android
