// Copyright (c) 2026 LuneOS
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

/* Tests for the wake / no-wake split of the schedule manager: only
 * schedules with wake=true (the default) may arm the RTC wakeup timeout,
 * everything else goes to the plain (wakeup=false) timer. */

#include <gtest/gtest.h>
#include <glib.h>

#include <vector>

#include "activity/Activity.h"
#include "activity/schedule/AbstractScheduleManager.h"
#include "activity/schedule/Schedule.h"
#include "conf/ActivityJson.h"
#include "util/MojoObjectJson.h"

namespace {

struct TimeoutCall {
    bool set;
    time_t at;
    bool wake;
};

class TestScheduleManager: public AbstractScheduleManager {
public:
    std::vector<TimeoutCall> calls;

    virtual void enable() {}

    void fireCallback() { wake(); }

protected:
    virtual void updateTimeout(time_t nextWakeup, time_t curTime, bool wake)
    {
        TimeoutCall c = { true, nextWakeup, wake };
        calls.push_back(c);
    }

    virtual void cancelTimeout(bool wake)
    {
        TimeoutCall c = { false, 0, wake };
        calls.push_back(c);
    }
};

/* addItem defers the timeout update to the main loop */
void pump()
{
    while (g_main_context_iteration(NULL, FALSE)) {
    }
}

class UnittestScheduleManager: public testing::Test {
protected:
    UnittestScheduleManager()
        : now(time(NULL))
    {
    }

    std::shared_ptr<Schedule> makeSchedule(activityId_t id, time_t start, bool wake)
    {
        std::shared_ptr<Activity> activity = std::make_shared<Activity>(id);
        activities.push_back(activity);
        std::shared_ptr<Schedule> schedule = std::make_shared<Schedule>(activity, start);
        schedule->setWake(wake);
        /* The queue hooks are auto-unlink: keep the Schedule alive the way
         * its Activity would in the daemon. */
        schedules.push_back(schedule);
        return schedule;
    }

    size_t count(bool set, bool wake) const
    {
        size_t n = 0;
        for (size_t i = 0; i < mgr.calls.size(); i++) {
            if (mgr.calls[i].set == set && mgr.calls[i].wake == wake) {
                n++;
            }
        }
        return n;
    }

    const TimeoutCall *last(bool wake) const
    {
        for (size_t i = mgr.calls.size(); i > 0; i--) {
            if (mgr.calls[i - 1].wake == wake) {
                return &mgr.calls[i - 1];
            }
        }
        return NULL;
    }

    time_t now;
    TestScheduleManager mgr;
    std::vector<std::shared_ptr<Activity>> activities;
    std::vector<std::shared_ptr<Schedule>> schedules;
};

} // namespace

TEST_F(UnittestScheduleManager, ScheduleDefaultsToWake)
{
    std::shared_ptr<Activity> activity = std::make_shared<Activity>(1);
    Schedule schedule(activity, now + 60);
    EXPECT_TRUE(schedule.requiresWake());
}

TEST_F(UnittestScheduleManager, WakeScheduleArmsWakeupOnly)
{
    mgr.addItem(makeSchedule(1, now + 300, true));
    pump();

    ASSERT_EQ(1u, mgr.calls.size());
    EXPECT_TRUE(mgr.calls[0].set);
    EXPECT_TRUE(mgr.calls[0].wake);
    EXPECT_EQ(now + 300, mgr.calls[0].at);
}

TEST_F(UnittestScheduleManager, NoWakeScheduleNeverArmsWakeup)
{
    mgr.addItem(makeSchedule(1, now + 60, false));
    pump();

    ASSERT_EQ(1u, mgr.calls.size());
    EXPECT_TRUE(mgr.calls[0].set);
    EXPECT_FALSE(mgr.calls[0].wake);
    EXPECT_EQ(now + 60, mgr.calls[0].at);
    EXPECT_EQ(0u, count(true, true));
}

TEST_F(UnittestScheduleManager, EarlierNoWakeItemDoesNotMoveWakeup)
{
    mgr.addItem(makeSchedule(1, now + 300, true));
    pump();
    mgr.addItem(makeSchedule(2, now + 60, false));
    pump();

    /* One set per class, the wake one still at +300 */
    EXPECT_EQ(1u, count(true, true));
    EXPECT_EQ(1u, count(true, false));
    ASSERT_TRUE(last(true) != NULL);
    EXPECT_EQ(now + 300, last(true)->at);
    ASSERT_TRUE(last(false) != NULL);
    EXPECT_EQ(now + 60, last(false)->at);
}

TEST_F(UnittestScheduleManager, LaterWakeItemDoesNotRearm)
{
    mgr.addItem(makeSchedule(1, now + 300, true));
    pump();
    mgr.addItem(makeSchedule(2, now + 600, true));
    pump();

    EXPECT_EQ(1u, count(true, true));
    EXPECT_EQ(0u, count(true, false));
}

TEST_F(UnittestScheduleManager, RemovingLastWakeItemCancelsWakeupOnly)
{
    std::shared_ptr<Schedule> wakeItem = makeSchedule(1, now + 300, true);
    mgr.addItem(wakeItem);
    mgr.addItem(makeSchedule(2, now + 60, false));
    pump();
    mgr.calls.clear();

    mgr.removeItem(wakeItem);

    ASSERT_EQ(1u, mgr.calls.size());
    EXPECT_FALSE(mgr.calls[0].set);
    EXPECT_TRUE(mgr.calls[0].wake);
}

TEST_F(UnittestScheduleManager, CallbackRearmsBothClasses)
{
    mgr.addItem(makeSchedule(1, now + 300, true));
    mgr.addItem(makeSchedule(2, now + 60, false));
    pump();
    mgr.calls.clear();

    mgr.fireCallback();

    EXPECT_EQ(1u, count(true, true));
    EXPECT_EQ(1u, count(true, false));
}

TEST_F(UnittestScheduleManager, WakeFlagPersistsInJson)
{
    std::shared_ptr<Activity> activity = std::make_shared<Activity>(1);

    Schedule wakeSchedule(activity, now + 60);
    MojObject rep;
    ASSERT_EQ(MojErrNone, wakeSchedule.toJson(rep, ACTIVITY_JSON_PERSIST));
    bool wake = true;
    EXPECT_FALSE(rep.get(_T("wake"), wake));

    Schedule noWakeSchedule(activity, now + 60);
    noWakeSchedule.setWake(false);
    MojObject rep2;
    ASSERT_EQ(MojErrNone, noWakeSchedule.toJson(rep2, ACTIVITY_JSON_PERSIST));
    ASSERT_TRUE(rep2.get(_T("wake"), wake));
    EXPECT_FALSE(wake);
}
