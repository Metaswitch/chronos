#include "timer_store.h"

#include <algorithm>
#include <time.h>

TimerStore::TimerStore() : _current_ms(0),
                           _current_s(0)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
  {
    perror("Failed to get system time - timer service cannot run: ");
    exit(-1);
  }
  _current_second = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000);
}

TimerStore::~TimerStore()
{
  // Delete the timers in the lookup table as they will never pop now.
  for (auto it = _timer_lookup_table.begin(); it != _timer_lookup_table.end(); it++)
  {
    delete it->second;
  }
  _timer_lookup_table.clear();
  for (int ii = 0; ii < 100; ii ++)
  {
    _ten_ms_buckets[ii].clear();
  }
  for (int ii = 0; ii < NUM_SECOND_BUCKETS; ii++)
  {
    _s_buckets[ii].clear();
  }
}

// Give a timer to the data store.  At this point the data store takes ownership of
// the timer and the caller should not reference it again (as the timer store may
// delete it at any time).
void TimerStore::add_timer(Timer* t)
{
  std::unordered_set<Timer*>* bucket = find_bucket_from_timer(t);
  if (bucket)
  {
    bucket->insert(t);
  }
  else
  {
    // Timer is too far in the future to be handled by the buckets, put it in the
    // extra heap.
    // LOG_INFO("Adding timer to extra heap, consider re-building with a larger"
    //          "NUM_SECOND_BUCKETS constant");
    _extra_heap.push_back(t);
    std::push_heap(_extra_heap.begin(), _extra_heap.end());
  }

  // Finally add the timer to the lookup table.
  _timer_lookup_table.insert(std::pair<unsigned int, Timer*>(t->id, t));
}

// Delete a timer from the store by ID.
void TimerStore::delete_timer(TimerID id)
{
  std::map<TimerID, Timer*>::iterator it;
  it = _timer_lookup_table.find(id);
  if (it != _timer_lookup_table.end())
  {
    // The timer is still present in the store, delete it.
    std::unordered_set<Timer*>* bucket = find_bucket_from_timer(it->second);
    if (bucket)
    {
      bucket->erase(it->second);
    }
    else
    {
      // TODO The timer is in the heap... work out how to delete it.  Might
      // require removing it from the vector and calling `make_heap()` to reorder
      // the vector.
    }
  }
}

// Retrieve the set of timers to pop in the next 10ms.  The timers returned are
// disowned by the store and must be freed by the caller or returned to the store
// through `add_timer()`.
//
// If the returned set is empty, there are no timers in the store and the caller
// will try again later (after a signal that a new timer has been added).
std::unordered_set<Timer *> TimerStore::get_next_timer()
{
  std::unordered_set<Timer*> bucket;

  // If there are no timers, simply return an empty set.
  if (_timer_lookup_table.empty())
  {
    return bucket;
  }

  // The store is not empty, find the first set that will pop.
  while (bucket.empty())
  {
    if (_current_ms == 100)
    {
      refill_ms_buckets();
    }
    bucket = _ten_ms_buckets[_current_ms];
  }

  // Remove the timers from the lookup table, this function passes ownership of the
  // memory for the timers to the caller.
  for (auto it = bucket.begin(); it != bucket.end(); it++)
  {
    _timer_lookup_table.erase((*it)->id);
  }

  // Return the bucket by value to pass the contents to the caller, also increment
  // the current bucket count.
  return bucket;
}

/*****************************************************************************/
/* Private functions.                                                        */
/*****************************************************************************/

// Moves timers from the next seconds bucket to the 10ms buckets and resets the
// current 10ms bucket index.
void TimerStore::refill_ms_buckets()
{
  if (_current_s == NUM_SECOND_BUCKETS)
  {
    distribute_s_bucket(_current_s++);
    _current_second += 1000;
    _current_ms = 0;
    return;
  }

  refill_s_buckets();

  distribute_s_bucket(_current_s++);
  _current_ms = 0;
}

// Moves timers from a given second bucket into the appropriate 10ms bucket.
void TimerStore::distribute_s_bucket(unsigned int index)
{
  for (auto it = _s_buckets[index].begin(); it != _s_buckets[index].end(); it++)
  {
    //TODO move to correct 10ms bucket, rather than the first one.
    _ten_ms_buckets[0].insert(*it);
  }
}

// Moves timers from the extra heap to the seconds buckets and resets the current
// seconds bucket index.
void TimerStore::refill_s_buckets()
{
  // TODO Currently a no-op.
  _current_s = 0;
}

// Calculate which bucket the given timer belongs in, based on the next pop time
// and the store's current view of the clock.
//
// If the timer would be stored in the heap, this function returns NULL.
std::unordered_set<Timer*>* TimerStore::find_bucket_from_timer(Timer* t)
{
  // Calculate how long till the timer will pop.
  unsigned int next_pop_timestamp = t->start_time + (t->sequence_number * t->interval);
  unsigned int time_to_next_pop;
  if (next_pop_timestamp < _current_second)
  {
    // Timer should have already popped.  Best we can do is put it in the very first
    // available bucket so it gets popped as soon as possible.
    // LOG_WARN("Modifying timer after pop time, window condition detected");
    time_to_next_pop = 0;
  }
  else
  {
    time_to_next_pop = next_pop_timestamp - _current_second;
  }

  // Now find the bucket for the timer and drop it in.
  if (time_to_next_pop < 1000)
  {
    return &_ten_ms_buckets[time_to_next_pop / 10];
  }
  else if (time_to_next_pop < 1000 * NUM_SECOND_BUCKETS)
  {
    return &_s_buckets[time_to_next_pop / 1000];
  }

  return NULL;
}
