#ifndef MOCK_TIMER_HANDLER_H__
#define MOCK_TIMER_HANDLER_H__

#include "timer_handler.h"

#include <gmock/gmock.h>

class MockTimerHandler : public TimerHandler
{
public:
  MOCK_METHOD1(add_timer, void(Timer*));
  MOCK_METHOD2(update_replica_tracker_for_timer, void(TimerID, int));
  MOCK_METHOD4(get_timers_for_node, HTTPCode(std::string request_node,
                                             int max_responses,
                                             std::string cluster_view_id,
                                             std::string& get_response));
};

#endif

