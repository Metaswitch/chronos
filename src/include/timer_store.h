#ifndef TIMER_STORE_H__
#define TIMER_STORE_H__

#include "timer.h"
#include "health_checker.h"

#include <unordered_set>
#include <map>
#include <string>

class TimerStore
{
public:
  TimerStore(HealthChecker* hc);
  virtual ~TimerStore();

  // Add a timer to the store.
  virtual void add_timer(Timer*);
  virtual void add_timers(std::unordered_set<Timer*>&);

  // Remove a timer by ID from the store.
  virtual void delete_timer(TimerID);

  // Get the next bucket of timers to pop.
  virtual void get_next_timers(std::unordered_set<Timer*>&);

  // Give the UT test fixture access to our member variables
  friend class TestTimerStore;

private:
  // The timer store uses 4 data structures to ensure timers pop on time:
  // - A short timer wheel consisting of 100 10ms buckets (1s in total).
  // - A long timer wheel consisting of 3600 1s buckets (1hr in total).
  // - A heap,
  // - A set of overdue timers.
  //
  // New timers are placed into on of these structures:
  // - The short wheel if due to pop in the next second.
  // - The long wheel if due to pop in the next hour (but not the next second).
  // - The heap if due to pop >=1hr in the future.
  // - The overdue set if they should have already popped.
  //
  // Timers in the overdue set are popped whenever `get_next_timers` is called.
  //
  // The short wheel ticks forward at the rate of 1 bucket per 10ms. On evey
  // tick the timers in the current bucket are popped. Every time the short
  // wheel does a full rotation, the long wheel ticks forward, and every timer
  // in the next bucket is placed into the correct place in the short wheel.
  // Every time the long wheel does a full rotation, all timers on the heap due
  // to pop in the next hour are placed into the appropriate place in the
  // short/long wheels.
  //
  // To achieve this the store tracks the time of the next tick to process
  // _tick_timestamp, which is a multiple of 10ms. The wheels are arrays
  // of sets that store pointers to timer objects. Any timestamp can be mapped
  // to an index into these arrays (using division and modulo arithmetic).
  //
  // When a tick is processed:
  // - All timers in the current short bucket are popped.
  // - The tick time is increased by 10ms.
  // - If the new tick time is on a 1s boundary, all timers in the current
  //   long bucket are distributed to the appropriate short bucket.
  // - If the new tick time is on a 1hr boundary, all timers in the heap that
  //   are due to pop in the next hour are moved into the correct positions in
  //   the short/long wheels.
  //
  // A result of this algorithm is that it is not possible to tell where a timer
  // is stored based solely on it's pop time. For example:
  // - At time 0ms, a new timer was set to pop at time 3,600,030ms. It would
  //   go straight into the heap as it's due to pop in >= 1hr.
  // - At time 3,599,900ms, another new timer is set to pop, also at
  //   3,600,030ms.  It would go in the short wheel as it's due to pop in <1s.
  // - So at time 3,599,990 one the timers are in different locations, despite
  //   popping at the same time.
  // - This is OK, because at time 3,600,000 the long wheel does a complete
  //   rotation, and both timers get moved into the short wheel, to be popped
  //   at the right time.
  //
  // This does mean that when removing a timer, the overdue set, both wheels and
  // the heap may need to be searched, although the timer is guaranteed to be in
  // only one of them (and the heap is searched last for efficiency).

  // A table of all known timers
  std::map<TimerID, Timer *> _timer_lookup_table;

  // Health checker, which is notified when a timer is successfully added.
  HealthChecker* _health_checker;
  
  // Constants controlling the size and resolution of the timer wheels.
  static const int SHORT_WHEEL_RESOLUTION_MS = 10;
  static const int SHORT_WHEEL_NUM_BUCKETS = 100;
  static const int SHORT_WHEEL_PERIOD_MS =
                                 (SHORT_WHEEL_RESOLUTION_MS * SHORT_WHEEL_NUM_BUCKETS);

  static const int LONG_WHEEL_RESOLUTION_MS = SHORT_WHEEL_PERIOD_MS;
  static const int LONG_WHEEL_NUM_BUCKETS = 3600;
  static const int LONG_WHEEL_PERIOD_MS =
                            (LONG_WHEEL_RESOLUTION_MS * LONG_WHEEL_NUM_BUCKETS);

  // Type of a single timer bucket.
  typedef std::unordered_set<Timer *> Bucket;

  // Bucket for timers that are added after they were supposed to pop.
  Bucket _overdue_timers;

  // The short timer wheel.
  Bucket _short_wheel[SHORT_WHEEL_NUM_BUCKETS];

  // The long timer wheel.
  Bucket _long_wheel[LONG_WHEEL_NUM_BUCKETS];

  // Heap of longer-lived timers (> 1hr)
  std::vector<Timer *> _extra_heap;

  // Timestamp of the next tick to process. This is stored in ms, and is always
  // a multiple of SHORT_WHEEL_RESOLUTION_MS.
  uint64_t _tick_timestamp;

  // Return the current wall time in ms.
  static uint64_t wall_time_ms();

  // Utility functions to locate a Timer's correct home in the store's timer
  // wheels.
  Bucket* short_wheel_bucket(Timer* timer);
  Bucket* long_wheel_bucket(Timer* timer);

  // Utility functions to locate a bucket in the timer wheels based on a
  // timestamp.
  Bucket* short_wheel_bucket(uint64_t t);
  Bucket* long_wheel_bucket(uint64_t t);

  // Utility methods to convert a timestamp to the resolution used by the
  // wheels.  These round down (so to 10ms accuracy, 12345 -> 12340, but 12340
  // -> 12340).
  static uint64_t to_short_wheel_resolution(uint64_t t);
  static uint64_t to_long_wheel_resolution(uint64_t t);

  // Refill timer wheels from the longer duration stores.
  //
  // This method is safe to call even if no wheels need refilling, in which
  // case it is a no-op.
  void maybe_refill_wheels();

  // Refill the long timer wheel from the heap.
  void refill_long_wheel();

  // Refill the short timer wheel from the long wheel.
  void refill_short_wheel();

  // Ensure a timer is no longer stored in the timer wheels.  This is an
  // expensive operation and should only be called when unsure of the timer
  // store's consistency.
  void purge_timer_from_wheels(Timer* timer);

  // Pop a single timer bucket into the set.
  void pop_bucket(TimerStore::Bucket* bucket,
                  std::unordered_set<Timer*>& set);
};

#endif

