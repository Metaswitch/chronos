/**
 * @file timer_handler.cpp
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

#include <time.h>
#include <cstring>
#include <iostream>

#include "utils.h"
#include "globals.h"
#include "timer_handler.h"
#include "log.h"
#include "constants.h"

void* TimerHandler::timer_handler_entry_func(void* arg)
{
  static_cast<TimerHandler*>(arg)->run();
  return NULL;
}

TimerHandler::TimerHandler(TimerStore* store,
                           Callback* callback,
                           Replicator* replicator,
                           GRReplicator* gr_replicator,
                           SNMP::ContinuousIncrementTable* all_timers_table,
                           SNMP::InfiniteTimerCountTable* tagged_timers_table,
                           SNMP::InfiniteScalarTable* scalar_timers_table) :
  _store(store),
  _callback(callback),
  _replicator(replicator),
  _gr_replicator(gr_replicator),
  _all_timers_table(all_timers_table),
  _tagged_timers_table(tagged_timers_table),
  _scalar_timers_table(scalar_timers_table),
  _terminate(false),
  _nearest_new_timer(-1)
{
  pthread_mutex_init(&_mutex, NULL);

#ifdef UNIT_TEST
  _cond = new MockPThreadCondVar(&_mutex);
#else
  _cond = new CondVar(&_mutex);
#endif

  int rc = pthread_create(&_handler_thread,
                          NULL,
                          &timer_handler_entry_func,
                          (void*)this);
  if (rc < 0)
  {
    // LCOV_EXCL_START
    printf("Failed to start timer handling thread: %s", strerror(errno));
    exit(2);
    // LCOV_EXCL_STOP
  }
}

TimerHandler::~TimerHandler()
{
  if (_handler_thread)
  {
    pthread_mutex_lock(&_mutex);
    _terminate = true;
    _cond->signal();
    pthread_mutex_unlock(&_mutex);
    pthread_join(_handler_thread, NULL);
  }
  delete _cond;
  _cond = NULL;

  pthread_mutex_destroy(&_mutex);
  delete _callback;
}

void TimerHandler::add_timer(Timer* timer, bool update_stats)
{
  pthread_mutex_lock(&_mutex);

  // Pull out any existing timer from the timer store
  Timer* existing_timer = NULL;
  _store->fetch(timer->id, &existing_timer);

  // We've found a timer.
  if (existing_timer)
  {
    bool will_add_timer = true;
    std::string cluster_view_id;
    __globals->get_cluster_view_id(cluster_view_id);

    if ((timer->is_matching_cluster_view_id(cluster_view_id)) &&
        !(existing_timer->is_matching_cluster_view_id(cluster_view_id)))
    {
      // If the new timer matches the current cluster view ID, and the old timer
      // doesn't, always prioritise the new timer.
      TRC_DEBUG("Adding timer with current cluster view ID");
    }
    else if (timer->sequence_number == existing_timer->sequence_number)
    {
      // If the new timer has the same sequence number as the old timer,
      // then check which timer is newer. If the existing timer is newer then we just
      // want to replace the timer and not change it
      if (Utils::overflow_less_than(timer->start_time_mono_ms,
                                    existing_timer->start_time_mono_ms))
      {
        TRC_DEBUG("Timer sequence numbers the same, but timer is older than the "
                  "timer in the store");
        delete timer;
        timer = new Timer(*existing_timer);
        will_add_timer = false;
      }
      else
      {
        TRC_DEBUG("Adding timer as it's newer than the timer in the store");
      }
    }
    else
    {
      // One of the sequence numbers is non-zero - at least one request is not
      // from the client
      if ((near_time(timer->start_time_mono_ms,
                     existing_timer->start_time_mono_ms))            &&
          (timer->sequence_number < existing_timer->sequence_number) &&
          (timer->sequence_number != 0))
      {
        // These are probably the same timer, and the timer we are trying to add is both
        // not from the client, and has a lower sequence number (so is less "informed")
        TRC_DEBUG("Not adding timer as it's older than the timer in the store");
        delete timer;
        timer = new Timer(*existing_timer);
        will_add_timer = false;
      }
      else
      {
        TRC_DEBUG("Adding timer as it's newer than the timer in the store");
      }
    }

    // We're adding the new timer (rather than discarding it and putting the
    // existing timer back in the store)
    if (will_add_timer)
    {
      // If the new timer is a tombstone, make sure its interval is long enough
      save_tombstone_information(timer, existing_timer);

      // Update the site information
      save_site_information(timer, existing_timer);
    }
  }
  else
  {
    TRC_DEBUG("Adding new timer");
  }

  // It would be good in future work to pull all statistics logic out into a
  // separate statistics module, passing in new and old tags, and what is
  // happening to the timer (add, update, delete), to keep the timer_handler
  // scope of responsibility clear.

  // Update statistics 
  if (update_stats)
  {
    std::map<std::string, uint32_t> tags_to_add = std::map<std::string, uint32_t>();
    std::map<std::string, uint32_t> tags_to_remove = std::map<std::string, uint32_t>();

    if (timer->is_tombstone())
    {
      // If the new timer is a tombstone, no new tags should be added
      // If it overwrites an existing timer, the old tags should
      // be removed, and global count decremented
      if ((existing_timer) &&
          !(existing_timer->is_tombstone()))
      {
        tags_to_remove = existing_timer->tags;
        TRC_DEBUG("New timer is a tombstone overwriting an existing timer");
        _all_timers_table->decrement(1);
      }
    }
    else
    {
      // Add new timer tags
      tags_to_add = timer->tags;

      // If there was an old existing timer, its tags should be removed
      // Global count should only increment if there was not an old
      // timer, as otherwise it is only an update.
      if ((existing_timer) &&
          !(existing_timer->is_tombstone()))
      {
        tags_to_remove = existing_timer->tags;
      }
      else
      {
        TRC_DEBUG("New timer being added, and no existing timer");
        _all_timers_table->increment(1);
      }
    }

    update_statistics(tags_to_add, tags_to_remove);
  }

  delete existing_timer;

  TRC_DEBUG("Inserting the new timer with ID %llu", timer->id);
  _store->insert(timer);

  pthread_mutex_unlock(&_mutex);
}

void TimerHandler::return_timer(Timer* timer)
{
  // We may need to tombstone the timer
  // We also need to check for timers with a zero interval and repeat_for value.
  // This would be for when a customer wants some information back from Chronos
  // immediately and only once, hence we should tombstone the timer after use.
  if (((timer->sequence_number + 1) * timer->interval_ms > timer->repeat_for) ||
      ((timer->interval_ms == 0) && 
       (timer->repeat_for == 0)))
  {
    // This timer won't pop again, so tombstone it and update statistics
    timer->become_tombstone();
    update_statistics(std::map<std::string, uint32_t>(), timer->tags);

    // Decrement global timer count for tombstoned timer
    TRC_DEBUG("Timer won't pop again and is being tombstoned");
    _all_timers_table->decrement(1);
  }

  // Timer will be re-added, but stats should not be updated, as
  // no stats were altered on it popping
  add_timer(timer, false);
}

void TimerHandler::handle_successful_callback(TimerID timer_id)
{
  // Fetch the timer from the store and replicate it (within and cross-site)
  pthread_mutex_lock(&_mutex);

  Timer* timer = NULL;
  _store->fetch(timer_id, &timer);

  if (timer)
  {
    // Update the sites
    timer->update_sites_on_timer_pop();
    _replicator->replicate(timer);
    _gr_replicator->replicate(timer);

    // Pass the timer pair back to the store, relinquishing responsibility for it.
    _store->insert(timer);
  }

  pthread_mutex_unlock(&_mutex);
}

void TimerHandler::handle_failed_callback(TimerID timer_id)
{
  // Fetch the timer from the store and delete it.
  pthread_mutex_lock(&_mutex);
  Timer* timer = NULL;
  _store->fetch(timer_id, &timer);
  pthread_mutex_unlock(&_mutex);

  if (timer)
  {
    // If the timer is not a tombstone we also update statistics.
    if (!timer->is_tombstone())
    {
      update_statistics(std::map<std::string, uint32_t>(), timer->tags);
      _all_timers_table->decrement(1);
    }
  }

  delete timer; timer = NULL;
}

HTTPCode TimerHandler::get_timers_for_node(std::string request_node,
                                           int max_responses,
                                           std::string cluster_view_id,
                                           uint32_t time_from,
                                           std::string& get_response)
{
  // We pass in the time_from parameter from the handlers. We will use this
  // parameter in the future to help with resynchronisation operations. We
  // pass it into the timer handler now to help with UTing the handler code

  pthread_mutex_lock(&_mutex);

  // Create the JSON doc for the Timer information
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();

  writer.String(JSON_TIMERS);
  writer.StartArray();

  TRC_DEBUG("Get timers for %s", request_node.c_str());

  int retrieved_timers = 0;
  uint32_t last_time_from = 0;
  uint32_t current_time_from = 0;

  for (TimerStore::TSIterator it = _store->begin(time_from);
       !(it.end());
       ++it)
  {
    Timer* timer_copy = new Timer(**it);
    current_time_from = timer_copy->next_pop_time();

    // Break out of the for loop once we hit the maximum number of
    // timers to collect, and we know that the next timer doesn't
    // have the same pop time as our last timer
    if ((retrieved_timers >= max_responses) &&
        (last_time_from != current_time_from))
    {
      TRC_DEBUG("Reached the max number of timers to collect");
      delete timer_copy;
      break;
    }

    if (!timer_copy->is_tombstone())
    {
      std::vector<std::string> old_replicas;
      if (timer_is_on_node(request_node,
                           timer_copy,
                           old_replicas))
      {
        writer.StartObject();
        {
          // The timer will have a replica on the requesting node. Add this
          // entry to the JSON document

          // Add in Old Timer ID
          writer.String(JSON_TIMER_ID);
          writer.Int64(timer_copy->id);

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

      last_time_from = current_time_from;
    }

    // Tidy up the copy
    delete timer_copy;
  }

  writer.EndArray();
  writer.EndObject();
  get_response = sb.GetString();
  pthread_mutex_unlock(&_mutex);

  TRC_DEBUG("Retrieved %d timers", retrieved_timers);
  return (retrieved_timers >= max_responses) ? HTTP_PARTIAL_CONTENT : HTTP_OK;
}

bool TimerHandler::timer_is_on_node(std::string request_node,
                                    Timer* timer,
                                    std::vector<std::string>& old_replicas)
{
  bool timer_is_on_requesting_node = false;

  // Store the old replica list
  std::string localhost;
  __globals->get_cluster_local_ip(localhost);
  old_replicas = timer->replicas;

  // Calculate whether the new request node is interested in the timer. This
  // updates the replica list in the timer object to be the new replica list
  timer->update_cluster_information();

  int index = 0;
  for (std::vector<std::string>::iterator it = timer->replicas.begin();
                                          it != timer->replicas.end();
                                          ++it, ++index)
  {
    if (*it == request_node)
    {
      timer_is_on_requesting_node = true;
      break;
    }
  }

  return timer_is_on_requesting_node;
}

// The core function in the timer handler, basic principle is to loop around repeatedly
// retrieving timers from the store, waiting until they need to pop and popping them.
//
// If there are no timers in the store at all, we wait forever for one to be added (or
// until we're terminated).  If we are woken while waiting for one set of timers to
// pop, check the timer store to make sure we're holding the nearest timers.
void TimerHandler::run()
{
  std::unordered_set<Timer*> next_timers;

  pthread_mutex_lock(&_mutex);

  _store->fetch_next_timers(next_timers);

  while (!_terminate)
  {
    if (!next_timers.empty())
    {
      TRC_DEBUG("Have a timer to pop");
      _timer_count -= next_timers.size();
      pthread_mutex_unlock(&_mutex);
      pop(next_timers);
      pthread_mutex_lock(&_mutex);
    }
    else
    {
      struct timespec next_pop;
      clock_gettime(CLOCK_MONOTONIC, &next_pop);

      // Work out how long we should wait for (this should be the length of the
      // short wheel bucket - if this crosses a second boundary then set the
      // secs/nsecs appropriately).
      if (next_pop.tv_nsec < (1000 - _store->SHORT_WHEEL_RESOLUTION_MS) * 1000 * 1000)
      {
        // LCOV_EXCL_START - We can't guarantee which of these two paths we go
        // through in UT
        next_pop.tv_nsec += _store->SHORT_WHEEL_RESOLUTION_MS * 1000 * 1000;
        // LCOV_EXCL_STOP
      }
      else
      {
        // LCOV_EXCL_START - We can't guarantee which of these two paths we go
        // through in UT
        next_pop.tv_nsec -= (1000 - _store->SHORT_WHEEL_RESOLUTION_MS) * 1000 * 1000;
        next_pop.tv_sec += 1;
        // LCOV_EXCL_STOP
      }

      int rc = _cond->timedwait(&next_pop);

      if (rc < 0 && rc != ETIMEDOUT)
      {
        // LCOV_EXCL_START
        printf("Failed to wait for condition variable: %s", strerror(errno));
        exit(2);
        // LCOV_EXCL_STOP
      }
    }


    _store->fetch_next_timers(next_timers);
  }


  for (std::unordered_set<Timer*>::iterator it = next_timers.begin();
                                            it != next_timers.end();
                                            ++it)
  {
    delete *it;
  }

  next_timers.clear();

  pthread_mutex_unlock(&_mutex);
}

/*****************************************************************************/
/* PRIVATE FUNCTIONS                                                         */
/*****************************************************************************/

// Pop a set of timers, this function takes ownership of the timers and
// thus empties the passed in set.
void TimerHandler::pop(std::unordered_set<Timer*>& timers)
{
  for (std::unordered_set<Timer*>::iterator it = timers.begin();
                                            it != timers.end();
                                            ++it)
  {
    pop(*it);
  }

  timers.clear();
}

// Pop a specific timer, if required pass the timer on to the replication layer to
// reset the timer for another pop, otherwise destroy the timer record.
void TimerHandler::pop(Timer* timer)
{
  // Tombstones are reaped when they pop.
  if (timer->is_tombstone())
  {
    TRC_DEBUG("Discarding expired tombstone");
    delete timer;
    timer = NULL;
    return;
  }

  // Increment the timer's sequence before sending the callback.
  timer->sequence_number++;

  // Update the timer in case it has out of date configuration
  timer->update_cluster_information();

  // The callback borrows of the timer at this point.
  // cppcheck-suppress uselessAssignmentPtrArg
  _callback->perform(timer); timer = NULL;
}

void TimerHandler::save_tombstone_information(Timer* t, Timer* existing)
{
  if (t->is_tombstone())
  {
    // Learn the interval so that this tombstone lasts long enough to
    // catch errors.
    t->interval_ms = existing->interval_ms;
    t->repeat_for = existing->repeat_for;
  }
}

void TimerHandler::save_site_information(Timer* new_timer, Timer* old_timer)
{
  // Firstly, check if the sites are the same (potentially in a different
  // order). We expect this to be the mainline case, so we always do this
  // cheaper check
  std::vector<std::string> old_timer_sites = old_timer->sites;
  std::vector<std::string> new_timer_sites = new_timer->sites;
  std::sort(old_timer_sites.begin(), old_timer_sites.end());
  std::sort(new_timer_sites.begin(), new_timer_sites.end());

  if (old_timer_sites == new_timer_sites)
  {
    new_timer->sites = old_timer->sites;
    return;
  }

  // The sites aren't the same. We have to check the sites to make sure that
  // the site ordering is retained (which is O(n^2) cost - but this only
  // happens when the sites are added/removed which we expect to be rare).
  std::vector<std::string> site_names;

  // Remove any sites that aren't in the new timer
  for (std::string site: old_timer->sites)
  {
    if (std::find(new_timer->sites.begin(), new_timer->sites.end(), site) !=
        new_timer->sites.end())
    {
      site_names.push_back(site);
    }
    else
    {
      TRC_DEBUG("Removing site (%s) as it no longer exists", site.c_str());
    }
  }

  // Add any new sites that are only in the new timer
  for (std::string site: new_timer->sites)
  {
    if (std::find(site_names.begin(), site_names.end(), site) ==
        site_names.end())
    {
      TRC_DEBUG("Adding remote site (%s) to sites", site.c_str());
      site_names.push_back(site);
    }
  }

  new_timer->sites = site_names;
}

// Report an update to the number of timers to statistics
// This should be called when we remove a timer (by adding an empty map of new
// tags) and when we add a new timer (using an empty map of existing tags) and
// can be used for updating purposes (providing both new and old tags)
void TimerHandler::update_statistics(std::map<std::string, uint32_t> new_tags,
                                     std::map<std::string, uint32_t> old_tags)
{
  if (_tagged_timers_table)
  {
    std::map<std::string, uint32_t> tags_to_add;
    std::map<std::string, uint32_t> tags_to_remove;

    // Calculate difference in supplied maps:
    // Iterate over the old_tags, and decrement for any not in new_tags
    for (std::map<std::string, uint32_t>::const_iterator it = old_tags.begin();
                                                         it != old_tags.end();
                                                         ++it)
    {
      // Check if the old tag is not present in new_tags
      if (new_tags.count(it->first) == 0)
      {
        // Not present in new_tags. Add tags to tags_to_remove
        tags_to_remove[it->first] = it->second;
      }
      // Any tags also present in new_tags are processed below
    }

    // Iterate over new_tags, and calculate correct increment/decrement
    for (std::map<std::string, uint32_t>::const_iterator it = new_tags.begin();
                                                         it != new_tags.end();
                                                         ++it)
    {
      // Check if the new tag is not present in old_tags
      if (old_tags.count(it->first) == 0)
      {
        // Not present in old_tags. Add tags to tags_to_add
        tags_to_add[it->first] = it->second;
      }
      else
      {
        // Pull out old_tags[it->first] to save on processing in each check below
        uint32_t old_tag_count = old_tags[it->first];

        // If new_tag count is greater, add difference to tags_to_add
        if (it->second > old_tag_count)
        {
          tags_to_add[it->first] = (it->second) - old_tag_count;
        }
        // If new_tag count is smaller, add difference to tags_to_remove
        else if (it->second < old_tag_count)
        {
          tags_to_remove[it->first] = old_tag_count - (it->second);
        }
        // If the tag counts are equal, do nothing
      }
    }

    // Increment correct statistics
    for (std::map<std::string, uint32_t>::const_iterator it = tags_to_add.begin();
                                                         it != tags_to_add.end();
                                                         ++it)
    {
      TRC_DEBUG("Incrementing %s by %d", (it->first).c_str(), it->second);
      _scalar_timers_table->increment(it->first, it->second);
      _tagged_timers_table->increment(it->first, it->second);
    }

    // Decrement correct statistics
    for (std::map<std::string, uint32_t>::const_iterator it = tags_to_remove.begin();
                                                         it != tags_to_remove.end();
                                                         ++it)
    {
      TRC_DEBUG("Decrementing %s by %d", (it->first).c_str(), it->second);
      _scalar_timers_table->decrement(it->first, it->second);
      _tagged_timers_table->decrement(it->first, it->second);
    }
  }
}

bool TimerHandler::near_time(uint32_t a, uint32_t b)
{
  return ((a>=b ? (a-b):(b-a)) < NETWORK_DELAY);
}
