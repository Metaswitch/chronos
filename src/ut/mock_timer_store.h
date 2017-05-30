/**
 * @file mock_timer_store.h
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCK_TIMER_STORE_H__
#define MOCK_TIMER_STORE_H__

#include "timer_store.h"
#include "httpconnection.h"
#include "gmock/gmock.h"

class MockTimerStore : public TimerStore
{
public:
  MockTimerStore(): TimerStore(NULL) {};
  ~MockTimerStore() {};
  MOCK_METHOD1(insert, void(Timer*));
  MOCK_METHOD2(fetch, void(TimerID, Timer**));
  MOCK_METHOD1(fetch_next_timers, void(std::unordered_set<Timer*>&));
  MOCK_METHOD3(get_by_not_view_id, bool(std::string, int, std::unordered_set<Timer*>&));
};

#endif
