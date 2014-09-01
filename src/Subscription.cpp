// @@@LICENSE
//
//      Copyright (c) 2009-2013 LG Electronics, Inc.
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
// LICENSE@@@

#include "Activity.h"
#include "Subscription.h"
#include "Subscriber.h"
#include "Logging.h"
#include <stdexcept>

MojLogger Subscription::s_log(_T("activitymanager.subscription"));

Subscription::Subscription(boost::shared_ptr<Activity> activity,
	bool detailedUpdates)
	: m_detailedEvents(detailedUpdates)
	, m_plugged(false)
	, m_activity(activity)
{
}

Subscription::~Subscription()
{
}

void Subscription::HandleCancelWrapper()
{
	LOG_AM_TRACE("Entering function %s", __FUNCTION__);
	LOG_AM_DEBUG("%s cancelling subscription",
		GetSubscriber().GetString().c_str());

	try {
		m_activity->RemoveSubscription(shared_from_this());

		/* Any specialized implementation for the specific Subscription type */
		HandleCancel();
	} catch (const std::exception& except) {
		LOG_AM_ERROR(MSGID_SUBSCRIPTION_CANCEL_FAIL, 1,
			PMLOGKS("SUBSCRIBER",GetSubscriber().GetString().c_str() ),
			"Unhandled exception \"%s\" occurred while cancelling subscription",
			except.what());
	} catch (...) {
		LOG_AM_ERROR(MSGID_SUBSCRIPTION_CANCEL_ERR,1,
			PMLOGKS("SUBSCRIBER",GetSubscriber().GetString().c_str() ),
			"Unhandled exception of unknown type occurred cancelling subscription");
	}
}

bool Subscription::operator<(const Subscription& rhs) const
{
	return (this < &rhs);
}

MojErr Subscription::QueueEvent(ActivityEvent_t event)
{
	LOG_AM_TRACE("Entering function %s", __FUNCTION__);

	/* Non-detailed subscriptions do not get update events */
	if ((event == ActivityUpdateEvent) && !m_detailedEvents) {
		return MojErrNone;
	}

	if (!IsPlugged()) {
		return SendEvent(event);
	} else {
		/* If there's already an event queued, suppress this one.
		 * Current Activity details will be pulled when the event is
		 * *sent* */
		if ((event == ActivityUpdateEvent) && !m_eventQueue.empty()) {
			return MojErrNone;
		}

		m_eventQueue.push_back(event);
		return MojErrNone;
	}
}

void Subscription::Plug()
{
	LOG_AM_TRACE("Entering function %s", __FUNCTION__);

	m_plugged = true;
}

void Subscription::Unplug()
{
	LOG_AM_TRACE("Entering function %s", __FUNCTION__);

	m_plugged = false;

	/* Don't issue any events if there are persist commands waiting */
	if (m_activity->IsPersistCommandHooked())
		return;

	while (!m_eventQueue.empty()) {
		ActivityEvent_t event = m_eventQueue.front();
		m_eventQueue.pop_front();
		SendEvent(event);
	}
}

bool Subscription::IsPlugged() const
{
	return (m_plugged || m_activity->IsPersistCommandHooked());
}

