/**
 * @file mock_timer_handler.h
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef MOCK_TIMER_HANDLER_H__
#define MOCK_TIMER_HANDLER_H__

#include "timer_handler.h"

#include <gmock/gmock.h>

class MockTimerHandler : public TimerHandler
{
public:
  MOCK_METHOD2(add_timer,void(Timer*,bool));
  MOCK_METHOD1(return_timer,void(Timer*));
  MOCK_METHOD1(handle_successful_callback,void(TimerID));
  MOCK_METHOD1(handle_failed_callback,void(TimerID));
  MOCK_METHOD5(get_timers_for_node, HTTPCode(std::string request_node,
                                             int max_responses,
                                             std::string cluster_view_id,
                                             uint32_t time_from,
                                             std::string& get_response));
};

#endif

