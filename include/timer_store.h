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

// This is the structure that is stored in the TimerStore. The active timer
// is used to determine when to pop and flow into buckets, and the information
// timer is kept when the cluster is updated
struct TimerPair {
  TimerPair() : active_timer(NULL),
                information_timer(NULL)
                {}
  Timer* active_timer;
  Timer* information_timer;

  bool operator==(const TimerPair &other) const
  {
    if (active_timer == NULL && information_timer == NULL &&
        other.active_timer == NULL && other.information_timer == NULL)
    {
      return true;
    }
    if (active_timer != NULL && information_timer != NULL &&
        other.active_timer != NULL && other.information_timer != NULL)
    {
      return (active_timer->id == other.active_timer->id &&
              information_timer->id == other.information_timer->id);
    }
    if (active_timer != NULL && other.active_timer != NULL &&
        information_timer == NULL && other.information_timer == NULL)
    {
      return (active_timer->id == other.active_timer->id);
    }
    if (information_timer != NULL && other.information_timer != NULL &&
        active_timer == NULL && other.active_timer == NULL)
    {
      return (information_timer->id == other.information_timer->id);
    }
    return false;
  }

  bool operator<(const TimerPair &other) const
  {
    // Check for active timer
    if (!other.active_timer)
    {
      return true;
    }

    return (active_timer->id < other.active_timer->id);
  }
};


// This defines a hashing mechanism, based on the uniqueness of the timer ids,
// that will be used when a TimerPair is added to a set
namespace std
{
  template <>
  struct hash<TimerPair>
  {
    size_t operator()(const TimerPair& tp) const
    {
      if (tp.active_timer != NULL)
      {
        return (hash<uint64_t>()(tp.active_timer->id));
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
  virtual void insert(TimerPair tp, TimerID id,
                      uint32_t next_pop_time,
                      std::vector<std::string> cluster_view_id_vector);

  // Fetch a timer by ID, populate the TimerPair, and return whether the
  // value was found or not
  virtual bool fetch(TimerID id, TimerPair& tp);

  // Fetch the next buckets of timers to pop and remove from store
  virtual void fetch_next_timers(std::unordered_set<TimerPair>& set);

  // Removes all timers from the wheels and heap, without deleting them. Useful for cleanup in UT.
  void clear();

  // A table of all known timers indexed by ID. The TimerPair is in the
  // timer wheel - any other timers are stored for use when
  // resynchronising between Chronos's.
  std::map<TimerID, TimerPair> _timer_lookup_id_table;

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

  class TSIterator
  {
  public:
    TSIterator(TimerStore* ts, std::string cluster_view_id);
    TSIterator(TimerStore* ts);

    TSIterator& operator++();
    TimerPair& operator*();
    bool operator==(const TSIterator& other) const;
    bool operator!=(const TSIterator& other) const;

  private:
    std::map<std::string, std::unordered_set<TimerID>>::iterator outer_iterator;
    std::unordered_set<TimerID>::iterator inner_iterator;
    TimerStore* _ts;
    std::string _cluster_view_id;
    void inner_next();
  };

  TSIterator begin(std::string cluster_view_id);
  TSIterator end();

private:
  // A table of all know timers indexed by cluster view id.
  std::map<std::string, std::unordered_set<TimerID>> _timer_view_id_table;

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

  // Heap of longer-lived timers (> 1hr)
  TimerHeap _extra_heap;

  // We store Timer*s in the heap (as the TimerHeap interface requires
  // heap-allocated pointers and the TimerPair is always stack-allocated), so
  // this utility method looks up the timer ID to get back to a TimerPair.
  TimerPair pop_from_heap();

  // Return the current timestamp in ms.
  static uint32_t timestamp_ms();

  // Delete a timer from the timer wheel
  void remove_timer_from_timer_wheel(TimerPair timer);

  // Delete a timer from the cluster view ID index
  void remove_timer_from_cluster_view_id(TimerPair timer);
};


#endif

