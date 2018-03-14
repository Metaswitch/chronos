/**
 * @file timer_store.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
#define TIMER_LOG_PARAMS(T) (T)->id,                                           \
                            (T)->start_time_mono_ms,                           \
                            (T)->interval_ms,                                  \
                            (T)->repeat_for,                                   \
                            (T)->sequence_number,                              \
                            (T)->callback_url.c_str(),                         \
                            (T)->callback_body.c_str()

TimerStore::TimerStore(HealthChecker* hc) :
  _health_checker(hc)
{
  _tick_timestamp = to_short_wheel_resolution(timestamp_ms());
}

void TimerStore::clear()
{
  _timer_lookup_id_table.clear();

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
  for (std::map<TimerID, Timer*>::iterator it = _timer_lookup_id_table.begin();
                                           it != _timer_lookup_id_table.end();
                                           ++it)
  {
    delete it->second;
  }

  clear();
}

void TimerStore::insert(Timer* timer)
{
  if (_timer_lookup_id_table.find(timer->id) != _timer_lookup_id_table.end())
  {
    // LCOV_EXCL_START - Not in UTs as this is a logic error
    throw std::logic_error("There is already a timer with this ID in the store!");
    // LCOV_EXCL_STOP
  }

  Bucket* bucket;

  if (Utils::overflow_less_than(timer->next_pop_time(), _tick_timestamp))
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
                TIMER_LOG_PARAMS(timer));
    _overdue_timers.insert(timer);
  }
  else if (Utils::overflow_less_than(to_short_wheel_resolution(timer->next_pop_time()),
           to_short_wheel_resolution(_tick_timestamp + SHORT_WHEEL_PERIOD_MS)))
  {
    bucket = short_wheel_bucket(timer->next_pop_time());
    bucket->insert(timer);
  }
  else if (Utils::overflow_less_than(to_long_wheel_resolution(timer->next_pop_time()),
           to_long_wheel_resolution(_tick_timestamp + LONG_WHEEL_PERIOD_MS)))
  {
    bucket = long_wheel_bucket(timer->next_pop_time());
    bucket->insert(timer);
  }
  else
  {
    // Timer is too far in the future to be handled by the wheels, put it in
    // the extra heap.
    TRC_DEBUG("Adding timer to extra heap");
    _extra_heap.insert(timer);
  }

  // Finally, add the timer to the lookup table.
  _timer_lookup_id_table[timer->id] = timer;

  // We've successfully added a timer, so confirm to the
  // health-checker that we're still healthy.
  _health_checker->health_check_passed();
}

void TimerStore::fetch(TimerID id, Timer** timer)
{
  std::map<TimerID, Timer*>::iterator it;
  it = _timer_lookup_id_table.find(id);

  if (it != _timer_lookup_id_table.end())
  {
    // The Timer is still present in the store. Remove the timer from the
    // wheel, and pass ownership back by populating the timer parameter.
    *timer = it->second;

    TRC_DEBUG("Removing timer from wheel");
    remove_timer_from_timer_wheel(*timer);
    _timer_lookup_id_table.erase(id);

    TRC_DEBUG("Successfully found an existing timer");
  }
}

void TimerStore::fetch_next_timers(std::unordered_set<Timer*>& set)
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

TimerStore::Bucket* TimerStore::short_wheel_bucket(Timer* timer)
{
  return short_wheel_bucket(timer->next_pop_time());
}

TimerStore::Bucket* TimerStore::long_wheel_bucket(Timer* timer)
{
  return long_wheel_bucket(timer->next_pop_time());
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
                            std::unordered_set<Timer*>& set)
{
  for(TimerStore::Bucket::iterator it = bucket->begin();
                                   it != bucket->end();
                                   ++it)
  {
    _timer_lookup_id_table.erase((*it)->id);
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
  // Move all timers from the first bucket of the long wheel into the short.
  if ((_tick_timestamp % SHORT_WHEEL_PERIOD_MS) == 0)
  {
    refill_short_wheel();
  }
}

// Refill the long timer wheel by taking all timers from the heap that are due
// to pop in < long wheel timer total.
void TimerStore::refill_long_wheel()
{
  if (!_extra_heap.empty())
  {
    Timer* timer = static_cast<Timer*>(_extra_heap.get_next_timer());

    if (timer != NULL)
    {
      TRC_DEBUG("Timer at top of heap has ID %lu", timer->id);
    }

    while ((timer != NULL) &&
           (Utils::overflow_less_than(timer->next_pop_time(),
                                      _tick_timestamp + LONG_WHEEL_PERIOD_MS)))
    {
      // Remove timer from heap
      _extra_heap.remove(timer);
      Bucket* bucket = long_wheel_bucket(timer);
      bucket->insert(timer);

      if (!_extra_heap.empty())
      {
        timer = static_cast<Timer*>(_extra_heap.get_next_timer());
      }
      else
      {
        timer = NULL;
      }
    }
  }
}

// Refill the short timer wheel by distributing timers from the current bucket
// in the long timer wheel.
// All timers in the long wheel bucket are moved into the short wheel.
void TimerStore::refill_short_wheel()
{
  Bucket* long_bucket = long_wheel_bucket(_tick_timestamp);

  for (Bucket::iterator it = long_bucket->begin();
                        it != long_bucket->end();
                        ++it)
  {
    Bucket* short_bucket = short_wheel_bucket(*it);
    short_bucket->insert(*it);
  }

  long_bucket->clear();
}

// Refill the short timer wheel by distributing timers from the next bucket in
// the long timer wheel.
// Only timers due to pop within SHORT_WHEEL_PERIOD_MS should be moved from the
// long bucket, so we check the time of each timer.
void TimerStore::refill_short_wheel_from_next_long_bucket()
{
  Bucket* long_bucket = long_wheel_bucket(_tick_timestamp + LONG_WHEEL_RESOLUTION_MS);
  Bucket::iterator it = long_bucket->begin();

  while (it != long_bucket->end())
  {
    if (Utils::overflow_less_than((*it)->next_pop_time(),
                                  _tick_timestamp + SHORT_WHEEL_PERIOD_MS))
    {
      Bucket* short_bucket = short_wheel_bucket(*it);
      short_bucket->insert(*it);
      it = long_bucket->erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void TimerStore::remove_timer_from_timer_wheel(Timer* timer)
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
        bool success = _extra_heap.remove(timer);

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

// Remove the timer from all the timer buckets.  This is a fallback that is only
// used when we're deleting a timer that should be in the store, but that we
// couldn't find in the buckets and the heap.  It's an expensive operation but
// is a last ditch effort to restore consistency.
// LCOV_EXCL_START
void TimerStore::purge_timer_from_wheels(Timer* t)
{
  TRC_WARNING("Purging timer from store.\n", TIMER_LOG_FMT, TIMER_LOG_PARAMS(t));

  for (int ii = 0; ii < SHORT_WHEEL_NUM_BUCKETS; ++ii)
  {
    if (_short_wheel[ii].erase(t) != 0)
    {
      TRC_WARNING("  Deleting timer %lu from short wheel bucket %d", t->id, ii);
    }
  }

  for (int ii = 0; ii < LONG_WHEEL_NUM_BUCKETS; ++ii)
  {
    if (_long_wheel[ii].erase(t) != 0)
    {
      TRC_WARNING("  Deleting timer %lu from long wheel bucket %d", t->id, ii);
    }
  }
}
// LCOV_EXCL_STOP

TimerStore::TSOrderedTimerIterator::TSOrderedTimerIterator(TimerStore* ts,
                                                           uint32_t time_from) :
  _ts(ts),
  _time_from(time_from)
{}

void TimerStore::TSOrderedTimerIterator::iterate_through_ordered_timers()
{
  std::sort(_ordered_timers.begin(),
            _ordered_timers.end(),
            Timer::compare_timer_pop_times);

  _iterator = _ordered_timers.begin();

  while (_iterator != _ordered_timers.end())
  {
    if ((Utils::overflow_less_than(_time_from, (*_iterator)->next_pop_time())) ||
        (_time_from == (*_iterator)->next_pop_time()))
    {
      break;
    }
    else
    {
      ++_iterator;
    }
  }
}

TimerStore::TSShortWheelIterator::TSShortWheelIterator(TimerStore* ts,
                                                       uint32_t time_from) :
  TSOrderedTimerIterator(ts, time_from)
{
  // We have to check the next bucket of the long wheel for any timers which
  // need moving into the short wheel (to ensure they'll get picked up by one of
  // the iterators).
  _ts->refill_short_wheel_from_next_long_bucket();
  _bucket = 0;
  _iterator = _ordered_timers.end();

  if (Utils::overflow_less_than(to_short_wheel_resolution(time_from),
                                to_short_wheel_resolution(
                                   _ts->_tick_timestamp + SHORT_WHEEL_PERIOD_MS)))
  {

    _bucket = (time_from / SHORT_WHEEL_RESOLUTION_MS) % SHORT_WHEEL_NUM_BUCKETS;
    int current_bucket = (_ts->_tick_timestamp / SHORT_WHEEL_RESOLUTION_MS) % SHORT_WHEEL_NUM_BUCKETS;
    _end_bucket = current_bucket + SHORT_WHEEL_NUM_BUCKETS;

    // We can never return timers from a bucket earlier than the current bucket,
    // as we clear out the bucket once we move onto the next one.
    // Therefore, if the bucket that time_from falls into (_bucket) is less than
    // the bucket that the current time falls into (current_bucket), then
    // _bucket is logically in the "future".
    // In order to ensure that we only loop through each bucket once, we add
    // SHORT_WHEEL_NUM_BUCKETS to _bucket, moving it the correct distance from
    // _end_bucket.
    // Because _end_bucket is always SHORT_WHEEL_NUM_BUCKETS ahead of
    // current_bucket, _bucket will still always be less than _end_bucket.
    if (_bucket < current_bucket)
    {
      _bucket += SHORT_WHEEL_NUM_BUCKETS;
    }

    next_bucket();
  }
  else
  {
    _bucket = SHORT_WHEEL_NUM_BUCKETS;
    _end_bucket = SHORT_WHEEL_NUM_BUCKETS;
  }
}

TimerStore::TSShortWheelIterator& TimerStore::TSShortWheelIterator::operator++()
{
  ++_iterator;
  if (_iterator == _ordered_timers.end())
  {
    ++_bucket;
    next_bucket();
  }
  return *this;
}

Timer* TimerStore::TSShortWheelIterator::operator*()
{
  return *_iterator;
}

bool TimerStore::TSShortWheelIterator::end() const
{
  return ((_iterator == _ordered_timers.end()) &&
          (_bucket == _end_bucket));
}

void TimerStore::TSShortWheelIterator::next_bucket()
{
  _ordered_timers.clear();
  _iterator = _ordered_timers.end();

  while ((_bucket < _end_bucket) &&
         (_ordered_timers.size() == 0))
  {
    for (Timer* timer : _ts->_short_wheel[_bucket % SHORT_WHEEL_NUM_BUCKETS])
    {
      _ordered_timers.push_back(timer);
    }

    if (_ordered_timers.size() != 0)
    {
      iterate_through_ordered_timers();

      // LCOV_EXCL_START
      if (_iterator == _ordered_timers.end())
      {
        _ordered_timers.clear();
        _iterator = _ordered_timers.end();
        ++_bucket;
      }
      // LCOV_EXCL_STOP
    }
    else
    {
      ++_bucket;
    }
  }
}

TimerStore::TSLongWheelIterator::TSLongWheelIterator(TimerStore* ts,
                                                     uint32_t time_from) :
  TSOrderedTimerIterator(ts, time_from)
{
  // We have to top up the long wheel with times from the heap to ensure the
  // iterators will pick them up in the correct order.
  _ts->refill_long_wheel();
  _bucket = 0;
  _iterator = _ordered_timers.end();

  if (Utils::overflow_less_than(to_long_wheel_resolution(time_from),
                                to_long_wheel_resolution(
                                   _ts->_tick_timestamp + LONG_WHEEL_PERIOD_MS)))
  {
    _bucket = (time_from / LONG_WHEEL_RESOLUTION_MS) % LONG_WHEEL_NUM_BUCKETS;
    int current_bucket = (_ts->_tick_timestamp / LONG_WHEEL_RESOLUTION_MS) % LONG_WHEEL_NUM_BUCKETS;
    _end_bucket = current_bucket + LONG_WHEEL_NUM_BUCKETS;

    // We can never return timers from a bucket earlier than the current bucket,
    // as we clear out the bucket once we move onto the next one.
    // Therefore, if the bucket that time_from falls into (_bucket) is less than
    // the bucket that the current time falls into (current_bucket), then
    // _bucket is logically in the "future".
    // In order to ensure that we only loop through each bucket once, we add
    // LONG_WHEEL_NUM_BUCKETS to _bucket, moving it the correct distance from
    // _end_bucket.
    // Because _end_bucket is always LONG_WHEEL_NUM_BUCKETS ahead of
    // current_bucket, _bucket will still always be less than _end_bucket.
    if (_bucket < current_bucket)
    {
      _bucket += LONG_WHEEL_NUM_BUCKETS;
    }

    next_bucket();
  }
  else
  {
    _bucket = LONG_WHEEL_NUM_BUCKETS;
    _end_bucket = LONG_WHEEL_NUM_BUCKETS;
  }
}

TimerStore::TSLongWheelIterator& TimerStore::TSLongWheelIterator::operator++()
{
  ++_iterator;
  if (_iterator == _ordered_timers.end())
  {
    ++_bucket;
    next_bucket();
  }
  return *this;
}

Timer* TimerStore::TSLongWheelIterator::operator*()
{
  return *_iterator;
}

bool TimerStore::TSLongWheelIterator::end() const
{
  return ((_iterator == _ordered_timers.end()) &&
          (_bucket == _end_bucket));
}

void TimerStore::TSLongWheelIterator::next_bucket()
{
  _ordered_timers.clear();
  _iterator = _ordered_timers.end();

  while ((_bucket < _end_bucket) &&
         (_ordered_timers.size() == 0))
  {
    for (Timer* timer : _ts->_long_wheel[_bucket % LONG_WHEEL_NUM_BUCKETS])
    {
      _ordered_timers.push_back(timer);
    }

    if (_ordered_timers.size() != 0)
    {
      iterate_through_ordered_timers();

      // LCOV_EXCL_START
      if (_iterator == _ordered_timers.end())
      {
        _ordered_timers.clear();
        _iterator = _ordered_timers.end();
        ++_bucket;
      }
      // LCOV_EXCL_STOP
    }
    else
    {
      ++_bucket;
    }
  }
}

TimerStore::TSHeapIterator::TSHeapIterator(TimerStore* ts,
                                           uint32_t time_from) :
  _ts(ts),
  _time_from(time_from),
  _iterator(_ts->_extra_heap.ordered_begin())
{
  while (!this->end())
  {
    if (Utils::overflow_less_than(time_from, static_cast<Timer*>(*_iterator)->next_pop_time()))
    {
      break;
    }
    else
    {
      ++_iterator;
    }
  }
}

TimerStore::TSHeapIterator& TimerStore::TSHeapIterator::operator++()
{
  ++_iterator;
  return *this;
}

Timer* TimerStore::TSHeapIterator::operator*()
{
  return static_cast<Timer*>(*_iterator);
}

bool TimerStore::TSHeapIterator::end() const
{
  return (_iterator == _ts->_extra_heap.ordered_end());
}

TimerStore::TSIterator::TSIterator(TimerStore* ts,
                                   uint32_t time_from) :
  _ts(ts),
  _time_from(time_from),
  _short_wheel_it(ts, time_from),
  _long_wheel_it(ts, time_from),
  _heap_it(ts, time_from)
{
}

TimerStore::TSIterator& TimerStore::TSIterator::operator++()
{
  next_iterator();
  return *this;
}

Timer* TimerStore::TSIterator::operator*()
{
  if (!_short_wheel_it.end())
  {
    return (Timer*)(*_short_wheel_it);
  }
  else if (!_long_wheel_it.end())
  {
    return (Timer*)(*_long_wheel_it);
  }
  else
  {
    return (Timer*)(*_heap_it);
  }
}

bool TimerStore::TSIterator::end() const
{
  return ((_short_wheel_it.end()) &&
          (_long_wheel_it.end()) &&
          (_heap_it.end()));
}

void TimerStore::TSIterator::next_iterator()
{
  if (!_short_wheel_it.end())
  {
    ++_short_wheel_it;
  }
  else if (!_long_wheel_it.end())
  {
    ++_long_wheel_it;
  }
  else if (!_heap_it.end())
  {
    ++_heap_it;
  }
}

TimerStore::TSIterator TimerStore::begin(uint32_t time_from)
{
  return TimerStore::TSIterator(this, time_from);
}
