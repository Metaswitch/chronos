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
  virtual ~TimerStore();

  // Add a timer to the store.
  virtual void add_timer(Timer*);
  virtual void add_timers(std::unordered_set<Timer*>&);

  // Remove a timer by ID from the store.
  virtual void delete_timer(TimerID);

  // Get the next bucket of timers to pop.
  virtual void get_next_timers(std::unordered_set<Timer*>&);

  virtual void update_current_timestamp();

  // Give the UT test fixture access to our member variables
  friend class TestTimerStore;

private:

  // A table of all known timers
  std::map<TimerID, Timer *> _timer_lookup_table;

  // 10ms buckets of timers (up to 1 second)
  int _current_ms_bucket;
  std::unordered_set<Timer *> _ten_ms_buckets[100];

  // 1sec buckets of timers (up to 1 hour)
  int _current_s_bucket;
  std::unordered_set<Timer *> _s_buckets[NUM_SECOND_BUCKETS];

  // Heap of longer-lived timers (> 1hr)
  std::vector<Timer *> _extra_heap;

  // Current (ms) timestamp.
  uint64_t _current_timestamp;

  // Number of ms_buckets to move when updating the current view of the
  // store.
  uint64_t _ms_bucket_offset;

  // Utility functions to replenish the buckets for each layer
  void refill_ms_buckets();
  void distribute_s_bucket(unsigned int);
  void refill_s_buckets();

  // Utility function to locate a Timer's correct home in the
  // store.
  std::unordered_set<Timer*>* find_bucket_from_timer(Timer*);
};

#endif
