
#include "timer_store.h"
#include "log.h"
#include <algorithm>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "chronos_pd_definitions.h"

// Macros to help log timer details.
#define TIMER_LOG_FMT "ID:       %lu\n"                                        \
                      "Start:    %lu\n"                                        \
                      "Interval: %u\n"                                         \
                      "Repeat:   %u\n"                                         \
                      "Seq:      %u\n"                                         \
                      "URL:      %s\n"                                         \
                      "Body:\n"                                                \
                      "%s"
#define TIMER_LOG_PARAMS(T) (T)->id,                                           \
                            (T)->start_time,                                   \
                            (T)->interval,                                     \
                            (T)->repeat_for,                                   \
                            (T)->sequence_number,                              \
                            (T)->callback_url.c_str(),                         \
                            (T)->callback_body.c_str()

TimerStore::TimerStore(HealthChecker* hc)
{
  _tick_timestamp = to_short_wheel_resolution(wall_time_ms());
  _health_checker = hc;
}

TimerStore::~TimerStore()
{
  // Delete the timers in the lookup table as they will never pop now.
  for (std::map<TimerID, Timer*>::iterator it = _timer_lookup_table.begin(); 
                                           it != _timer_lookup_table.end(); 
                                           ++it)
  {
    delete it->second;
  }

  _timer_lookup_table.clear();

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

// Give a timer to the data store.  At this point the data store takes ownership
// of the timer and the caller should not reference it again (as the timer store
// may delete it at any time).
void TimerStore::add_timer(Timer* t)
{
  // First check if this timer already exists.
  std::map<TimerID, Timer*>::iterator map_it = _timer_lookup_table.find(t->id);

  if (map_it != _timer_lookup_table.end())
  {
    Timer* existing = map_it->second;

    // Compare timers for precedence, start-time then sequence-number.
    if ((t->start_time < existing->start_time) ||
        ((t->start_time == existing->start_time) &&
         (t->sequence_number < existing->sequence_number)))
    {
      // Existing timer is more recent
      delete t;
      return;
    }
    else
    {
      // Existing timer is older
      if (t->is_tombstone())
      {
        // Learn the interval so that this tombstone lasts long enough to catch
        // errors.
        t->interval = existing->interval;
        t->repeat_for = existing->interval;
      }
      delete_timer(t->id);
    }
  }

  // Work out where to store the timer (overdue bucket, short wheel, long wheel,
  // or heap).
  //
  // Note that these if tests MUST use less than. For example if _tick_time is
  // 20,330 a 1s timer will pop at 21,330. When in the short wheel the timer
  // will live in bucket 33. But this is the bucket that is about to pop, so the
  // timer must actually go in to the long wheel.  The same logic applies for
  // the 1s buckets (where timers due to pop in >=1hr need to go into the heap).
  uint64_t next_pop_time = t->next_pop_time();
  Bucket* bucket;

  if (next_pop_time < _tick_timestamp)
  {
    // The timer should have already popped so put it in the overdue timers,
    // and warn the user.
    //
    // We can't just put the timer in the next bucket to pop.  We need to know
    // what bucket to look in when deleting timers, and this is derived from
    // the pop time. So if we put the timer in the wrong bucket we can't find
    // it to delete it.
    LOG_WARNING("Modifying timer after pop time (current time is %lu). "
                "Window condition detected.\n" TIMER_LOG_FMT,
                _tick_timestamp,
                TIMER_LOG_PARAMS(t));
    _overdue_timers.insert(t);
  }
  else if (to_short_wheel_resolution(next_pop_time) <
           to_short_wheel_resolution(_tick_timestamp + SHORT_WHEEL_PERIOD_MS))
  {

    bucket = short_wheel_bucket(next_pop_time);
    bucket->insert(t);
  }
  else if (to_long_wheel_resolution(next_pop_time) <
           to_long_wheel_resolution(_tick_timestamp + LONG_WHEEL_PERIOD_MS))
  {
    bucket = long_wheel_bucket(next_pop_time);
    bucket->insert(t);
  }
  else
  {
    // Timer is too far in the future to be handled by the wheels, put it in
    // the extra heap.
    LOG_WARNING("Adding timer to extra heap, consider re-building with a larger "
                "LONG_WHEEL_NUM_BUCKETS constant");
    _extra_heap.push_back(t);
    std::push_heap(_extra_heap.begin(), _extra_heap.end());
  }

  // Finally, add the timer to the lookup table.
  _timer_lookup_table.insert(std::pair<TimerID, Timer*>(t->id, t));

  // We've successfully added a timer, so confirm to the
  // health-checker that we're still healthy.
  _health_checker->health_check_passed();
}

// Add a collection of timers to the data store.  The collection is emptied by
// this operation, since the timers are now owned by the store.
void TimerStore::add_timers(std::unordered_set<Timer*>& set)
{
  for (std::unordered_set<Timer*>::iterator it = set.begin(); 
                                            it != set.end(); 
                                            ++it)
  {
    add_timer(*it);
  }
  set.clear();
}

// Delete a timer from the store by ID.
void TimerStore::delete_timer(TimerID id)
{
  std::map<TimerID, Timer*>::iterator it;
  it = _timer_lookup_table.find(id);
  if (it != _timer_lookup_table.end())
  {
    // The timer is still present in the store, delete it.
    Timer* timer = it->second;
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
          std::vector<Timer*>::iterator heap_it;
          heap_it = std::find(_extra_heap.begin(), _extra_heap.end(), timer);
          if (heap_it != _extra_heap.end())
          {
            // Timer is in heap, remove it.
            _extra_heap.erase(heap_it, heap_it + 1);
            std::make_heap(_extra_heap.begin(), _extra_heap.end());
          }
          else
          {
            // We failed to remove the timer from any data structure.  Try and
            // purge the timer from all the timer wheels (we're already sure
            // that it's not in the heap).

            // LCOV_EXCL_START
            LOG_ERROR("Failed to remove timer consistently");
            purge_timer_from_wheels(timer);

            // Assert after purging, so we get a nice log detailing how the
            // purge went.
            assert(!"Failed to remove timer consistently");
            // LCOV_EXCL_STOP
          }
        }
      }
    }

    _timer_lookup_table.erase(id);
    delete timer;
  }
}

// Retrieve the set of timers to pop.  The timers returned are disowned by the
// store and must be freed by the caller or returned to the store through
// `add_timer()`.
//
// If the returned set is empty, there are no timers in the store and the caller
// will try again later (after a signal that a new timer has been added).
void TimerStore::get_next_timers(std::unordered_set<Timer*>& set)
{
  // Always pop the overdue timers, even if we're not processing any ticks.
  pop_bucket(&_overdue_timers, set);

  // Now process the required number of ticks. Integer division does the
  // necessary rounding for us.
  uint64_t current_timestamp = wall_time_ms();
  int num_ticks = ((current_timestamp - _tick_timestamp) /
                   SHORT_WHEEL_RESOLUTION_MS);

  for (int ii = 0; ii < num_ticks; ++ii)
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

uint64_t TimerStore::wall_time_ms()
{
  uint64_t wall_time;
  struct timespec ts;

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
  {
    CL_CHRONOS_NO_SYSTEM_TIME.log(strerror(errno));
    LOG_ERROR("Failed to get system time - timer service cannot run: %s",
              strerror(errno));
    assert(!"Failed to get system time");
  }

  // Convert the timestamp to ms (being careful to always store the result in a
  // uinit64 to avoid wrapping).
  wall_time = ts.tv_sec;
  wall_time *= 1000;
  wall_time += (ts.tv_nsec / 1000000);
  return wall_time;
}

uint64_t TimerStore::to_short_wheel_resolution(uint64_t t)
{
  return (t - (t % SHORT_WHEEL_RESOLUTION_MS));
}

uint64_t TimerStore::to_long_wheel_resolution(uint64_t t)
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

TimerStore::Bucket* TimerStore::short_wheel_bucket(uint64_t t)
{
  size_t bucket_index = (t / SHORT_WHEEL_RESOLUTION_MS) % SHORT_WHEEL_NUM_BUCKETS;
  return &_short_wheel[bucket_index];
}

TimerStore::Bucket* TimerStore::long_wheel_bucket(uint64_t t)
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
    _timer_lookup_table.erase((*it)->id);
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

// Refill the long timer wheel by taking all timers from the heap that are due
// to pop in < 1hr.
void TimerStore::refill_long_wheel()
{
  if (!_extra_heap.empty())
  {
    std::pop_heap(_extra_heap.begin(), _extra_heap.end());
    Timer* timer = _extra_heap.back();

    while ((timer != NULL) &&
           (timer->next_pop_time() < _tick_timestamp + LONG_WHEEL_PERIOD_MS))
    {
      // Remove timer from heap
      _extra_heap.pop_back();
      Bucket* bucket = long_wheel_bucket(timer);
      bucket->insert(timer);

      if (!_extra_heap.empty())
      {
        std::pop_heap(_extra_heap.begin(), _extra_heap.end());
        timer = _extra_heap.back();
      }
      else
      {
        timer = NULL;
      }
    }

    // Push the timer back into the heap.
    if (!_extra_heap.empty())
    {
      std::push_heap(_extra_heap.begin(), _extra_heap.end());
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
    Timer* timer = *it;
    Bucket* short_bucket = short_wheel_bucket(timer);
    short_bucket->insert(timer);
  }

  long_bucket->clear();
}

// Remove the timer from all the timer buckets.  This is a fallback that is only
// used when we're deleting a timer that should be in the store, but that we
// couldn't find in the buckets and the heap.  It's an expensive operation but
// is a last ditch effort to restore consistency.
// LCOV_EXCL_START
void TimerStore::purge_timer_from_wheels(Timer* t)
{
  LOG_WARNING("Purging timer from store.\n", TIMER_LOG_FMT, TIMER_LOG_PARAMS(t));

  for (int ii = 0; ii < SHORT_WHEEL_NUM_BUCKETS; ++ii)
  {
    if (_short_wheel[ii].erase(t) != 0)
    {
      LOG_WARNING("  Deleting timer %lu from short wheel bucket %d", t->id, ii);
    }
  }

  for (int ii = 0; ii < LONG_WHEEL_NUM_BUCKETS; ++ii)
  {
    if (_long_wheel[ii].erase(t) != 0)
    {
      LOG_WARNING("  Deleting timer %lu from long wheel bucket %d", t->id, ii);
    }
  }
}
// LCOV_EXCL_STOP

