/**
 * @file timer_store.h
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#ifndef TIMER_STORE_H__
#define TIMER_STORE_H__

#include "timer.h"
#include "timer_heap.h"
#include "health_checker.h"
#include "httpconnection.h"

#include <unordered_set>
#include <map>
#include <string>

// This defines a hashing mechanism, based on the uniqueness of the timer ids,
// that will be used when a Timer is added to a set
namespace std
{
  template <>
  struct hash<Timer*>
  {
    size_t operator()(const Timer* t) const
    {
      if (t != NULL)
      {
        return (hash<uint64_t>()(t->id));
      }
      else
      {
        return 0;
      }
    }
  };
}

class TimerStore
{
public:

  TimerStore(HealthChecker* hc);
  virtual ~TimerStore();

  // Insert a timer (with an ID that doesn't exist already)
  virtual void insert(Timer* timer);

  // Fetch a timer by ID and populate the Timer
  virtual void fetch(TimerID id, Timer** timer);

  // Fetch the next buckets of timers to pop and remove from store
  virtual void fetch_next_timers(std::unordered_set<Timer*>& set);

  // Removes all timers from the wheels and heap, without deleting them. Useful
  // for cleanup in UT.
  void clear();

  // A table of all known timers indexed by ID.
  std::map<TimerID, Timer*> _timer_lookup_id_table;

  // Constants controlling the size of the short wheel buckets (this needs to
  // be public so that the timer handler can work out how long it should
  // wait for a tick)
#ifndef UNIT_TEST
  static const int SHORT_WHEEL_RESOLUTION_MS = 8;
#else
  // Use fewer, larger buckets in UT, so we do less work when iterating over
  // timers, and run at an acceptable speed under Valgrind. The timer wheel
  // algorithms are independent of particular bucket sizes, so this doesn't
  // reduce the quality of our testing.
  static const int SHORT_WHEEL_RESOLUTION_MS = 256;
#endif

  class TSOrderedTimerIterator
  {
  protected:
    TSOrderedTimerIterator(TimerStore* ts, uint32_t time_from);

    void iterate_through_ordered_timers();

    std::vector<Timer*> _ordered_timers;
    std::vector<Timer*>::iterator _iterator;
    TimerStore* _ts;
    uint32_t _time_from;
  };

  class TSShortWheelIterator : public TSOrderedTimerIterator
  {
  public:
    TSShortWheelIterator(TimerStore* ts, uint32_t time_from);
    TSShortWheelIterator& operator++();
    Timer* operator*();
    bool end() const;

  private:
    int _bucket;
    void next_bucket();
  };

  class TSLongWheelIterator : public TSOrderedTimerIterator
   {
   public:
    TSLongWheelIterator(TimerStore* ts, uint32_t time_from);
    TSLongWheelIterator& operator++();
    Timer* operator*();
    bool end() const;

  private:
    int _bucket;
    void next_bucket();
  };

class TSHeapIterator
  {
  public:
    TSHeapIterator(TimerStore* ts, uint32_t time_from);
    TSHeapIterator& operator++();
    Timer* operator*();
    bool end() const;

  private:
    TimerStore* _ts;
    uint32_t _time_from;
    TimerHeap::ordered_iterator _iterator;
  };

  class TSIterator
  {
  public:
    TSIterator(TimerStore* ts, uint32_t time_from);
    TSIterator& operator++();
    Timer* operator*();
    bool end() const;

  private:
    TimerStore* _ts;
    uint32_t _time_from;
    TSShortWheelIterator _short_wheel_it;
    TSLongWheelIterator _long_wheel_it;
    TSHeapIterator _heap_it;

    void next_iterator();
  };

  TSIterator begin(uint32_t time_from);

private:
  // The timer store uses 4 data structures to ensure timers pop on time:
  // - A short timer wheel consisting of 128 8ms buckets (1024ms in total).
  // - A long timer wheel consisting of 4096 1024ms buckets (4194304ms in total).
  // - A heap,
  // - A set of overdue timers.
  //
  // New timers are placed into on of these structures:
  // - The short wheel if due to pop in 1024ms.
  // - The long wheel if due to pop in 4194304ms (but not the next 1024ms).
  // - The heap if due to pop >= 4194304 (~>1hr) in the future.
  // - The overdue set if they should have already popped.
  //
  // Timers in the overdue set are popped whenever `get_next_timers` is called.
  //
  // The short wheel ticks forward at the rate of 1 bucket per 8ms. On evey
  // tick the timers in the current bucket are popped. Every time the short
  // wheel does a full rotation, the long wheel ticks forward, and every timer
  // in the next bucket is placed into the correct place in the short wheel.
  // Every time the long wheel does a full rotation, all timers on the heap due
  // to pop in the next hour are placed into the appropriate place in the
  // short/long wheels.
  //
  // To achieve this the store tracks the time of the next tick to process
  // _tick_timestamp, which is a multiple of 8ms. The wheels are arrays
  // of sets that store pointers to timer objects. Any timestamp can be mapped
  // to an index into these arrays (using division and modulo arithmetic).
  //
  // When a tick is processed:
  // - All timers in the current short bucket are popped.
  // - The tick time is increased by 8ms.
  // - If the new tick time is on a 1s boundary, all timers in the current
  //   long bucket are distributed to the appropriate short bucket.
  // - If the new tick time is on a 1hr boundary, all timers in the heap that
  //   are due to pop in the next hour are moved into the correct positions in
  //   the short/long wheels.
  //
  // A result of this algorithm is that it is not possible to tell where a timer
  // is stored based solely on it's pop time. For example:
  // - At time 0ms, a new timer was set to pop at time 4,194,305ms. It would
  //   go straight into the heap as it's due to pop in >= long timer wheel total.
  // - At time 4,194,300ms, another new timer is set to pop, also at
  //   4,194,305ms.  It would go in the short wheel as it's due to pop in <
  //   short wheel timer total.
  // - So at time 4,194,300 one the timers are in different locations, despite
  //   popping at the same time.
  // - This is OK, because at time 4,194,304 the long wheel does a complete
  //   rotation, and both timers get moved into the short wheel, to be popped
  //   at the right time.
  //
  // This does mean that when removing a timer, the overdue set, both wheels and
  // the heap may need to be searched, although the timer is guaranteed to be in
  // only one of them (and the heap is searched last for efficiency).

  // Health checker, which is notified when a timer is successfully added.
  HealthChecker* _health_checker;

  // Constants controlling the size and resolution of the timer wheels.
#ifndef UNIT_TEST
  static const int SHORT_WHEEL_NUM_BUCKETS = 128;
  static const int LONG_WHEEL_NUM_BUCKETS = 4096;
#else
  // Use fewer, larger buckets in UT, so we do less work when iterating over
  // timers, and run at an acceptable speed under Valgrind. The timer wheel
  // algorithms are independent of particular bucket sizes, so this doesn't
  // reduce the quality of our testing.
  static const int SHORT_WHEEL_NUM_BUCKETS = 4;
  static const int LONG_WHEEL_NUM_BUCKETS = 2048;
#endif
  static const int SHORT_WHEEL_PERIOD_MS =
                                 (SHORT_WHEEL_RESOLUTION_MS * SHORT_WHEEL_NUM_BUCKETS);

  static const int LONG_WHEEL_RESOLUTION_MS = SHORT_WHEEL_PERIOD_MS;
  static const int LONG_WHEEL_PERIOD_MS =
                            (LONG_WHEEL_RESOLUTION_MS * LONG_WHEEL_NUM_BUCKETS);

  // Type of a single timer bucket.
  typedef std::unordered_set<Timer*> Bucket;

  // Bucket for timers that are added after they were supposed to pop.
  Bucket _overdue_timers;

  // The short timer wheel.
  Bucket _short_wheel[SHORT_WHEEL_NUM_BUCKETS];

  // The long timer wheel.
  Bucket _long_wheel[LONG_WHEEL_NUM_BUCKETS];

  // Heap of longer-lived timers (> 1hr)
  TimerHeap _extra_heap;

  // Timestamp of the next tick to process. This is stored in ms, and is always
  // a multiple of SHORT_WHEEL_RESOLUTION_MS.
  uint32_t _tick_timestamp;

  // Return the current timestamp in ms.
  static uint32_t timestamp_ms();

  // Utility functions to locate a Timer's correct home in the store's timer
  // wheels.
  Bucket* short_wheel_bucket(Timer* timer);
  Bucket* long_wheel_bucket(Timer* timer);

  // Utility functions to locate a bucket in the timer wheels based on a
  // timestamp.
  Bucket* short_wheel_bucket(uint32_t t);
  Bucket* long_wheel_bucket(uint32_t t);

  // Utility methods to convert a timestamp to the resolution used by the
  // wheels.  These round down (so to 8ms accuracy, 1644 -> 1640, but 1640
  // -> 1640).
  static uint32_t to_short_wheel_resolution(uint32_t t);
  static uint32_t to_long_wheel_resolution(uint32_t t);

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

  // Delete a timer from the timer wheel
  void remove_timer_from_timer_wheel(Timer* timer);
};


#endif

