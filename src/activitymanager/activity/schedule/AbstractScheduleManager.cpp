// Copyright (c) 2009-2022 LG Electronics, Inc.
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

#include <activity/schedule/AbstractScheduleManager.h>
#include <stdexcept>
#include <cstdlib>

#include "activity/Activity.h"
#include "util/Logging.h"

MojLogger AbstractScheduleManager::s_log(_T("activitymanager.scheduler"));

AbstractScheduleManager::AbstractScheduleManager()
        : m_localOffsetSet(false)
        , m_localOffset(0)
{
    m_nextWakeup[0] = m_nextWakeup[1] = 0;
    m_wakeScheduled[0] = m_wakeScheduled[1] = false;

    /* Calculate a random base start time between 11pm and 5am so all
     * the devices don't cause a storm of syncs if their midnights are
     * aligned.  (Generally, local time should be used for that sort of thing,
     * so at least they'll be distributed globally.  Also, technically,
     * an hour spread would be ok, but there are longitudes with less
     * subscribers, so spreading the more populated neighbors farther will
     * still help.) */
    srandom(time(0));
    m_smartBase = (23 * 60 * 60) + (random() % (6 * 60 * 60));
}

AbstractScheduleManager::~AbstractScheduleManager()
{
}

void AbstractScheduleManager::addItem(std::shared_ptr<Schedule> item)
{
    bool updateWake = false;

    LOG_AM_TRACE("Entering function %s", __FUNCTION__);

    LOG_AM_DEBUG(
            "Adding [Activity %llu] to start at %llu (%s)",
            item->getActivity()->getId(),
            (unsigned long long )item->getNextStartTime(),
            timeToString(item->getNextStartTime(), !item->isLocal()).c_str());

    if (item->isLocal()) {
        m_localQueue.insert(*item);
        updateWake = isClassHead(m_localQueue, *item);
    } else {
        m_queue.insert(*item);
        updateWake = isClassHead(m_queue, *item);
    }

    if (updateWake) {
        g_timeout_add(0, dequeueAndUpdateTimeout, this);
    }
}

/* True if no earlier item of the same wake class precedes 'item' in
 * 'queue', i.e. adding/removing it may move that class' timeout. */
bool AbstractScheduleManager::isClassHead(const ScheduleQueue& queue,
                                          const Schedule& item) const
{
    for (ScheduleQueue::const_iterator it = queue.begin(); it != queue.end(); ++it) {
        if (&(*it) == &item) {
            return true;
        }
        if (it->requiresWake() == item.requiresWake()) {
            return false;
        }
    }
    return false;
}

void AbstractScheduleManager::removeItem(std::shared_ptr<Schedule> item)
{
    bool updateWake = false;

    LOG_AM_TRACE("Entering function %s", __FUNCTION__);

    LOG_AM_DEBUG("Removing [Activity %llu]", item->getActivity()->getId());

    try {
        /* Do NOT attempt to get an iterator to an item that isn't in a
         * container. */
        if (item->m_queueItem.is_linked()) {

            /* If the item is at the head of its wake class in either queue,
             * that class' time might have changed.  Otherwise, it
             * definitely didn't. */
            if (item->isLocal()) {
                updateWake = isClassHead(m_localQueue, *item);
            } else {
                updateWake = isClassHead(m_queue, *item);
            }

            item->m_queueItem.unlink();

            if (updateWake) {
                dequeueAndUpdateTimeout();
            }
        }
    } catch (...) {
    }
}

std::string AbstractScheduleManager::timeToString(time_t convert, bool isUTC)
{
    char buf[32];
    struct tm tm;

    memset(&tm, 0, sizeof(struct tm));
    gmtime_r(&convert, &tm);

    size_t len = strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    if (isUTC) {
        buf[len] = 'Z';
        buf[len + 1] = '\0';
    }

    return std::string(buf);
}

time_t AbstractScheduleManager::stringToTime(const char *convert, bool& isUTC)
{
    struct tm tm;
    memset(&tm, 0, sizeof(struct tm));

    char *next = strptime(convert, "%Y-%m-%d %H:%M:%S", &tm);
    if (!next) {
        throw std::runtime_error("Failed to parse start time");
    }

    if (*next == 'Z') {
        isUTC = true;
    } else if (*next == '\0') {
        isUTC = false;
    } else {
        throw std::runtime_error("Start time must end in 'Z' for UTC, or nothing");
    }

    /* mktime performs conversions assuming the time is in the local timezone.
     * timezone must be set for UTC for this to behave properly. */
    return mktime(&tm);
}

void AbstractScheduleManager::setLocalOffset(off_t offset)
{
    LOG_AM_TRACE("Entering function %s", __FUNCTION__);
    LOG_AM_DEBUG("Setting local offset to %lld", (long long )offset);

    bool updateWake = false;

    if (!m_localOffsetSet) {
        m_localOffset = offset;
        m_localOffsetSet = true;
        updateWake = true;
    } else if (m_localOffset != offset) {
        m_localOffset = offset;
        updateWake = true;
    }

    if (updateWake) {
        dequeueAndUpdateTimeout();
    }
}

off_t AbstractScheduleManager::getLocalOffset() const
{
    if (!m_localOffsetSet) {
        LOG_AM_WARNING(MSGID_SCHE_OFFSET_NOTSET, 0,
                       "Attempt to access local offset before it has been set");
    }

    return m_localOffset;
}

time_t AbstractScheduleManager::getSmartBaseTime() const
{
    return m_smartBase;
}

void AbstractScheduleManager::wake()
{
    LOG_AM_TRACE("Entering function %s", __FUNCTION__);
    LOG_AM_DEBUG("Wake callback");

    /* sleepd does not tell us which of our two timeouts fired and it has
     * dropped that one, so re-arm both unconditionally. */
    m_wakeScheduled[0] = false;
    m_wakeScheduled[1] = false;

    dequeueAndUpdateTimeout();
}

gboolean AbstractScheduleManager::dequeueAndUpdateTimeout(gpointer data)
{
    AbstractScheduleManager* self = static_cast<AbstractScheduleManager*>(data);
    if (!self) {
        return G_SOURCE_REMOVE;
    }

    self->dequeueAndUpdateTimeout();
    return G_SOURCE_REMOVE;
}

/* XXX Handle timer rollover? */
void AbstractScheduleManager::dequeueAndUpdateTimeout()
{
    LOG_AM_TRACE("Entering function %s", __FUNCTION__);

    /* Nothing to do?  Then return.  A new timeout will be scheduled
     * the next time something is queued */
    if (m_queue.empty() && (!m_localOffsetSet || m_localQueue.empty())) {
        LOG_AM_DEBUG("Not dequeuing any items as queue is now empty");
        for (int wake = 0; wake < 2; wake++) {
            if (m_wakeScheduled[wake]) {
                cancelTimeout(wake != 0);
                m_wakeScheduled[wake] = false;
            }
        }
        return;
    }

    time_t curTime = time(NULL);

    LOG_AM_DEBUG("Beginning to dequeue items at time %llu",(unsigned long long) curTime);

    /* If anything on the queue already happened in the past, dequeue it
     * and mark it as Scheduled(). */

    processQueue(m_queue, curTime);

    /* Only process the local queue if the timezone offset is known.
     * Otherwise, wait, because it will be known shortly. */
    if (m_localOffsetSet) {
        processQueue(m_localQueue, curTime + m_localOffset);
    }

    LOG_AM_DEBUG("Done dequeuing items");

    /* Arm the RTC wakeup only for schedules that asked for it; everything
     * else gets a plain timer that fires when the device is awake anyway. */
    updateTimeoutForClass(true, curTime);
    updateTimeoutForClass(false, curTime);
}

void AbstractScheduleManager::updateTimeoutForClass(bool wake, time_t curTime)
{
    const int idx = wake ? 1 : 0;
    time_t next;

    if (!getNextStartTime(wake, next)) {
        LOG_AM_DEBUG("No unscheduled %s items remain", wake ? "wake" : "no-wake");

        if (m_wakeScheduled[idx]) {
            cancelTimeout(wake);
            m_wakeScheduled[idx] = false;
        }

        return;
    }

    if (!m_wakeScheduled[idx] || (next != m_nextWakeup[idx])) {
        updateTimeout(next, curTime, wake);
        m_nextWakeup[idx] = next;
        m_wakeScheduled[idx] = true;
    }
}

void AbstractScheduleManager::processQueue(ScheduleQueue& queue, time_t curTime)
{
    while (!queue.empty()) {
        Schedule& item = *(queue.begin());

        if (item.getNextStartTime() <= curTime) {
            item.m_queueItem.unlink();
            item.scheduled();
        } else {
            break;
        }
    }
}

void AbstractScheduleManager::reQueue(ScheduleQueue& queue)
{
    LOG_AM_TRACE("Entering function %s", __FUNCTION__);
    LOG_AM_DEBUG("Requeuing");

    ScheduleQueue updated;

    while (!queue.empty()) {
        Schedule& item = *(queue.begin());

        item.m_queueItem.unlink();
        item.calcNextStartTime();
        updated.insert(item);
    }

    updated.swap(queue);
}

void AbstractScheduleManager::timeChanged()
{
    LOG_AM_TRACE("Entering function %s", __FUNCTION__);
    LOG_AM_DEBUG(
            "System time or timezone changed, recomputing start times and requeuing Scheduled Activities");

    reQueue(m_queue);
    reQueue(m_localQueue);

    dequeueAndUpdateTimeout();
}

bool AbstractScheduleManager::getNextStartTime(bool wake, time_t& next) const
{
    bool found = false;

    /* Queues are sorted by start time, so the first item of the requested
     * class is that class' earliest. */
    for (ScheduleQueue::const_iterator it = m_queue.begin(); it != m_queue.end(); ++it) {
        if (it->requiresWake() == wake) {
            next = it->getNextStartTime();
            found = true;
            break;
        }
    }

    /* Only consider the local queue if the timezone offset is known.
     * Otherwise, wait, because it will be known shortly. */
    if (m_localOffsetSet) {
        for (ScheduleQueue::const_iterator it = m_localQueue.begin(); it != m_localQueue.end(); ++it) {
            if (it->requiresWake() == wake) {
                time_t nextLocal = it->getNextStartTime() - m_localOffset;
                if (!found || (nextLocal < next)) {
                    next = nextLocal;
                    found = true;
                }
                break;
            }
        }
    }

    return found;
}

