/**
 * @file timer_store.cpp
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

#include "globals.h"
#include "timer_store.h"
#include "utils.h"
#include "log.h"
#include <algorithm>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "chronos_pd_definitions.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "constants.h"

// Macros to help log timer details.
#define TIMER_LOG_FMT "ID:       %lu\n"                                        \
                      "Start:    %u\n"                                         \
                      "Interval: %u\n"                                         \
                      "Repeat:   %u\n"                                         \
                      "Seq:      %u\n"                                         \
                      "URL:      %s\n"                                         \
                      "Body:\n"                                                \
                      "%s"
#define TIMER_LOG_PARAMS(T) (T).active_timer->id,                                           \
                            (T).active_timer->start_time_mono_ms,                           \
                            (T).active_timer->interval_ms,                                  \
                            (T).active_timer->repeat_for,                                   \
                            (T).active_timer->sequence_number,                              \
                            (T).active_timer->callback_url.c_str(),                         \
                            (T).active_timer->callback_body.c_str()

TimerStore::TimerStore(HealthChecker* hc) :
  _health_checker(hc)
{
  _tick_timestamp = to_short_wheel_resolution(timestamp_ms());
}

void TimerStore::clear()
{
  _timer_lookup_id_table.clear();
  _timer_view_id_table.clear();

  for (int ii = 0; ii < SHORT_WHEEL_NUM_BUCKETS; ++ii)
  {
    _short_wheel[ii].clear();
  }

  for (int ii = 0; ii < LONG_WHEEL_NUM_BUCKETS; ++ii)
  {
    _long_wheel[ii].clear();
  }

  _extra_heap.clear();
}

TimerStore::~TimerStore()
{
  // Delete the timers in the lookup table as they will never pop now.
  for (std::map<TimerID, TimerPair>::iterator it =
                                                _timer_lookup_id_table.begin();
                                                it != _timer_lookup_id_table.end();
                                                ++it)
  {
    delete it->second.active_timer;
    delete it->second.information_timer;
  }

  clear();
}

void TimerStore::insert(TimerPair tp,
                        TimerID id,
                        uint32_t next_pop_time,
                        std::vector<std::string> cluster_view_id_vector)
{
  if (_timer_lookup_id_table.find(id) != _timer_lookup_id_table.end())
  {
    // LCOV_EXCL_START - Not in UTs as this is a logic error
    throw std::logic_error("There is already a timer with this ID in the store!");
    // LCOV_EXCL_STOP
  }

  Bucket* bucket;

  if (Utils::overflow_less_than(next_pop_time, _tick_timestamp))
  {
    // The timer should have already popped so put it in the overdue timers,
    // and warn the user.
    //
    // We can't just put the timer in the next bucket to pop.  We need to know
    // what bucket to look in when deleting timers, and this is derived from
    // the pop time. So if we put the timer in the wrong bucket we can't find
    // it to delete it.
    TRC_WARNING("Modifying timer after pop time (current time is %lu). "
                "Window condition detected.\n" TIMER_LOG_FMT,
                _tick_timestamp,
                TIMER_LOG_PARAMS(tp));
    _overdue_timers.insert(tp);
  }
  else if (Utils::overflow_less_than(to_short_wheel_resolution(next_pop_time),
           to_short_wheel_resolution(_tick_timestamp + SHORT_WHEEL_PERIOD_MS)))
  {

    bucket = short_wheel_bucket(next_pop_time);
    bucket->insert(tp);
  }
  else if (Utils::overflow_less_than(to_long_wheel_resolution(next_pop_time),
           to_long_wheel_resolution(_tick_timestamp + LONG_WHEEL_PERIOD_MS)))
  {
    bucket = long_wheel_bucket(next_pop_time);
    bucket->insert(tp);
  }
  else
  {
    // Timer is too far in the future to be handled by the wheels, put it in
    // the extra heap.
    TRC_WARNING("Adding timer to extra heap, consider re-building with a larger "
                "LONG_WHEEL_NUM_BUCKETS constant");
    _extra_heap.insert(tp.active_timer);
  }

  // Add to the view specific mapping for easy retrieval on resync
  for (std::vector<std::string>::iterator it = cluster_view_id_vector.begin();
                                          it != cluster_view_id_vector.end();
                                          ++it)
  {
    _timer_view_id_table[*it].insert(id);
  }

  // Finally, add the timer to the lookup table.
  _timer_lookup_id_table[id] = tp;

  // We've successfully added a timer, so confirm to the
  // health-checker that we're still healthy.
  _health_checker->health_check_passed();
}

bool TimerStore::fetch(TimerID id, TimerPair& timer)
{
  std::map<TimerID, TimerPair>::iterator it;
  it = _timer_lookup_id_table.find(id);

  if (it != _timer_lookup_id_table.end())
  {
    // The TimerPair is still present in the store.
    // Remove the active timer from the wheel, and pass ownership
    // back by populating the timer parameter.
    timer = it->second;

    TRC_DEBUG("Removing timer from wheel and cluster view ids");
    remove_timer_from_timer_wheel(timer);
    remove_timer_from_cluster_view_id(timer);
    _timer_lookup_id_table.erase(id);


    TRC_DEBUG("Successfully found an existing timer");
    return true;
  }
  return false;
}

void TimerStore::fetch_next_timers(std::unordered_set<TimerPair>& set)
{
  // Always pop the overdue timers, even if we're not processing any ticks.
  pop_bucket(&_overdue_timers, set);

  // Now process the required number of ticks. Integer division does the
  // necessary rounding for us.
  uint32_t current_timestamp = timestamp_ms();
  uint32_t num_ticks = ((current_timestamp - _tick_timestamp) /
                   SHORT_WHEEL_RESOLUTION_MS);

  for (int ii = 0; ii < (int)(num_ticks); ++ii)
  {
    // Pop all timers in the current bucket.
    Bucket* bucket = short_wheel_bucket(_tick_timestamp);
    pop_bucket(bucket, set);

    // Get ready for the next tick - advance the tick time, and refill the
    // timer wheels.
    _tick_timestamp += SHORT_WHEEL_RESOLUTION_MS;
    maybe_refill_wheels();
  }
}

/*****************************************************************************/
/* Private functions.                                                        */
/*****************************************************************************/

uint32_t TimerStore::timestamp_ms()
{
  uint64_t time;
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
  {
    // LCOV_EXCL_START
    CL_CHRONOS_NO_SYSTEM_TIME.log(strerror(errno));
    TRC_ERROR("Failed to get system time - timer service cannot run: %s",
              strerror(errno));
    assert(!"Failed to get system time");
    // LCOV_EXCL_STOP
  }

  // Convert the timestamp to ms (being careful to always store the result in a
  // uinit64 to avoid wrapping).
  time = ts.tv_sec;
  time *= 1000;
  time += ts.tv_nsec / 1000000;
  return time;
}

uint32_t TimerStore::to_short_wheel_resolution(uint32_t t)
{
  return (t - (t % SHORT_WHEEL_RESOLUTION_MS));
}

uint32_t TimerStore::to_long_wheel_resolution(uint32_t t)
{
  return (t - (t % LONG_WHEEL_RESOLUTION_MS));
}

TimerStore::Bucket* TimerStore::short_wheel_bucket(TimerPair timer)
{
  return short_wheel_bucket(timer.active_timer->next_pop_time());
}

TimerStore::Bucket* TimerStore::long_wheel_bucket(TimerPair timer)
{
  return long_wheel_bucket(timer.active_timer->next_pop_time());
}

TimerStore::Bucket* TimerStore::short_wheel_bucket(uint32_t t)
{
  size_t bucket_index = (t / SHORT_WHEEL_RESOLUTION_MS) % SHORT_WHEEL_NUM_BUCKETS;
  return &_short_wheel[bucket_index];
}

TimerStore::Bucket* TimerStore::long_wheel_bucket(uint32_t t)
{
  size_t bucket_index = (t / LONG_WHEEL_RESOLUTION_MS) % LONG_WHEEL_NUM_BUCKETS;
  return &_long_wheel[bucket_index];
}

void TimerStore::pop_bucket(TimerStore::Bucket* bucket,
                            std::unordered_set<TimerPair>& set)
{
  for(TimerStore::Bucket::iterator it = bucket->begin();
                                   it != bucket->end();
                                   ++it)
  {
    _timer_lookup_id_table.erase((*it).active_timer->id);
    // Remove from cluster structure
    remove_timer_from_cluster_view_id(*it);
    set.insert(*it);
  }
  bucket->clear();
}

// Refill the timer buckets from the longer lived store. This function is safe
// to call at any time - if no changes are needed no work is done.
void TimerStore::maybe_refill_wheels()
{
  // Every hour on the hour refill the long timer wheel.
  if ((_tick_timestamp % LONG_WHEEL_PERIOD_MS) == 0)
  {
    refill_long_wheel();
  }

  // Every second on the second refill the short timer wheel. Do this 2nd as
  // timers may need to propogate from the heap -> long wheel -> short wheel.
  if ((_tick_timestamp % SHORT_WHEEL_PERIOD_MS) == 0)
  {
    refill_short_wheel();
  }
}

TimerPair TimerStore::pop_from_heap()
{
  Timer* t = static_cast<Timer*>(_extra_heap.get_next_timer());
  return _timer_lookup_id_table[t->id];
}

// Refill the long timer wheel by taking all timers from the heap that are due
// to pop in < long wheel timer total.
void TimerStore::refill_long_wheel()
{
  if (!_extra_heap.empty())
  {
    TimerPair timer = pop_from_heap();

    if (timer.active_timer != NULL)
    {
      TRC_DEBUG("Timer at top of heap is has ID %lu", timer.active_timer->id);
    }

    while ((timer.active_timer != NULL) &&
           (Utils::overflow_less_than(timer.active_timer->next_pop_time(), _tick_timestamp + LONG_WHEEL_PERIOD_MS)))
    {
      // Remove timer from heap
      _extra_heap.remove(timer.active_timer);
      Bucket* bucket = long_wheel_bucket(timer);
      bucket->insert(timer);

      if (!_extra_heap.empty())
      {
        timer = pop_from_heap();
      }
      else
      {
        timer.active_timer = NULL;
      }
    }
  }
}

// Refill the short timer wheel by distributing timers from the current bucket
// in the long timer wheel.
void TimerStore::refill_short_wheel()
{
  Bucket* long_bucket = long_wheel_bucket(_tick_timestamp);

  for (Bucket::iterator it = long_bucket->begin();
                        it != long_bucket->end();
                        ++it)
  {
    TimerPair timer = *it;
    Bucket* short_bucket = short_wheel_bucket(timer);
    short_bucket->insert(timer);
  }

  long_bucket->clear();
}

void TimerStore::remove_timer_from_timer_wheel(TimerPair timer)
{
  Bucket* bucket;
  size_t num_erased;

  // Delete the timer from the overdue buckets / timer wheels / heap. Try the
  // overdue bucket first, then the short wheel then the long wheel, then
  // finally the heap.
  num_erased = _overdue_timers.erase(timer);

  if (num_erased == 0)
  {
    bucket = short_wheel_bucket(timer);
    num_erased = bucket->erase(timer);

    if (num_erased == 0)
    {
      bucket = long_wheel_bucket(timer);
      num_erased = bucket->erase(timer);

      if (num_erased == 0)
      {
        bool success = _extra_heap.remove(timer.active_timer);
        
        if (!success)
        {
          // We failed to remove the timer from any data structure.  Try and
          // purge the timer from all the timer wheels (we're already sure
          // that it's not in the heap).
          // LCOV_EXCL_START
          TRC_ERROR("Failed to remove timer consistently");
          purge_timer_from_wheels(timer);
          // LCOV_EXCL_STOP
        }
      }
    }
  }
}

void TimerStore::remove_timer_from_cluster_view_id(TimerPair timer)
{
  // Remove from cluster structure
  _timer_view_id_table[timer.active_timer->cluster_view_id].erase(timer.active_timer->id);
  if (_timer_view_id_table[timer.active_timer->cluster_view_id].empty())
  {
    _timer_view_id_table.erase(timer.active_timer->cluster_view_id);
  }

  // Remove from cluster structure
  if (timer.information_timer)
  {
    _timer_view_id_table[timer.information_timer->cluster_view_id].erase(timer.active_timer->id);
    if (_timer_view_id_table[timer.information_timer->cluster_view_id].empty())
    {
      _timer_view_id_table.erase(timer.information_timer->cluster_view_id);
    }
  }
}

// Remove the timer from all the timer buckets.  This is a fallback that is only
// used when we're deleting a timer that should be in the store, but that we
// couldn't find in the buckets and the heap.  It's an expensive operation but
// is a last ditch effort to restore consistency.
// LCOV_EXCL_START
void TimerStore::purge_timer_from_wheels(TimerPair t)
{
  TRC_WARNING("Purging timer from store.\n", TIMER_LOG_FMT, TIMER_LOG_PARAMS(t));

  for (int ii = 0; ii < SHORT_WHEEL_NUM_BUCKETS; ++ii)
  {
    if (_short_wheel[ii].erase(t) != 0)
    {
      TRC_WARNING("  Deleting timer %lu from short wheel bucket %d", t.active_timer->id, ii);
    }
  }

  for (int ii = 0; ii < LONG_WHEEL_NUM_BUCKETS; ++ii)
  {
    if (_long_wheel[ii].erase(t) != 0)
    {
      TRC_WARNING("  Deleting timer %lu from long wheel bucket %d", t.active_timer->id, ii);
    }
  }
}
// LCOV_EXCL_STOP

TimerStore::TSIterator TimerStore::begin(std::string new_cluster_view_id)
{
  return TimerStore::TSIterator(this, new_cluster_view_id);
}

TimerStore::TSIterator TimerStore::end()
{
  return TimerStore::TSIterator(this);
}

TimerStore::TSIterator::TSIterator(TimerStore* ts, std::string cluster_view_id) :
  _ts(ts),
  _cluster_view_id(cluster_view_id)
{
  outer_iterator = _ts->_timer_view_id_table.begin();
  inner_next();
}

TimerStore::TSIterator::TSIterator(TimerStore* ts) :
  _ts(ts)
{
  outer_iterator = _ts->_timer_view_id_table.end();
}

TimerStore::TSIterator& TimerStore::TSIterator::operator++()
{
  ++inner_iterator;
  if (inner_iterator == outer_iterator->second.end())
  {
    ++outer_iterator;
    inner_next();
  }
  return *this;
}

// While the same TimerID may exist under two different cluster view ID
// indices, they will never exist under the same cluster index, so we are safe
// to look for a TimerID a second time, as if it has been deleted, the
// second cluster index will have removed its reference to that TimerID
TimerPair& TimerStore::TSIterator::operator*()
{
  std::map<TimerID, TimerPair>::iterator it = _ts->_timer_lookup_id_table.find(*inner_iterator);
  return it->second;
}

bool TimerStore::TSIterator::operator==(const TimerStore::TSIterator& other) const
{
  return (this->outer_iterator == other.outer_iterator &&
    (other.outer_iterator == _ts->_timer_view_id_table.end() ||
     this->inner_iterator == other.inner_iterator));
}

bool TimerStore::TSIterator::operator!=(const TimerStore::TSIterator& other) const
{
  return !(*this == other);
}

void TimerStore::TSIterator::inner_next()
{
  while (outer_iterator != _ts->_timer_view_id_table.end() &&
         outer_iterator->first == _cluster_view_id)
  {
    ++outer_iterator;
  }

  if (outer_iterator != _ts->_timer_view_id_table.end())
  {
    inner_iterator = outer_iterator->second.begin();
  }
}
