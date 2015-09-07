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

#include "globals.h"
#include "timer_handler.h"
#include "log.h"
#include "constants.h"

void* TimerHandler::timer_handler_entry_func(void* arg)
{
  ((TimerHandler*)arg)->run();
  return NULL;
}

TimerHandler::TimerHandler(TimerStore* store,
                           Callback* callback,
                           Replicator* replicator,
                           Alarm* timer_pop_alarm,
                           SNMP::InfiniteTimerCountTable* tagged_timers_table,
                           SNMP::U32Scalar* current_timers_scalar) :
                           _store(store),
                           _callback(callback),
                           _replicator(replicator),
                           _timer_pop_alarm(timer_pop_alarm),
                           _tagged_timers_table(tagged_timers_table),
                           _current_timers_scalar(current_timers_scalar),
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
    printf("Failed to start timer handling thread: %s", strerror(errno));
    exit(2);
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
    TRC_DEBUG("Unlocked");
    pthread_join(_handler_thread, NULL);
    TRC_DEBUG("Joined");
  }
  delete _cond;
  _cond = NULL;

  pthread_mutex_destroy(&_mutex);
  delete _callback;
}

void TimerHandler::add_timer_to_store(Timer* timer)
{
  TimerPair new_tp;
  new_tp.active_timer = timer;

  TimerPair existing_tp;
  bool successful = _store->fetch(timer->id, existing_tp);

  TRC_DEBUG("Performed fetch operation");

  if (successful)
  {
    TRC_DEBUG("Found an existing timer with the same ID");
    Timer* existing = existing_tp.active_timer;

    // Compare timers for precedence, start-time then sequence number.
    if (overflow_less_than(timer->start_time_mono_ms, existing->start_time_mono_ms) ||
        ((timer->start_time_mono_ms == existing->start_time_mono_ms) &&
        (timer->sequence_number < existing->sequence_number)))
    {
      TRC_DEBUG("The timer in the store is more recent, discard the new timer");
      delete timer;
      new_tp.active_timer = existing;
      new_tp.information_timer = existing_tp.information_timer;
    }
    else
    {
      // We want to add the timer, so decide whether this is an update, or
      // if we need to save off the old timer (as an information timer)
      if (existing->cluster_view_id != timer->cluster_view_id)
      {
        // The cluster IDs on the new and existing timers are different.
        // This means that the cluster configuration has changed between
        // then and when the timer was last updated
        TRC_DEBUG("Cluster view IDs are different on the new and existing timers");

        if (existing_tp.information_timer)
        {
          // There's already a saved timer, but the new timer doesn't match the
          // existing timer. This is an error condition, and suggests that
          // a scaling operation has been started before an old scaling operation
          // finished, or there was a node failure during a scaling operation.
          // Either way, the saved timer information is out of date, and is
          // deleted (by not saving a copy of it when we delete the entire Timer
          // ID structure in the next step)
          TRC_WARNING("Deleting out of date timer from timer map");
        }

        set_tombstone_values(timer, existing);

        // Save the old timer information in the list of timers.
        Timer* existing_copy = new Timer(*existing);

        new_tp.active_timer = timer;
        new_tp.information_timer = existing_copy;
      }
      else
      {
        set_tombstone_values(timer, existing);
        new_tp.active_timer = timer;

        if (existing_tp.information_timer)
        {
          Timer* existing_copy = new Timer(*existing_tp.information_timer);
          new_tp.information_timer = existing_copy;
        }
      }
      // Update statistics to reflect update in timers
      //update_statistics(timer->tags, existing->tags);

      // The timer information has already been removed from the timer store,
      // so we delete the reference to the old pair, and insert the new pair
      delete existing_tp.active_timer;
      delete existing_tp.information_timer;
    }
  }
  else
  {
    // This timer is new, so we can update statistics with empty existing
    //update_statistics(timer->tags, std::vector<std::string>());
  }

  TRC_DEBUG("Adding the new timer");
  TimerID id = new_tp.active_timer->id;
  uint32_t next_pop_time = new_tp.active_timer->next_pop_time();
  std::string cluster_view_id = new_tp.active_timer->cluster_view_id;
  std::vector<std::string> cluster_view_id_vector (1, cluster_view_id);
  _store->insert(new_tp, id, next_pop_time, cluster_view_id_vector);
}

void TimerHandler::return_timer_to_store(Timer* timer, bool successful)
{
  if (!successful)
  {
    // In this case, we should update our local statistics, and set the alarm
    //update_statistics(std::vector<std::string>(), timer->tags);

    if (_timer_pop_alarm && timer->is_last_replica())
    {
      _timer_pop_alarm->set();
    }

    delete timer;
    return;
  }

  // We succeeded but may need to tombstone the timer
  if ((timer->sequence_number + 1) * timer->interval_ms > timer->repeat_for)
  {
    // This timer won't pop again, so tombstone it and update statistics
    timer->become_tombstone();
    //update_statistics(std::vector<std::string>(), timer->tags);
  }

  // Replicate and add the timer back to store
  _replicator->replicate(timer);
  add_timer_to_store(timer);

}

void TimerHandler::update_replica_tracker_for_timer(TimerID id,
                                                    int replica_index)
{
  pthread_mutex_lock(&_mutex);
  TimerPair store_timers;
  bool successful = _store->fetch(id, store_timers);

  if (successful)
  {
    Timer* timer;
    bool timer_in_wheel = true;

    TRC_DEBUG("Active timer got for replica: %d", store_timers.active_timer);
    TRC_DEBUG("Information timer got for replica: %d", store_timers.information_timer);

    if (store_timers.information_timer == NULL)
    {
      timer = store_timers.active_timer;
    }
    else
    {
      timer = store_timers.information_timer;
      timer_in_wheel = false;
    }

    TRC_DEBUG("Globals: %d", __globals);

    std::string cluster_view_id;
    __globals->get_cluster_view_id(cluster_view_id);

    if (!timer->is_matching_cluster_view_id(cluster_view_id))
    {
      // The cluster view ID is out of date, so update the tracker.
      int remaining_replicas = timer->update_replica_tracker(replica_index);

      TRC_DEBUG("Checking how many replicas there are left");

      if (remaining_replicas == 0)
      {
        if (!timer_in_wheel)
        {
          // All the new replicas have been told about the timer. We don't
          // need to store the information about the timer anymore.
          delete timer; timer = NULL;
          store_timers.information_timer = NULL;
        }
        else
        {
          // This is a window condition where the node is responsible for an
          // old timer replica. The node knows that all new replicas that
          // should know about the timer are in the process of being told,
          // but it hasn't yet received an update or tombstone for its
          // replica. It will receive this soon.
        }
      }
      else
      {
        uint32_t next_pop_time = store_timers.active_timer->next_pop_time();
        std::vector<std::string> cluster_view_id_vector(1, cluster_view_id);
        _store->insert(store_timers, id, next_pop_time, cluster_view_id_vector);
      }
    }
    else
    {
      uint32_t next_pop_time = store_timers.active_timer->next_pop_time();
      std::vector<std::string> cluster_view_id_vector(1, cluster_view_id);
      _store->insert(store_timers, id, next_pop_time, cluster_view_id_vector);
    }
  }
  TRC_DEBUG("Updated replicas successfully");
  pthread_mutex_unlock(&_mutex);
}

HTTPCode TimerHandler::get_timers_for_node(std::string request_node,
                                           int max_responses,
                                           std::string cluster_view_id,
                                           std::string& get_response)
{
  pthread_mutex_lock(&_mutex);
  std::vector<TimerPair> timers;
  bool finished = _store->get_by_view_id(cluster_view_id, max_responses, timers);

  TRC_DEBUG("Get timers for %s", request_node.c_str());

  // Create the JSON doc for the Timer information
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();

  writer.String(JSON_TIMERS);
  writer.StartArray();

  int retrieved_timers = 0;
  for (std::vector<TimerPair>::iterator it = timers.begin();
                                        it != timers.end();
                                        ++it)
  {
    Timer* timer_copy;
    if (!it->information_timer)
    {
      timer_copy = new Timer(*(it->active_timer));
    }
    else
    {
      timer_copy = new Timer(*(it->information_timer));
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
          // The timer will have a replica on the requesting node. Add this
          // entry to the JSON document

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

    // Break out of the for loop once we hit the maximum number of timers to
    // collect
    if ((max_responses != 0) && (retrieved_timers == max_responses))
    {
      TRC_DEBUG("Reached the max number of timers to collect");
      break;
    }
  }

  writer.EndArray();
  writer.EndObject();

  get_response = sb.GetString();

  TRC_DEBUG("Retrieved %d timers", retrieved_timers);

  pthread_mutex_unlock(&_mutex);

  return ((max_responses != 0) &&
          (!finished)) ? HTTP_PARTIAL_CONTENT : HTTP_OK;
}

bool TimerHandler::timer_is_on_node(std::string request_node,
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
    old_replicas = timer->replicas;

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

// The core function in the timer handler, basic principle is to loop around repeatedly
// retrieving timers from the store, waiting until they need to pop and popping them.
//
// If there are no timers in the store at all, we wait forever for one to be added (or
// until we're terminated).  If we are woken while waiting for one set of timers to
// pop, check the timer store to make sure we're holding the nearest timers.
void TimerHandler::run() {
  std::unordered_set<TimerPair> next_timers;
  std::unordered_set<Timer*>::iterator sample_timer;

  pthread_mutex_lock(&_mutex);

  _store->fetch_next_timers(next_timers);

  while (!_terminate)
  {

    if (!next_timers.empty())
    {
      TRC_DEBUG("Have a timer to pop");
      _timer_count -= next_timers.size();
      //update_statistics(_timer_count);
      pthread_mutex_unlock(&_mutex);
      pop(next_timers);
      pthread_mutex_lock(&_mutex);
    }
    else
    {
      struct timespec next_pop;
      clock_gettime(CLOCK_MONOTONIC, &next_pop);

      if (next_pop.tv_nsec < 790 * 1000 * 1000)
      {
        next_pop.tv_nsec += 10 * 1000 * 1000;
      }
      else
      {
        next_pop.tv_nsec -= 790 * 1000 * 1000;
        next_pop.tv_sec += 1;
      }

      int rc = _cond->timedwait(&next_pop);

      if (rc < 0 && rc != ETIMEDOUT)
      {
        printf("Failed to wait for condition variable: %s", strerror(errno));
        exit(2);
      }
    }


    _store->fetch_next_timers(next_timers);
  }


  for (std::unordered_set<TimerPair>::iterator it = next_timers.begin();
                                               it != next_timers.end();
                                               ++it)
  {
    delete it->active_timer;
    delete it->information_timer;
  }

  next_timers.clear();

  pthread_mutex_unlock(&_mutex);
}

/*****************************************************************************/
/* PRIVATE FUNCTIONS                                                         */
/*****************************************************************************/

// Pop a set of timers, this function takes ownership of the timers and
// thus empties the passed in set.
void TimerHandler::pop(std::unordered_set<TimerPair>& timers)
{
  for (std::unordered_set<TimerPair>::iterator it = timers.begin();
                                            it != timers.end();
                                            ++it)
  {
    pop(it->active_timer);
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
    return;
  }

  // Increment the timer's sequence before sending the callback.
  timer->sequence_number++;

  // Update the timer in case it has out of date configuration
  timer->update_cluster_information();

  // The callback borrows of the timer at this point.
  _callback->perform(timer); timer = NULL;
}

void TimerHandler::set_tombstone_values(Timer* t, Timer* existing)
{
  if (t->is_tombstone())
  {
  // Learn the interval so that this tombstone lasts long enough to
  //  catch errors.
    t->interval_ms = existing->interval_ms;
    t->repeat_for = existing->interval_ms;
  }
}

bool TimerHandler::overflow_less_than(uint32_t a, uint32_t b)
{
  return ((a - b) > ((uint32_t)(1) << 31));
}

// Report an update to the number of timers to statistics
void TimerHandler::update_statistics(std::vector<std::string> new_tags,
                                     std::vector<std::string> old_tags)
{
  if (_tagged_timers_table)
  {
    std::vector<std::string> to_add;
    std::vector<std::string> to_remove;

    std::unordered_set<std::string> set_old_tags(old_tags.begin(), old_tags.end());
    std::unordered_set<std::string> set_new_tags(new_tags.begin(), new_tags.end());

    std::set_difference(set_old_tags.begin(), set_old_tags.end(),
                        set_new_tags.begin(), set_new_tags.end(),
                        std::back_inserter(to_remove));

    std::set_difference(set_new_tags.begin(), set_new_tags.end(),
                        set_old_tags.begin(), set_old_tags.end(),
                        std::back_inserter(to_add));


    for (std::vector<std::string>::iterator it = to_add.begin();
         it != to_add.end();
         ++it)
    {
      _tagged_timers_table->increment(*it);
    }

    for (std::vector<std::string>::iterator it = to_remove.begin();
         it != to_remove.end();
         ++it)
    {
      _tagged_timers_table->decrement(*it);
    }
  }
}
