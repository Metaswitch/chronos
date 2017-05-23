/**
 * @file timer_handler.h
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef TIMER_HANDLER_H__
#define TIMER_HANDLER_H__

#include <pthread.h>

#ifdef UNIT_TEST
#include "pthread_cond_var_helper.h"
#else
#include "cond_var.h"
#endif

#include "timer_store.h"
#include "callback.h"
#include "replicator.h"
#include "gr_replicator.h"
#include "alarm.h"
#include "snmp_continuous_increment_table.h"
#include "snmp_infinite_timer_count_table.h"
#include "snmp_infinite_scalar_table.h"
#include "snmp_scalar.h"

class TimerHandler
{
public:
  TimerHandler(TimerStore*,
               Callback*,
               Replicator*,
               GRReplicator*,
               SNMP::ContinuousIncrementTable*,
               SNMP::InfiniteTimerCountTable*,
               SNMP::InfiniteScalarTable*);
  virtual ~TimerHandler();
  TimerHandler(const TimerHandler& copy) = delete;
  virtual void add_timer(Timer*, bool=true);
  virtual void return_timer(Timer*);
  virtual void handle_successful_callback(TimerID id);
  virtual void handle_failed_callback(TimerID id);
  virtual HTTPCode get_timers_for_node(std::string node,
                                       int max_rsps_with_unique_pop_time,
                                       std::string cluster_view_id,
                                       uint32_t time_from,
                                       std::string& get_response);
  void run();

  friend class TestTimerHandler;

#ifdef UNIT_TEST
  TimerHandler() {}
#endif

private:
  // Constant that specifies timers that are closer than this are considered the
  // same. It should be bigger than the expected network lag
  static const int NETWORK_DELAY = 200;

  void pop(std::unordered_set<Timer*>&);
  void pop(Timer*);

  // Update a timer object with the current cluster configuration. Store off
  // the old set of replicas, and return whether the requesting node is
  // one of the new replicas
  bool timer_is_on_node(std::string request_node,
                        Timer* timer,
                        std::vector<std::string>& old_replicas);

  // Ensure the update to the timer "sticks" by making it last at least as long
  // as the previous timer
  void save_tombstone_information(Timer* timer, Timer* existing);

  // Ensure the update to the timer honours any previous site ordering
  void save_site_information(Timer* new_timer, Timer* old_timer);

  // Report a statistics changed - called with empty maps if a timer has only
  // just been introduced, or is being permanently deleted/tombstoned
  void update_statistics(std::map<std::string, uint32_t> new_tags,
                         std::map<std::string, uint32_t> old_tags);

  // Check to see if these two timestamps are within NETWORK_DELAY of each other
  bool near_time(uint32_t a, uint32_t b);

  TimerStore* _store;
  Callback* _callback;
  Replicator* _replicator;
  GRReplicator* _gr_replicator;
  SNMP::ContinuousIncrementTable* _all_timers_table;
  SNMP::InfiniteTimerCountTable* _tagged_timers_table;
  SNMP::InfiniteScalarTable* _scalar_timers_table;
  SNMP::U32Scalar* _current_timers_scalar;

  pthread_t _handler_thread;
  uint32_t _timer_count;
  std::map<std::string, int> _tag_count = {};
  volatile bool _terminate;
  volatile unsigned int _nearest_new_timer;
  pthread_mutex_t _mutex;

#ifdef UNIT_TEST
  MockPThreadCondVar* _cond;
#else
  CondVar* _cond;
#endif

  static void* timer_handler_entry_func(void *);
};

#endif
