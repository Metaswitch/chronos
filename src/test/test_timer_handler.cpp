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
#include "globals.h"
//#include "snmp_continuous_increment_table.h"
#include "fakesnmp.hpp"

#include <gtest/gtest.h>

using namespace ::testing;

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

static SNMP::U32Scalar _fake_scalar("","");
static SNMP::InfiniteTimerCountTable* _fake_table;
static SNMP::ContinuousIncrementTable* _fake_cont_table;

class TestTimerHandler : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();
    _store = new MockTimerStore();
    _callback = new MockCallback();
    _replicator = new MockReplicator();
    _fake_table = SNMP::InfiniteTimerCountTable::create("","");
    _fake_cont_table = SNMP::ContinuousIncrementTable::create("","");
  }

  void TearDown()
  {
    delete _th;
    delete _store;
    delete _replicator;
    delete _fake_table;
    delete _fake_cont_table;
    // _callback is deleted by the timer handler.

    Base::TearDown();
  }

  // Accessor functions into the timer handler's private variables
  MockPThreadCondVar* _cond() { return (MockPThreadCondVar*)_th->_cond; }

  MockTimerStore* _store;
  MockCallback* _callback;
  MockReplicator* _replicator;
  TimerHandler* _th;
  //SNMP::ContinuousIncrementTable* _fake_cont_table;
};


/*****************************************************************************/
/* Instance function tests                                                   */
/*****************************************************************************/

TEST_F(TestTimerHandler, StartUpAndShutDown)
{
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));
  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  TimerPair pair;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&pair));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  _th->add_timer(timer);

  _cond()->block_till_waiting();

  EXPECT_EQ(pair.active_timer, timer);

  delete timer;
}

TEST_F(TestTimerHandler, AddExistingTimer)
{
  cwtest_completely_control_time();

  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(1);
  TimerPair return_pair;
  return_pair.active_timer = timer1;

  TimerPair insert_pair1;
  TimerPair insert_pair2;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  EXPECT_CALL(*_store, fetch(timer1->id, _)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair1));

  _th->add_timer(timer1);

  EXPECT_EQ(insert_pair1.active_timer, timer1);

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(return_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair2));

  _th->add_timer(timer2);

  EXPECT_EQ(insert_pair2.active_timer, timer2);

  _cond()->block_till_waiting();

  cwtest_reset_time();

  // timer1 is deleted by handler
  delete timer2;
}

// Test that if there is already an information timer for this timer
// we overwrite it with a new information timer
TEST_F(TestTimerHandler, AddExistingTimerPair)
{
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timer2->id = 1;
  timer2->cluster_view_id = "updated-cluster-view-id";
  Timer* new_timer = default_timer(3);
  new_timer->id = 1;
  new_timer->cluster_view_id = "updated-again-cluster-view-id";

  TimerPair return_pair1;
  return_pair1.active_timer = timer1;

  TimerPair return_pair2;
  return_pair2.active_timer = timer1;
  return_pair2.information_timer = timer2;

  TimerPair insert_pair;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  EXPECT_CALL(*_store, fetch(timer1->id, _)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(timer1);

  EXPECT_EQ(insert_pair.active_timer, timer1);

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(return_pair1),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(timer2);

  EXPECT_EQ(insert_pair.active_timer, timer2);
  EXPECT_EQ(insert_pair.information_timer, timer1);

  EXPECT_CALL(*_store, fetch(new_timer->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(return_pair2),Return(true)));
  EXPECT_CALL(*_store, insert(_, new_timer->id, new_timer->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(new_timer);

  _cond()->block_till_waiting();

  delete timer1;
  // timer2 has been deleted by the handler
  delete new_timer;
}


TEST_F(TestTimerHandler, AddOlderTimer)
{
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(1);
  timer1->start_time_mono_ms = timer2->start_time_mono_ms + 100;
  TimerPair return_pair;
  return_pair.active_timer = timer1;

  TimerPair pair1;
  pair1.active_timer = timer1;

  TimerPair insert_pair1;
  TimerPair insert_pair2;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  EXPECT_CALL(*_store, fetch(timer1->id, _)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair1));

  _th->add_timer(timer1);

  EXPECT_EQ(insert_pair1.active_timer, timer1);

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(return_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair2));

  _th->add_timer(timer2);

  EXPECT_EQ(insert_pair2.active_timer, timer1);

  _cond()->block_till_waiting();

  delete timer1;
  // timer2 is deleted by handler
}


// This tests checks when we add a timer to the store and there is already a
// complete TimerPair that exists, and the new timer and the existing active
// have the same cluster view, we should preserve the information timer
TEST_F(TestTimerHandler, PreserveInformationTimers)
{
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timer2->id = 1;
  timer1->cluster_view_id = "information-timer-view-id";

  TimerPair pair1;
  pair1.active_timer = timer1;
  pair1.information_timer = timer2;

  Timer* new_timer = default_timer(3);
  new_timer->id = 1;

  TimerPair return_pair;

  TimerPair insert_pair;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  EXPECT_CALL(*_store, fetch(timer1->id, _)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(timer1);

  return_pair.active_timer = timer1;

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(return_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(timer2);

  EXPECT_EQ(insert_pair.active_timer, timer2);
  EXPECT_EQ(insert_pair.information_timer, timer1);

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(new_timer);

  EXPECT_EQ(insert_pair.active_timer, new_timer);
  EXPECT_EQ(insert_pair.information_timer, timer1);

  _cond()->block_till_waiting();

  delete timer1;
  // timer2 is deleted by handler
  delete new_timer;
}

// Add a tombstone with the same id as a timer in the store. The new tombstone
// should exist for the same length of time as the original timer
TEST_F(TestTimerHandler, AddTombstoneToExisting)
{
  cwtest_completely_control_time();

  Timer* timer1 = default_timer(1);
  Timer* tombstone = default_timer(1);

  timer1->interval_ms = tombstone->interval_ms * 10;
  timer1->repeat_for = tombstone->repeat_for * 10;

  tombstone->start_time_mono_ms = timer1->start_time_mono_ms + 50;
  tombstone->become_tombstone();

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  EXPECT_CALL(*_store, fetch(timer1->id, _)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer1->id, _, _));

  _th->add_timer(timer1);

  TimerPair return_pair;
  return_pair.active_timer = timer1;

  EXPECT_CALL(*_store, fetch(tombstone->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(return_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, tombstone->id, _, _));

  _th->add_timer(tombstone);

  EXPECT_EQ(tombstone->interval_ms, 1000);
  EXPECT_EQ(tombstone->repeat_for, 1000);

  _cond()->block_till_waiting();

  cwtest_reset_time();

  delete tombstone;
  // timer1 is deleted by handler
}

// Pop a timer that is a tombstone
TEST_F(TestTimerHandler, PopTombstoneTimer)
{
  Timer* timer1 = default_timer(1);
  timer1->become_tombstone();

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  _th->pop(timer1);

  _cond()->block_till_waiting();

  // timer1 has been deleted by the handler
}

// Return a timer to the store as if it has been passed back from HTTPCallback
TEST_F(TestTimerHandler, ReturnTimerSuccessful)
{
  Timer* timer1 = default_timer(1);

  TimerPair insert_pair;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  EXPECT_CALL(*_replicator, replicate(timer1));

  EXPECT_CALL(*_store, fetch(timer1->id, _)).WillOnce(Return(false));
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->return_timer(timer1, true);

  EXPECT_EQ(insert_pair.active_timer, timer1);

  _cond()->block_till_waiting();

  delete timer1;
}


// Return a timer to the store as if it has been passed back from HTTPCallback
// but has been dropped (by failing to send it)
TEST_F(TestTimerHandler, ReturnTimerFailure)
{
  Timer* timer1 = default_timer(1);

  TimerPair insert_pair;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  _th->return_timer(timer1, false);

  _cond()->block_till_waiting();

  // timer1 is deleted by timer handler
}

TEST_F(TestTimerHandler, ReturnTimerToTombstone)
{

  Timer* timer1 = default_timer(1);
  timer1->repeat_for = (timer1->sequence_number + 1) * timer1->interval_ms - 100;

  TimerPair insert_pair;

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to fetch_next_timers().
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  EXPECT_CALL(*_replicator, replicate(timer1));

  EXPECT_CALL(*_store, fetch(timer1->id, _)).WillOnce(Return(false));
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
  _cond()->block_till_waiting();

  _th->return_timer(timer1, true);

  EXPECT_EQ(insert_pair.active_timer, timer1);
  EXPECT_TRUE(insert_pair.active_timer->is_tombstone());

  _cond()->block_till_waiting();

  delete timer1;
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);
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

  TimerPair empty_pair;

  TimerPair pair;
  pair.active_timer = timer;

  std::unordered_set<TimerPair> timers;
  timers.insert(pair);

  TimerPair insert_pair;

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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);

  EXPECT_CALL(*_store, fetch(timer->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _));

  _th->add_timer(timer);

  EXPECT_CALL(*_store, fetch(timer->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->update_replica_tracker_for_timer(1u, 1);

  ASSERT_EQ(15u, insert_pair.active_timer->_replica_tracker);

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

  TimerPair empty_pair;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  timer->start_time_mono_ms = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));
  timer->interval_ms = 100;
  timer->repeat_for = 100;

  TimerPair pair;
  pair.active_timer = timer;

  TimerPair insert_pair;

  std::unordered_set<TimerPair> timers;
  timers.insert(pair);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);

  EXPECT_CALL(*_store, fetch(timer->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _));

  _th->add_timer(timer);

  EXPECT_CALL(*_store, fetch(timer->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(pair),Return(true)));
  EXPECT_CALL(*_store, insert(_,timer->id, timer->next_pop_time(), _)).
              WillOnce(SaveArg<0>(&insert_pair));

  _th->update_replica_tracker_for_timer(1u, 3);

  ASSERT_EQ(7u, insert_pair.active_timer->_replica_tracker);
  ASSERT_EQ("different-id", insert_pair.active_timer->cluster_view_id);

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

  std::unordered_set<TimerPair> timers;
  timers.insert(pair1);
  timers.insert(pair2);
  timers.insert(pair3);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);

  EXPECT_CALL(*_store, fetch(timer1->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,timer1->id, timer1->next_pop_time(), _));

  _th->add_timer(timer1);

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_,timer2->id, timer2->next_pop_time(), _));

  _th->add_timer(timer2);

  EXPECT_CALL(*_store, fetch(timer3->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer3->id, timer3->next_pop_time(), _));

  _th->add_timer(timer3);

  std::string get_response;

  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  _th->get_timers_for_node("10.0.0.1:9999", 2, updated_cluster_view_id, get_response);

  // Check the GET has the right format. This is two timers out of the three available (as the
  // max number of timers is set to 2). We're using a simple regex here as we use JSON
  // parsing in the code.
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":1,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"sequence-number\":0,\"interval\":0,\"repeat-for\":0},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback1\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]},\"statistics\":\\\{\"tags\":\\\[]}}},\\\{\"TimerID\":2,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\":.*,\"sequence-number\":0,\"interval\":10,\"repeat-for\":0},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback2\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]},\"statistics\":\\\{\"tags\":\\\[]}}}]}";
  //EXPECT_THAT(get_response, MatchesRegex(exp_rsp));

  std::string cluster_view_id = "cluster-view-id";
  cluster_addresses.push_back("10.0.0.2:9999");
  cluster_addresses.push_back("10.0.0.3:9999");
  __globals->lock();
  __globals->set_cluster_view_id(cluster_view_id);
  __globals->set_cluster_addresses(cluster_addresses);
  __globals->unlock();

  _cond()->block_till_waiting();

  cwtest_reset_time();

  delete timer1;
  delete timer2;
  delete timer3;
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);

  EXPECT_CALL(*_store, fetch(timer1->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _));

  _th->add_timer(timer1);

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _));

  _th->add_timer(timer2);

  EXPECT_CALL(*_store, fetch(timer3->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer3->id, timer3->next_pop_time(), _));

  _th->add_timer(timer3);

  std::unordered_set<TimerPair> next_timers;
  std::string get_response;

  _th->get_timers_for_node("10.0.0.4:9999", 1, "cluster-view-id", get_response);

  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  _cond()->block_till_waiting();

  cwtest_reset_time();

  delete timer1;
  delete timer2;
  delete timer3;
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

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);

  EXPECT_CALL(*_store, fetch(timer1->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _));

  _th->add_timer(timer1);

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _));

  _th->add_timer(timer2);

  EXPECT_CALL(*_store, fetch(timer3->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer3->id, timer3->next_pop_time(), _));

  _th->add_timer(timer3);

  std::string get_response;
  _th->get_timers_for_node("10.0.0.1:9999", 1, "cluster-view-id", get_response);

  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  _cond()->block_till_waiting();

  delete timer1;
  delete timer2;
  delete timer3;
}


// Test that updating a timer with a new cluster ID causes the original
// timer to be saved off.
TEST_F(TestTimerHandler, UpdateClusterViewID)
{
  TimerPair empty_pair;
  TimerPair return_pair;

  // Add the first timer with ID 1
  Timer* timer1 = default_timer(1);
  uint32_t timer1_pop_time = timer1->next_pop_time();
  return_pair.active_timer = timer1;

  TimerPair insert_pair;
  std::vector<std::string> insert_cluster_ids;
  std::vector<std::string> original_cluster_ids = {"updated-cluster-view-id", "cluster-view-id"};

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));

  _th = new TimerHandler(_store, _callback, _replicator, NULL, _fake_cont_table, _fake_table);

  EXPECT_CALL(*_store, fetch(timer1->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(empty_pair),Return(false)));
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _));

  _th->add_timer(timer1);

  // Add a new timer with the same ID, and an updated Cluster View ID
  Timer* timer2 = default_timer(1);
  timer2->id = 1;
  timer2->cluster_view_id = "updated-cluster-view-id";

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(return_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _)).
              WillOnce(DoAll(SaveArg<0>(&insert_pair),SaveArg<3>(&insert_cluster_ids)));

  _th->add_timer(timer2);

  EXPECT_EQ(insert_pair.active_timer, timer2);
  EXPECT_EQ(insert_pair.information_timer->cluster_view_id, "cluster-view-id");
  EXPECT_EQ(insert_pair.information_timer->next_pop_time(), timer1_pop_time);
  EXPECT_EQ(insert_cluster_ids, original_cluster_ids);

  return_pair.active_timer = timer2;
  return_pair.information_timer = timer1;

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(return_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _));

  _th->update_replica_tracker_for_timer(timer2->id, 0);

  _cond()->block_till_waiting();

  delete timer1;
  delete timer2;
}
