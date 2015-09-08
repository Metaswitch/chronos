/**
 * @file timer_handler.h
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
#include "alarm.h"
#include "snmp_continuous_accumulator_table.h"
#include "snmp_scalar.h"

class TimerHandler
{
public:
  TimerHandler(TimerStore*, Callback*, Replicator*, Alarm*, SNMP::ContinuousAccumulatorTable*, SNMP::U32Scalar*);
  virtual ~TimerHandler();
  virtual void add_timer_to_store(Timer*);
  virtual void return_timer_to_store(Timer*, bool);
  virtual void update_replica_tracker_for_timer(TimerID id,
                                                int replica_index);
  virtual HTTPCode get_timers_for_node(std::string node,
                                       int max_responses,
                                       std::string cluster_view_id,
                                       std::string& get_response);
  void run();

  friend class TestTimerHandler;

#ifdef UNIT_TEST
  TimerHandler() {}
#endif

private:
  void pop(std::unordered_set<TimerPair>&);
  void pop(Timer*);
  bool timer_is_on_node(std::string, std::string, Timer*, std::vector<std::string>&);
  void set_tombstone_values(Timer* timer, Timer* existing);
  bool overflow_less_than(uint32_t a, uint32_t b);
  void update_statistics(uint32_t);

  TimerStore* _store;
  Callback* _callback;
  Replicator* _replicator;
  Alarm* _timer_pop_alarm;
  SNMP::ContinuousAccumulatorTable* _total_timers_table;
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
