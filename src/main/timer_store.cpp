#include "globals.h"
#include "timer_store.h"
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

TimerStore::TimerStore(HealthChecker* hc) :
  _health_checker(hc)
{
  _tick_timestamp = to_short_wheel_resolution(wall_time_ms());
}

TimerStore::~TimerStore()
{
  // Delete the timers in the lookup table as they will never pop now.
  for (std::map<TimerID, std::vector<Timer*>>::iterator it = 
                                                _timer_lookup_table.begin(); 
                                                it != _timer_lookup_table.end(); 
                                                ++it)
  {
    for (std::vector<Timer*>::iterator it_timer = it->second.begin();
                                       it_timer != it->second.end();
                                       ++it_timer)
    {
      delete *it_timer;
    }
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
  std::vector<Timer*> timers;

  // First check if this timer already exists.
  std::map<TimerID, std::vector<Timer*>>::iterator map_it = 
                                                _timer_lookup_table.find(t->id);

  if (map_it != _timer_lookup_table.end())
  {
    // There's already a timer with this ID in the store. We can update the 
    // existing timer, discard the new timer, or add the new timer, and move the 
    // old timer out of the timer wheel, but still save the timer information 
    // in the timer map (this last option is used in scaling operations) 
    LOG_DEBUG("Already a timer with ID %ld in the store", t->id);

    Timer* existing = map_it->second.front();

    // Compare timers for precedence, start-time then sequence-number.
    if ((t->start_time < existing->start_time) ||
        ((t->start_time == existing->start_time) &&
         (t->sequence_number < existing->sequence_number)))
    {
      LOG_DEBUG("The timer in the store is more recent, discard the new timer");
      delete t;
      return;
    }
    else
    {
      // We want to add the timer, so decide whether this is an update, or 
      // if we need to save off the old timer. 
      if (existing->cluster_view_id != t->cluster_view_id)
      {
        // The cluster IDs on the new and existing timers are different.
        // This means that the cluster configuration has changed between 
        // and when the timer was last updated. 
        LOG_DEBUG("Cluster view IDs are different on the new and existing timers");

        if (map_it->second.size() > 1)
        {
          // There's already a saved timer, but the new timer doesn't match the
          // existing timer. This is an error condition, and suggests that 
          // a scaling operation has been started before an old scaling operation
          // finished, or there was a node failure during a scaling operation. 
          // Either way, the saved timer information is out of date, and is 
          // deleted. 
          LOG_WARNING("Deleting out of date timer from timer map");
        }

        set_tombstone_values(t, existing);

        // Save the old timer information in the list of timers.
        Timer* existing_copy = new Timer(*existing);
        timers.push_back(t);
        timers.push_back(existing_copy);
      }
      else
      {
        set_tombstone_values(t, existing);
        timers.push_back(t);
  
        if (map_it->second.size() > 1)
        {
          Timer* existing_copy = new Timer(*map_it->second.back());
          timers.push_back(existing_copy);
        }
      }

      // Delete the old timer from the timer map and the timer wheel - it will
      // be added again later on
      delete_timer(t->id);
    }
  }
  else
  {
    // The timer doesn't already exist, so add it to the store. 
    LOG_DEBUG("Adding a new timer to the store with ID %ld", t->id);
    timers.push_back(t);
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
  _timer_lookup_table.insert(std::pair<TimerID, std::vector<Timer*>>(t->id, timers));

  // We've successfully added a timer, so confirm to the
  // health-checker that we're still healthy.
  _health_checker->health_check_passed();
}

// Delete a timer from the store by ID. This deletes all stored
// information about the timer, deleting it from the timer wheel
// and the timer map
void TimerStore::delete_timer(TimerID id)
{
  std::map<TimerID, std::vector<Timer*>>::iterator it;
  it = _timer_lookup_table.find(id);

  if (it != _timer_lookup_table.end())
  {
    // The timer(s) are still present in the store.
    // Delete the first timer from the timer wheel, delete any 
    // other timers in the timer list, then erase the entry from
    // the timer map. 
    Timer* timer = it->second.front(); 
    delete_timer_from_timer_wheel(timer);

    for (std::vector<Timer*>::iterator it_timer = it->second.begin();
                                       it_timer != it->second.end();
                                       ++it_timer)
    {
      delete *it_timer;
    }

    _timer_lookup_table.erase(id);
  }
}

void TimerStore::delete_timer_from_timer_wheel(Timer* timer)
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
          // LCOV_EXCL_STOP
        }
      }
    }
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

void TimerStore::update_replica_tracker_for_timer(TimerID id, 
                                                  int replica_index)
{
  // Check if the timer exists.
  std::map<TimerID, std::vector<Timer*>>::iterator map_it = 
                                                   _timer_lookup_table.find(id);

  if (map_it != _timer_lookup_table.end())
  {
    Timer* timer;
    bool timer_in_wheel = true;

    if (map_it->second.size() == 1)
    {
      timer = map_it->second.front(); 
    }
    else 
    {
      timer = map_it->second.back(); 
      timer_in_wheel = false;
    }

    std::string cluster_view_id;
    __globals->get_cluster_view_id(cluster_view_id);

    if (!timer->is_matching_cluster_view_id(cluster_view_id))
    {
      // The cluster view ID is out of date, so update the tracker. 
      int remaining_replicas = timer->update_replica_tracker(replica_index);

      if (remaining_replicas == 0)
      {
        if (!timer_in_wheel)
        {
          // All the new replicas have been told about the timer. We don't
          // need to store the information about the timer anymore. 
          delete timer; timer = NULL;
          map_it->second.pop_back();
        }
        else
        {
          // This is a window condition where node is responsible for an 
          // old timer replica. The node knows that all new replicas that 
          // should know about the timer are in the process of being told, 
          // but it hasn't yet received an update or tombstone for its 
          // replica. It will receive this soon. 
        }
      }
    }
  }
}

HTTPCode TimerStore::get_timers_for_node(std::string request_node, 
                                         int max_responses,
                                         std::string cluster_view_id,
                                         std::string& get_response)
{
  LOG_DEBUG("Get timers for %s", request_node.c_str());

  // Create the JSON doc for the Timer information
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();

  writer.String(JSON_TIMERS);
  writer.StartArray();

  int retrieved_timers = 0;
  for (std::map<TimerID, std::vector<Timer*>>::iterator it = 
                                           _timer_lookup_table.begin();
                                           it != _timer_lookup_table.end();                                        
                                           ++it)
  {
    // Take a copy of the timer so we don't change the timer in the wheel/map. 
    // If there's a saved timer in the timer map, use that rather than 
    // the timer in the wheel. 
    Timer* timer_copy;
    if (it->second.size() == 1)
    {
      timer_copy = new Timer(*it->second.front());
    }
    else
    {
      timer_copy = new Timer(*it->second.back());   
    }

    if (!timer_copy->is_tombstone())   
    {
      std::vector<std::string> old_replicas;

      if (timer_is_on_node(request_node,
                           cluster_view_id,
                           timer_copy, 
                           old_replicas))
      {
        writer.StartObject();
        {
          // The timer will have a replica on the requesting node. Add this entry 
          // to the JSON document
  
          // Add in Old Timer ID
          writer.String(JSON_TIMER_ID);
          writer.Int(timer_copy->id);
  
          // Add the old replicas
          writer.String(JSON_OLD_REPLICAS);
          writer.StartArray();
          for (std::vector<std::string>::const_iterator i = old_replicas.begin();
                                                        i != old_replicas.end();
                                                      ++i) 
          {
            writer.String((*i).c_str());
          }
          writer.EndArray();

          // Finally, add the timer itself 
          writer.String(JSON_TIMER);
          timer_copy->to_json_obj(&writer);
        }
        writer.EndObject();

        retrieved_timers++;
      }
    }

    // Tidy up the copy
    delete timer_copy;

    // Break out of the for loop once we hit the maximum number of 
    // timers to collect
    if ((max_responses != 0) && 
        (retrieved_timers == max_responses))
    {
      LOG_DEBUG("Reached the max number of timers to collect");
      break;
    }
  }
 
  writer.EndArray();
  writer.EndObject(); 

  get_response = sb.GetString();

  LOG_DEBUG("Retrieved %d timers", retrieved_timers);
  return ((max_responses != 0) &&
          (retrieved_timers == max_responses)) ? HTTP_PARTIAL_CONTENT : 
                                                 HTTP_OK;
}

bool TimerStore::timer_is_on_node(std::string request_node,
                                  std::string cluster_view_id,
                                  Timer* timer,
                                  std::vector<std::string>& old_replicas)
{
  bool timer_is_on_requesting_node = false;
  
  if (!timer->is_matching_cluster_view_id(cluster_view_id)) 
  {
    // Store the old replica list
    std::string localhost;
    __globals->get_cluster_local_ip(localhost);

    for (std::vector<std::string>::iterator it = timer->replicas.begin();
                                            it != timer->replicas.end();
                                            ++it)
    {
      old_replicas.push_back(*it);
    }

    // Calculate whether the new request node is interested in the timer. This
    // updates the replica list in the timer object to be the new replica list
    timer->update_cluster_information(); 

    int index = 0;
    for (std::vector<std::string>::iterator it = timer->replicas.begin();
                                            it != timer->replicas.end();
                                            ++it, ++index)
    {
      if ((*it == request_node) && 
          !(timer->has_replica_been_informed(index)))
      {
        timer_is_on_requesting_node = true;
        break;
      }
    }
  }

  return timer_is_on_requesting_node;
}

void TimerStore::set_tombstone_values(Timer* t, Timer* existing)
{
  if (t->is_tombstone())
  {
    // Learn the interval so that this tombstone lasts long enough to catch
    // errors.
    t->interval = existing->interval;
    t->repeat_for = existing->interval;
  }
}
