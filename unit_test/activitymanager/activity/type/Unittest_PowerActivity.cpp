// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
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

/*
 * A running Activity that is cancelled or replaced is destroyed without
 * passing through endActivity(). Its power lock must still be dropped, or the
 * sleep service keeps the device awake until the lock expires.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "activity/Activity.h"
#include "activity/state/AbstractActivityState.h"
#include "activity/type/PowerActivity.h"
#include "activity/type/PowerManager.h"

namespace {

/* Records what would be sent to the sleep service instead of using the bus. */
class TestPowerActivity: public PowerActivity {
public:
    explicit TestPowerActivity(const std::shared_ptr<Activity>& activity)
        : PowerActivity(activity, 1)
    {
    }

    std::vector<std::string> sent;

    void markLocked()
    {
        m_currentState = kPowerLocked;
        m_targetState = kPowerLocked;
    }

protected:
    virtual MojErr createRemotePowerActivity(bool debounce)
    {
        sent.push_back(debounce ? "start debounce" : "start");
        return MojErrNone;
    }

    virtual MojErr removeRemotePowerActivity(bool notifyActivity)
    {
        sent.push_back(notifyActivity ? "end" : "end silently");
        return MojErrNone;
    }
};

class PowerActivityRelease: public ::testing::Test {
protected:
    virtual void SetUp()
    {
        activity = std::make_shared<Activity>(nextId++);
        power = std::make_shared<TestPowerActivity>(activity);
        activity->setPowerActivity(power);
    }

    static activityId_t nextId;
    std::shared_ptr<Activity> activity;
    std::shared_ptr<TestPowerActivity> power;
};

activityId_t PowerActivityRelease::nextId = 90000;

bool listsPoweredActivities()
{
    MojObject rep;
    PowerManager::getInstance().infoToJson(rep);
    return rep.contains(_T("poweredActivities"));
}

}

TEST_F(PowerActivityRelease, dropsAHeldLockWithoutTellingTheActivity)
{
    power->markLocked();

    power->release();

    ASSERT_EQ(1u, power->sent.size());
    EXPECT_EQ("end silently", power->sent[0]);
    EXPECT_EQ(AbstractPowerActivity::kPowerUnlocked, power->getPowerState());
}

TEST_F(PowerActivityRelease, dropsALockThatIsStillBeingTaken)
{
    power->begin();

    power->release();

    ASSERT_EQ(2u, power->sent.size());
    EXPECT_EQ("start", power->sent[0]);
    EXPECT_EQ("end silently", power->sent[1]);
}

TEST_F(PowerActivityRelease, sendsNothingWithoutALock)
{
    power->release();

    EXPECT_TRUE(power->sent.empty());
    EXPECT_EQ(AbstractPowerActivity::kPowerUnlocked, power->getPowerState());
}

TEST_F(PowerActivityRelease, leavesAnUnlockOnItsWayAlone)
{
    power->markLocked();
    power->end();

    power->release();

    ASSERT_EQ(1u, power->sent.size());
    EXPECT_EQ("end", power->sent[0]);
}

TEST_F(PowerActivityRelease, sendsTheRemovalOnce)
{
    power->markLocked();

    power->release();
    power->release();

    EXPECT_EQ(1u, power->sent.size());
}

TEST_F(PowerActivityRelease, powerManagerForgetsTheReleasedActivity)
{
    PowerManager::getInstance().requestBeginPowerActivity(activity);
    ASSERT_TRUE(listsPoweredActivities());

    PowerManager::getInstance().releasePowerActivity(activity);

    EXPECT_FALSE(listsPoweredActivities());
    ASSERT_EQ(2u, power->sent.size());
    EXPECT_EQ("end silently", power->sent[1]);
}

TEST_F(PowerActivityRelease, destroyingARunningActivityDropsItsLock)
{
    power->markLocked();

    ActivityStateDestroyed::newInstance()->enter(activity);

    ASSERT_EQ(1u, power->sent.size());
    EXPECT_EQ("end silently", power->sent[0]);
    EXPECT_EQ(AbstractPowerActivity::kPowerUnlocked, power->getPowerState());
}
