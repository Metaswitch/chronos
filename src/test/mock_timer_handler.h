#ifndef MOCK_TIMER_HANDLER_H__
#define MOCK_TIMER_HANDLER_H__

#include "timer_handler.h"

#include <gmock/gmock.h>

class MockTimerHandler : public TimerHandler
{
public:
  MOCK_METHOD1(add_timer, void(Timer*));
};

#endif

