#ifndef FAKE_TIMER_HANDLER_H__
#define FAKE_TIMER_HANDLER_H__

#include "timer_handler.h"

#include <gmock/gmock.h>

class FakeTimerHandler : public TimerHandler
{
public:
  void add_timer(Timer* t) { delete t; t = NULL; }
};

#endif

