/**
 * @file test_timer_handler.cpp
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

#include "timer_helper.h"
#include "pthread_cond_var_helper.h"
#include "mock_timer_store.h"
#include "mock_timer_handler.h"
#include "mock_callback.h"
#include "mock_replicator.h"
#include "base.h"
#include "test_interposer.hpp"
#include "snmp_continuous_accumulator_table.h"
#include "snmp_scalar.h"
#include "globals.h"
//#include "fakesnmp.hpp"

#include "timer_handler.h"

#include <gtest/gtest.h>

using namespace ::testing;

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TestTimerHandler : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();
    _store = new MockTimerStore();
    _callback = new MockCallback();
    _replicator = new MockReplicator();
  }

  void TearDown()
  {
    delete _th;
    delete _store;
    delete _replicator;
    // _callback is deleted by the timer handler.

    Base::TearDown();
  }

  // Accessor functions into the timer handler's private variables
  MockPThreadCondVar* _cond() { return (MockPThreadCondVar*)_th->_cond; }

//  SNMP::ContinuousAccumulatorTable* _fake_table = new SNMP::FakeContinuousAccumulatorTable();
  SNMP::ContinuousAccumulatorTable* _fake_table = NULL;
  SNMP::U32Scalar* _fake_scalar = NULL;

  MockTimerStore* _store;
  MockCallback* _callback;
  MockReplicator* _replicator;
  TimerHandler* _th;
};

/*****************************************************************************/
/* Instance function tests                                                   */
/*****************************************************************************/

TEST_F(TestTimerHandler, StartUpAndShutDown)
{
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));
  _th = new TimerHandler(_store, _callback, _replicator, NULL,  _fake_table, _fake_scalar);
  _cond()->block_till_waiting();
}

TEST_F(TestTimerHandler, PopOneTimer)
{
  std::unordered_set<TimerPair> timers;
  Timer* timer = default_timer(1);
  TimerPair pair;
  pair.active_timer = timer;
  timers.insert(pair);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  EXPECT_CALL(*_callback, perform(timer));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();
  delete timer;
}

TEST_F(TestTimerHandler, PopRepeatedTimer)
{
  std::unordered_set<TimerPair> timers;
  Timer* timer = default_timer(1);
  TimerPair pair;
  pair.active_timer = timer;
  timer->repeat_for = timer->interval_ms * 2;
  timers.insert(pair);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  EXPECT_CALL(*_callback, perform(timer)).Times(2);

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();
  delete timer;
}

TEST_F(TestTimerHandler, PopMultipleTimersSimultaneously)
{
  std::unordered_set<TimerPair> timers;
  Timer* timer1 = default_timer(1);
  TimerPair pair1;
  pair1.active_timer = timer1;
  Timer* timer2 = default_timer(2);
  TimerPair pair2;
  pair2.active_timer = timer2;
  timers.insert(pair1);
  timers.insert(pair2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandler, PopMultipleTimersSeries)
{
  std::unordered_set<TimerPair> timers1;
  std::unordered_set<TimerPair> timers2;
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  TimerPair pair1;
  TimerPair pair2;
  pair1.active_timer = timer1;
  pair2.active_timer = timer2;
  timers1.insert(pair1);
  timers2.insert(pair2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandler, PopMultipleRepeatingTimers)
{
  std::unordered_set<TimerPair> timers1;
  std::unordered_set<TimerPair> timers2;
  Timer* timer1 = default_timer(1);
  timer1->repeat_for = timer1->interval_ms * 2;
  Timer* timer2 = default_timer(2);
  timer2->repeat_for = timer2->interval_ms * 2;
  TimerPair pair1;
  TimerPair pair2;
  pair1.active_timer = timer1;
  pair2.active_timer = timer2;
  timers1.insert(pair1);
  timers2.insert(pair2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  EXPECT_CALL(*_callback, perform(timer1)).Times(2);
  EXPECT_CALL(*_callback, perform(timer2)).Times(2);

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandler, EmptyStore)
{
  std::unordered_set<TimerPair> timers1;
  std::unordered_set<TimerPair> timers2;
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  TimerPair pair1;
  TimerPair pair2;
  pair1.active_timer = timer1;
  pair2.active_timer = timer2;
  timers1.insert(pair1);
  timers2.insert(pair2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();

  // The first timer has been handled, but the store's now empty.  Pretend the store
  // gained a timer and signal the handler thread.
  _cond()->signal();
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandler, AddTimer)
{
  Timer* timer = default_timer(1);
  //TimerPair pair;
  //pair.active_timer = timer;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));
  EXPECT_CALL(*_store, fetch(_, _)).Times(1);
  EXPECT_CALL(*_store, insert(_, _, _, _)).Times(1);

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();

  _th->add_timer_to_store(timer);

  _cond()->block_till_waiting();

  delete timer;
}

TEST_F(TestTimerHandler, LeakTest)
{
  std::unordered_set<TimerPair> timers;
  Timer* timer = default_timer(1);
  TimerPair pair;
  pair.active_timer = timer;
  timers.insert(pair);

  // Make sure that the final call to fetch_next_timers actually returns some.  This
  // test should still pass valgrind's checking without leaking the timer.
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(timers));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();
}

TEST_F(TestTimerHandler, FutureTimerPop)
{
  // There are fixed points throughout time where things must stay exactly the
  // way they are. Whatever happens here will create its own timeline, its own
  // reality, a temporal tipping point. The future revolves around you, here,
  // now, so do good!
  cwtest_completely_control_time();

  Timer* timer = default_timer(1);
  TimerPair pair;
  pair.active_timer = timer;
  timer->interval_ms = 100;
  timer->repeat_for = 100;

  // Start the timer right now.
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  timer->start_time_mono_ms = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));

  // Since we only allocates timers on millisecond intervals, round the
  // time down to a millisecond.
  ts.tv_nsec = ts.tv_nsec - (ts.tv_nsec % (1000 * 1000));

  std::unordered_set<TimerPair> timers;
  timers.insert(pair);

  // After the timer pops, we'd expect to get a call back to get the next set of timers.
  // Then the standard one more check during termination.
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));
  EXPECT_CALL(*_callback, perform(_));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);
  _cond()->block_till_waiting();

  cwtest_advance_time_ms(100);

  _cond()->signal_timeout();
  _cond()->block_till_waiting();
  delete timer;

  // I always will be. But times change, and so must I... we all change. When
  // you think about it, we are all different people, all through our lives
  // and that's okay, that's good!
  cwtest_reset_time();
}

// Test that marking some of the replicas as being informed
// doesn't change the timer if it's got an up-to-date
// cluster view ID
TEST_F(TestTimerHandler, UpdateReplicaTrackerValueForNewTimer)
{
  cwtest_completely_control_time();

  Timer* timer = default_timer(1);

  TimerPair pair;
  pair.active_timer = timer;

  std::unordered_set<TimerPair> timers;
  timers.insert(pair);

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  timer->start_time_mono_ms = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));
  timer->interval_ms = 100;
  timer->repeat_for = 100;

  // Since we only allocates timers on millisecond intervals, round the
  // time down to a millisecond.
  ts.tv_nsec = ts.tv_nsec - (ts.tv_nsec % (1000 * 1000));

  timer->_replica_tracker = 15;

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);

  EXPECT_CALL(*_store, fetch(_, _)).
                       WillOnce(DoAll(SetArgReferee<1>(pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer);

  EXPECT_CALL(*_store, fetch(_, _)).
                       WillOnce(DoAll(SetArgReferee<1>(pair),Return(true)));

  _th->update_replica_tracker_for_timer(1u, 1);

  ASSERT_EQ(15u, timer->_replica_tracker);

  delete timer;

  _cond()->block_till_waiting();

  cwtest_reset_time();
}

// Test that marking some of the replicas as being informed
// changes the replica tracker if the cluster view ID is
// different
TEST_F(TestTimerHandler, UpdateReplicaTrackerValueForOldTimer)
{
  cwtest_completely_control_time();

  Timer* timer = default_timer(1);
  timer->_replica_tracker = 15;
  timer->cluster_view_id = "different-id";

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  timer->start_time_mono_ms = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));
  timer->interval_ms = 100;
  timer->repeat_for = 100;

  TimerPair pair;
  pair.active_timer = timer;

  std::unordered_set<TimerPair> timers;
  timers.insert(pair);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);

  EXPECT_CALL(*_store, fetch(_, _)).
                       WillOnce(DoAll(SetArgReferee<1>(pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer);

  EXPECT_CALL(*_store, fetch(_,_)).
                       WillOnce(DoAll(SetArgReferee<1>(pair),Return(true)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->update_replica_tracker_for_timer(1u, 3);

  ASSERT_EQ(7u, timer->_replica_tracker);

  delete timer;

  _cond()->block_till_waiting();

  cwtest_reset_time();
}


// Test that getting timers for a node returns the set of timers
// (up to the maximum requested)
TEST_F(TestTimerHandler, SelectTimers)
{
  cwtest_completely_control_time();

  Timer* timer1 = default_timer(1);
  timer1->interval_ms = 100;
  Timer* timer2 = default_timer(2);
  timer2->interval_ms = 10000 + 200;
  Timer* timer3 = default_timer(3);
  timer3->interval_ms = (3600 * 1000) + 300;

  TimerPair empty_pair;

  TimerPair pair1;
  pair1.active_timer = timer1;
  TimerPair pair2;
  pair2.active_timer = timer2;
  TimerPair pair3;
  pair3.active_timer = timer3;

  std::vector<TimerPair> timers;
  timers.push_back(pair1);
  timers.push_back(pair2);
  timers.push_back(pair3);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);

  EXPECT_CALL(*_store, fetch(1, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,1,_,_));

  _th->add_timer_to_store(timer1);

  EXPECT_CALL(*_store, fetch(2, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,2,_,_));

  _th->add_timer_to_store(timer2);

  EXPECT_CALL(*_store, fetch(3, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,3,_,_));

  _th->add_timer_to_store(timer3);

  std::string get_response;

  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  EXPECT_CALL(*_store, get_by_view_id(_, _,_)).
                       WillOnce(DoAll(SetArgReferee<2>(timers),Return(false)));

  _th->get_timers_for_node("10.0.0.1:9999", 2, updated_cluster_view_id, get_response);

  // Check the GET has the right format. This is two timers out of the three available (as the
  // max number of timers is set to 2). We're using a simple regex here as we use JSON
  // parsing in the code.
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":1,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"sequence-number\":0,\"interval\":0,\"repeat-for\":0},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback1\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]},\"statistics\":\\\{\"tags\":\\\[]}}},\\\{\"TimerID\":2,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\":.*,\"sequence-number\":0,\"interval\":10,\"repeat-for\":0},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback2\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]},\"statistics\":\\\{\"tags\":\\\[]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));

  std::string cluster_view_id = "cluster-view-id";
  cluster_addresses.push_back("10.0.0.2:9999");
  cluster_addresses.push_back("10.0.0.3:9999");
  __globals->lock();
  __globals->set_cluster_view_id(cluster_view_id);
  __globals->set_cluster_addresses(cluster_addresses);
  __globals->unlock();

  _cond()->block_till_waiting();

  cwtest_reset_time();
}


// Test that if there are no timers for the requesting node,
// that trying to get the timers returns an empty list
TEST_F(TestTimerHandler, SelectTimersTakeInformationalTimers)
{
  cwtest_completely_control_time();

  std::unordered_set<TimerPair> next_timers;

  Timer* timer1 = default_timer(1);
  timer1->interval_ms = 100;
  timer1->cluster_view_id = "old-cluster-view-id";

  TimerPair empty_pair;

  TimerPair pair1;
  pair1.active_timer = timer1;

  Timer* timer2 = default_timer(2);
  timer2->id = 1;
  timer2->interval_ms = 1000 + 200;

  TimerPair pair2;
  pair2.active_timer = timer2;

  std::vector<TimerPair> timers;
  timers.push_back(pair2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);

  // Add a timer to the store, then update it with a new cluster view ID.

  EXPECT_CALL(*_store, fetch(1, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,1,_,"old-cluster-view-id"));

  _th->add_timer_to_store(timer1);

  EXPECT_CALL(*_store, fetch(1, _)).
                       WillOnce(DoAll(SetArgReferee<1>(pair1),Return(true)));
  EXPECT_CALL(*_store, insert(_,1,_,"cluster-view-id"));

  _th->add_timer_to_store(timer2);

  std::string get_response;

  EXPECT_CALL(*_store, get_by_view_id("cluster-view-id", 1, _)).
                       WillOnce(DoAll(SetArgReferee<2>(timers),Return(false)));

  _th->get_timers_for_node("10.0.0.3:9999", 1, "cluster-view-id", get_response);

  // Check that the response is based on the informational timer, rather than the timer
  // in the timer wheel (the uri should be callback1 rather than callback2)
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":1,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"sequence-number\":0,\"interval\":0,\"repeat-for\":0},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback1\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"cluster-view-id\",\"replicas\":\\\[\"10.0.0.3:9999\"]},\"statistics\":\\\{\"tags\":\\\[]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));

  // timer1 was deleted by update;
  delete timer2;

  _cond()->block_till_waiting();

  cwtest_reset_time();
}

// Test that if there are no timers for the requesting node,
// that trying to get the timers returns an empty list
TEST_F(TestTimerHandler, SelectTimersNoMatchesReqNode)
{
  cwtest_completely_control_time();

  TimerPair empty_pair;

  Timer* timer1 = default_timer(1);
  timer1->interval_ms = 100;
  Timer* timer2 = default_timer(2);
  timer2->interval_ms = 1000 + 200;
  Timer* timer3 = default_timer(3);
  timer3->interval_ms = (3600 * 1000) + 300;

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);

  EXPECT_CALL(*_store, fetch(1, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer1);

  EXPECT_CALL(*_store, fetch(2, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer2);

  EXPECT_CALL(*_store, fetch(3, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer3);

  std::unordered_set<TimerPair> next_timers;
  std::string get_response;

  EXPECT_CALL(*_store, get_by_view_id("cluster-view-id", 1, _)).
                       WillOnce(Return(false));

  _th->get_timers_for_node("10.0.0.4:9999", 1, "cluster-view-id", get_response);

  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  _cond()->block_till_waiting();

  cwtest_reset_time();
}


// Test that if there are no timers with an out of date cluster
// ID then trying to get the timers returns an empty list
TEST_F(TestTimerHandler, SelectTimersNoMatchesClusterID)
{
  TimerPair empty_pair;

  Timer* timer1 = default_timer(1);
  timer1->interval_ms = 100;
  Timer* timer2 = default_timer(2);
  timer2->interval_ms = 1000 + 200;
  Timer* timer3 = default_timer(3);
  timer3->interval_ms = (3600 * 1000) + 300;

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);

  EXPECT_CALL(*_store, fetch(1, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer1);

  EXPECT_CALL(*_store, fetch(2, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer2);

  EXPECT_CALL(*_store, fetch(3, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer3);

  EXPECT_CALL(*_store, get_by_view_id("cluster-view-id", 1, _)).
                       WillOnce(Return(false));

  std::string get_response;
  _th->get_timers_for_node("10.0.0.1:9999", 1, "cluster-view-id", get_response);

  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  _cond()->block_till_waiting();
}


// Test that updating a timer with a new cluster ID causes the original
// timer to be saved off.
//
// WARNING: In this test we look directly in the timer store as there's no
// other way to test what's in the timer map (when it's not also in the timer
// wheel)
TEST_F(TestTimerHandler, UpdateClusterViewID)
{
  TimerPair empty_pair;

  // Add the first timer with ID 1
  Timer* timer1 = default_timer(1);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_table, _fake_scalar);

  EXPECT_CALL(*_store, fetch(1, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer1);

  // Add a new timer with the same ID, and an updated Cluster View ID
  Timer* timer2 = default_timer(1);
  timer2->cluster_view_id = "updated-cluster-view-id";

  EXPECT_CALL(*_store, fetch(1, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,_,_,_));

  _th->add_timer_to_store(timer2);

  _cond()->block_till_waiting();
}
