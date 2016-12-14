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

#ifndef HIDL_EVENTFLAG_H
#define HIDL_EVENTFLAG_H

#include <time.h>
#include <utils/Errors.h>
#include <atomic>

namespace android {
namespace hardware {

/**
 * EventFlag is an abstraction that application code utilizing FMQ can use to wait on
 * conditions like full, empty, data available etc. The same EventFlag object
 * can be used with multiple FMQs.
 */
struct EventFlag {
    /**
     * Create an event flag object with mapping information.
     *
     * @param fd File descriptor to be mmapped to create the event flag word.
     * There is no transfer of ownership of the fd. The caller will still
     * own the fd for the purpose of closing it.
     * @param offset Offset parameter to mmap.
     * @param ef Pointer to address of the EventFlag object that gets created. Will be set to
     * nullptr if unsuccesful.
     *
     * @return status Returns a status_t error code. Likely error codes are
     * NO_ERROR if the method is successful or BAD_VALUE due to invalid
     * mapping arguments.
     */
    static status_t createEventFlag(int fd, off_t offset, EventFlag** ef);

    /**
     * Create an event flag object from the address of the flag word.
     *
     * @param  efWordPtr Pointer to the event flag word.
     * @param status Returns a status_t error code. Likely error codes are
     * NO_ERROR if the method is successful or BAD_VALUE if efWordPtr is a null
     * pointer.
     * @param ef Pointer to the address of the EventFlag object that gets created. Will be set to
     * nullptr if unsuccesful.
     *
     * @return Returns a status_t error code. Likely error codes are
     * NO_ERROR if the method is successful or BAD_VALUE if efAddr is a null
     * pointer.
     *
     */
    static status_t createEventFlag(std::atomic<uint32_t>* efWordPtr,
                                    EventFlag** ef);

    /**
     * Delete an EventFlag object.
     *
     * @param ef A double pointer to the EventFlag object to be destroyed.
     *
     * @return Returns a status_t error code. Likely error codes are
     * NO_ERROR if the method is successful or BAD_VALUE due to
     * a bad input parameter.
     */
    static status_t deleteEventFlag(EventFlag** ef);

    /**
     * Set the specified bits of the event flag word here and wake up a thread.
     * @param bitmask The bits to be set on the event flag word.
     *
     * @return Returns a status_t error code. Likely error codes are
     * NO_ERROR if the method is successful or BAD_VALUE if the bit mask
     * does not have any bits set.
     */
    status_t wake(uint32_t bitmask);

    /**
     * Wait for any of the bits in the bit mask to be set.
     *
     * @param bitmask The bits to wait on.
     * @param timeout Specifies the absolute timeout for the wait according
     * to the CLOCK_MONOTONIC clock.
     * @param efState The event flag bits that caused the return from wake.
     *
     * @return Returns a status_t error code. Likely error codes are
     * NO_ERROR if the method is successful, BAD_VALUE due to bad input
     * parameters, TIMED_OUT if the wait timedout as per the timeout
     * parameter.
     */
    status_t wait(uint32_t bitmask, uint32_t* efState,
                  const struct timespec* timeout = nullptr);

private:
    bool mEfWordNeedsUnmapping = false;
    std::atomic<uint32_t>* mEfWordPtr = nullptr;

    /*
     * mmap memory for the event flag word.
     */
    EventFlag(int fd, off_t offset, status_t* status);

    /*
     * Use this constructor if we already know where the event flag word
     * lives.
     */
    EventFlag(std::atomic<uint32_t>* efWordPtr, status_t* status);

    /*
     * Disallow constructor without argument and copying.
     */
    EventFlag();
    EventFlag& operator=(const EventFlag& other) = delete;
    EventFlag(const EventFlag& other) = delete;

    /*
     * Utility method to unmap the event flag word.
     */
    static status_t unmapEventFlagWord(std::atomic<uint32_t>* efWordPtr,
                                       bool* efWordNeedsUnmapping);
    ~EventFlag();
};
}  // namespace hardware
}  // namespace android
#endif
