#ifndef TIMER_STORE_H__
#define TIMER_STORE_H__

#include "timer.h"

#include <unordered_set>
#include <map>
#include <string>

#define NUM_SECOND_BUCKETS 3600

class TimerStore
{
public:
  TimerStore();
  ~TimerStore();

  // Add a timer to the store.
  void add_timer(Timer *);

  // Remove a timer by ID from the store.
  void delete_timer(TimerID timer_id);

  // Get the next bucket of timers to pop.
  void get_next_timers(std::unordered_set<Timer*>&);

private:

  // A table of all known timers
  std::map<TimerID, Timer *> _timer_lookup_table;

  // 10ms buckets of timers (up to 1 second)
  int _current_ms;
  std::unordered_set<Timer *> _ten_ms_buckets[100];

  // 1sec buckets of timers (up to 1 hour)
  int _current_s;
  std::unordered_set<Timer *> _s_buckets[NUM_SECOND_BUCKETS];

  // Heap of longer-lived timers (> 1hr)
  std::vector<Timer *> _extra_heap;

  // Current (ms) timestamp of the 0th bucket.
  unsigned long long _current_second;

  // Utility functions to replenish the buckets for each layer
  void refill_ms_buckets();
  void distribute_s_bucket(unsigned int);
  void refill_s_buckets();

  // Utility function to locate a Timer's correct home in the
  // store.
  std::unordered_set<Timer*>* find_bucket_from_timer(Timer*);
};

#endif
