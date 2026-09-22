// Copyright (c) 2009-2018 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "Main.h"
#include "Schedule.h"

class AbstractScheduleManager {
public:
    AbstractScheduleManager();
    virtual ~AbstractScheduleManager();

    void addItem(std::shared_ptr<Schedule> item);
    void removeItem(std::shared_ptr<Schedule> item);

    static std::string timeToString(time_t convert, bool isUTC);
    static time_t stringToTime(const char *convert, bool& isUTC);

    void setLocalOffset(off_t offset);
    off_t getLocalOffset() const;

    time_t getSmartBaseTime() const;

    virtual void enable() = 0;

protected:
    /* Arm (or re-arm) the timeout for the given class of schedules.
     * wake=true: the device must be woken from suspend at nextWakeup.
     * wake=false: fire at nextWakeup if awake, otherwise on next resume. */
    virtual void updateTimeout(time_t nextWakeup, time_t curTime, bool wake) = 0;
    virtual void cancelTimeout(bool wake) = 0;

    typedef boost::intrusive::member_hook<Schedule, Schedule::QueueItem,
            &Schedule::m_queueItem> ScheduleQueueOption;
    typedef boost::intrusive::multiset<Schedule, ScheduleQueueOption,
            boost::intrusive::constant_time_size<false>> ScheduleQueue;

    void wake();
    static gboolean dequeueAndUpdateTimeout(gpointer data);
    void dequeueAndUpdateTimeout();
    void processQueue(ScheduleQueue& queue, time_t curTime);
    void reQueue(ScheduleQueue& queue);

    void timeChanged();

    /* Earliest absolute start time of the queued schedules whose
     * requiresWake() equals 'wake'.  Returns false if there is none. */
    bool getNextStartTime(bool wake, time_t& next) const;
    bool isClassHead(const ScheduleQueue& queue, const Schedule& item) const;
    void updateTimeoutForClass(bool wake, time_t curTime);

    static MojLogger s_log;

    /* Priority queues of tasks, in order, sorted by next run time.
     * Two queues, one in absolute time, and one in local time. */
    ScheduleQueue m_queue;
    ScheduleQueue m_localQueue;

    /* One sleepd timeout per class: [1] = wake schedules (RTC alarm),
     * [0] = no-wake schedules (plain timer, fires on resume if missed). */
    time_t m_nextWakeup[2];
    bool m_wakeScheduled[2];

    bool m_localOffsetSet;
    off_t m_localOffset;

    time_t m_smartBase;
};

#endif /* _SCHEDULER_H_ */
