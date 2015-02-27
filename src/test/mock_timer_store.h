#ifndef MOCK_TIMER_STORE_H__
#define MOCK_TIMER_STORE_H__

#include "timer_store.h"

#include "gmock/gmock.h"

class MockTimerStore : public TimerStore
{
public:
  MockTimerStore(): TimerStore(NULL) {};
  ~MockTimerStore() {};
  MOCK_METHOD1(add_timer, void(Timer*));
  MOCK_METHOD1(add_timers, void(std::unordered_set<Timer*>&));
  MOCK_METHOD1(delete_timer, void(TimerID));
  MOCK_METHOD1(get_next_timers, void(std::unordered_set<Timer*>&));
};

#endif
