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
#include "mock_infinite_table.h"
#include "mock_infinite_scalar_table.h"
#include "mock_increment_table.h"

#include <gtest/gtest.h>

using namespace ::testing;

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TestTimerHandlerFetchAndPop : public Base
{
protected:
  void SetUp()
  {
    // There are fixed points throughout time where things must stay exactly the
    // way they are. Whatever happens here will create its own timeline, its own
    // reality, a temporal tipping point. The future revolves around you, here,
    // now, so do good!
    cwtest_completely_control_time();

    Base::SetUp();
    _store = new MockTimerStore();
    _callback = new MockCallback();
    _replicator = new MockReplicator();
    _mock_tag_table = new MockInfiniteTable();
    _mock_scalar_table = new MockInfiniteScalarTable();
    _mock_increment_table = new MockIncrementTable();
  }

  void TearDown()
  {
    delete _th;
    delete _store;
    delete _replicator;
    delete _mock_tag_table;
    delete _mock_scalar_table;
    delete _mock_increment_table;
    // _callback is deleted by the timer handler.

    Base::TearDown();

    // I always will be. But times change, and so must I... we all change. When
    // you think about it, we are all different people, all through our lives
    // and that's okay, that's good!
    cwtest_reset_time();
  }

  // Accessor functions into the timer handler's private variables
  MockPThreadCondVar* _cond() { return (MockPThreadCondVar*)_th->_cond; }

  MockInfiniteTable* _mock_tag_table;
  MockInfiniteScalarTable* _mock_scalar_table;
  MockIncrementTable* _mock_increment_table;
  MockTimerStore* _store;
  MockCallback* _callback;
  MockReplicator* _replicator;
  TimerHandler* _th;
};


/*****************************************************************************/
/* Instance function tests                                                   */
/*****************************************************************************/

TEST_F(TestTimerHandlerFetchAndPop, StartUpAndShutDown)
{
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));
  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
}

TEST_F(TestTimerHandlerFetchAndPop, PopOneTimer)
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

  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer; timer = NULL;
}

TEST_F(TestTimerHandlerFetchAndPop, PopRepeatedTimer)
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

  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer;
}

TEST_F(TestTimerHandlerFetchAndPop, PopMultipleTimersSimultaneously)
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

  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandlerFetchAndPop, PopMultipleTimersSeries)
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

  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandlerFetchAndPop, PopMultipleRepeatingTimers)
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

  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandlerFetchAndPop, EmptyStore)
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

  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();

  // The first timer has been handled, but the store's now empty.  Pretend the store
  // gained a timer and signal the handler thread.
  _cond()->signal();
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandlerFetchAndPop, LeakTest)
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

  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
}

TEST_F(TestTimerHandlerFetchAndPop, FutureTimerPop)
{
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

  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();

  cwtest_advance_time_ms(100);

  _cond()->signal_timeout();
  _cond()->block_till_waiting();
  delete timer;
}

// Pop a timer that is a tombstone
TEST_F(TestTimerHandlerFetchAndPop, PopTombstoneTimer)
{
  Timer* timer1 = default_timer(1);
  timer1->become_tombstone();

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));
  _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();

  _th->pop(timer1);
  _cond()->block_till_waiting();
}

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TestTimerHandlerAddAndReturn : public Base
{
protected:
  void SetUp()
  {
    // There are fixed points throughout time where things must stay exactly the
    // way they are. Whatever happens here will create its own timeline, its own
    // reality, a temporal tipping point. The future revolves around you, here,
    // now, so do good!
    cwtest_completely_control_time();

    Base::SetUp();
    _store = new MockTimerStore();
    _callback = new MockCallback();
    _replicator = new MockReplicator();
    _mock_tag_table = new MockInfiniteTable();
    _mock_scalar_table = new MockInfiniteScalarTable();
    _mock_increment_table = new MockIncrementTable();

    // Set up the Timer Handler
    EXPECT_CALL(*_store, fetch_next_timers(_)).
                         WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>())).
                         WillOnce(SetArgReferee<0>(std::unordered_set<TimerPair>()));
    _th = new TimerHandler(_store, _callback, _replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
    _cond()->block_till_waiting();
  }

  void TearDown()
  {
    delete _th;
    delete _store;
    delete _replicator;
    delete _mock_tag_table;
    delete _mock_scalar_table;
    delete _mock_increment_table;
    // _callback is deleted by the timer handler.

    Base::TearDown();

    // I always will be. But times change, and so must I... we all change. When
    // you think about it, we are all different people, all through our lives
    // and that's okay, that's good!
    cwtest_reset_time();
  }

  // Accessor functions into the timer handler's private variables
  MockPThreadCondVar* _cond() { return (MockPThreadCondVar*)_th->_cond; }

  MockInfiniteTable* _mock_tag_table;
  MockInfiniteScalarTable* _mock_scalar_table;
  MockIncrementTable* _mock_increment_table;
  MockTimerStore* _store;
  MockCallback* _callback;
  MockReplicator* _replicator;
  TimerHandler* _th;
};

// Tests adding a single timer
TEST_F(TestTimerHandlerAddAndReturn, AddTimer)
{
  // Add the timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  TimerPair insert_pair;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // The timer is successfully added. As it's a new timer it's passed through to
  // the store unchanged.
  EXPECT_EQ(insert_pair.active_timer, timer);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete timer;
}

// Tests updating a timer
TEST_F(TestTimerHandlerAddAndReturn, UpdateTimer)
{
  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  TimerPair insert_pair;

  // Add more tags, to test that update_stats correctly calculates tag changes
  timer->tags["REG"] = 1;
  timer->tags["SUB"] = 5;
  timer->tags["BIND"] = 7;

  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  // Expect all tag increments
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("REG", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("REG", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("SUB", 5)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("SUB", 5)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("BIND", 7)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("BIND", 7)).Times(1);

  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // Update the timer. Make sure the newer timer is picked by giving it a later
  // start time
  Timer* timer2 = default_timer(1);
  timer2->start_time_mono_ms = insert_pair.active_timer->start_time_mono_ms + 100;
  // Add tags of different count values
  timer2->tags["REG"] = 1;
  timer2->tags["SUB"] = 3;
  timer2->tags["BIND"] = 10;

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  // Expect correct tag increments and decrements
  EXPECT_CALL(*_mock_tag_table, decrement("SUB", 2)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("SUB", 2)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("BIND", 3)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("BIND", 3)).Times(1);

  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer2);

  // The timer is successfully updated
  EXPECT_EQ(insert_pair.active_timer, timer2);

  // Update the timer. Make sure the newer timer is picked by giving it a later
  // sequence number
  Timer* timer3 = default_timer(1);
  timer3->sequence_number = 3;
  // Do not add tags, effectively removing tags with this timer
  // Expect decrements accordingly
  EXPECT_CALL(*_mock_tag_table, decrement("REG", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("REG", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("SUB", 3)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("SUB", 3)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("BIND", 10)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("BIND", 10)).Times(1);

  EXPECT_CALL(*_store, fetch(timer3->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer3->id, timer3->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer3);

  // The timer is successfully updated
  EXPECT_EQ(insert_pair.active_timer, timer3);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete timer3;
}

// Tests updating a timer, and having the timers tags change on the update
TEST_F(TestTimerHandlerAddAndReturn, AddExistingTimerChangedTags)
{
  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  TimerPair insert_pair;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // Update the timer. Make sure the newer timer is picked by giving it a later
  // start time.
  Timer* timer2 = default_timer(1);
  timer2->start_time_mono_ms = insert_pair.active_timer->start_time_mono_ms + 100;
  timer2->tags.clear();
  timer2->tags["NEWTAG"]++;
  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_mock_tag_table, increment("NEWTAG", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("NEWTAG", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer2);

  // The timer is successfully updated
  EXPECT_EQ(insert_pair.active_timer, timer2);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete timer2;
}

// Test that if there is already an information timer for this timer
// we overwrite it with a new information timer
TEST_F(TestTimerHandlerAddAndReturn, OverrideInformationTimer)
{
  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  TimerPair insert_pair;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // The first timer has the current cluster view ID and no informational
  // timer
  EXPECT_EQ(insert_pair.active_timer->cluster_view_id, "cluster-view-id");
  ASSERT_TRUE(insert_pair.information_timer == NULL);

  // Change the cluster view ID, and update the timer. This is an update, so the
  // counts/tags don't change. The added timer gains an informational timer
  Timer* timer2 = default_timer(1);
  timer2->cluster_view_id = "updated-cluster-view-id";
  __globals->set_cluster_view_id(timer2->cluster_view_id);

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer2->id, timer2->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer2);

  EXPECT_EQ(insert_pair.active_timer->cluster_view_id, "updated-cluster-view-id");
  EXPECT_EQ(insert_pair.information_timer->cluster_view_id, "cluster-view-id");

  // Change the cluster view ID, and update the timer again. The previous
  // informational timer is discarded.
  Timer* timer3 = default_timer(1);
  timer3->cluster_view_id = "updated-again-cluster-view-id";
  __globals->set_cluster_view_id(timer3->cluster_view_id);

  EXPECT_CALL(*_store, fetch(timer3->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer3->id, timer3->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer3);

  EXPECT_EQ(insert_pair.active_timer->cluster_view_id, "updated-again-cluster-view-id");
  EXPECT_EQ(insert_pair.information_timer->cluster_view_id, "updated-cluster-view-id");

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
  delete insert_pair.information_timer;
}

// Test that attempting to add an older timer doesn't update the stored
// timer
TEST_F(TestTimerHandlerAddAndReturn, AddOlderTimer)
{
  // Set up the timers. Make timer2 older than timer 1. Give them different
  // intervals (so we can easily tell what timer we have)
  Timer* timer = default_timer(1);
  Timer* timer2 = default_timer(1);
  timer->start_time_mono_ms = timer2->start_time_mono_ms + 100;
  timer->interval_ms = 10000;
  timer2->interval_ms = 20000;
  TimerPair insert_pair;

  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // Add an older timer. This doesn't change the stored timer
  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(0);
  EXPECT_CALL(*_store, insert(_, _, _, _)).
                       WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(timer2);

  EXPECT_EQ(insert_pair.active_timer->interval_ms, (unsigned)10000);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// This tests checks when we add a timer to the store and there is already a
// complete TimerPair that exists, and the new timer and the existing active
// have the same cluster view, we should preserve the information timer
TEST_F(TestTimerHandlerAddAndReturn, PreserveInformationTimers)
{
  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  timer->cluster_view_id = "information-timer-view-id";
  TimerPair insert_pair;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // Add the second timer. This causes the first timer to move to an
  // informational timer
  Timer* timer2 = default_timer(1);
  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(timer2);
  EXPECT_EQ(insert_pair.active_timer->cluster_view_id, "cluster-view-id");
  EXPECT_EQ(insert_pair.information_timer->cluster_view_id, "information-timer-view-id");

  // Update the active timer. This shouldn't change the informational timer
  Timer* timer3 = default_timer(1);
  timer3->start_time_mono_ms = insert_pair.active_timer->start_time_mono_ms + 100;
  EXPECT_CALL(*_store, fetch(timer3->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer3->id, timer3->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(timer3);

  EXPECT_EQ(insert_pair.active_timer->cluster_view_id, "cluster-view-id");
  EXPECT_EQ(insert_pair.information_timer->cluster_view_id, "information-timer-view-id");

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
  delete insert_pair.information_timer;
}

// Test that the information timers are preserved when adding a timer if the
// existing timer is re-added (because it is newer)
TEST_F(TestTimerHandlerAddAndReturn, PreserveInformationTimersNoUpdateStartTime)
{
  // Set up the timers. The new timer has a lower sequence number than the
  // existing timer
  Timer* timer_active = default_timer(1);
  Timer* timer_info = default_timer(1);
  timer_info->cluster_view_id = "different-id";
  TimerPair insert_pair;
  insert_pair.active_timer = timer_active;
  insert_pair.information_timer = timer_info;
  Timer* new_timer = default_timer(1);
  timer_active->start_time_mono_ms = new_timer->start_time_mono_ms + 1000;

  // Update the replica tracker. This should only change the information
  // timer
  EXPECT_CALL(*_store, fetch(timer_active->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer_active->id, timer_active->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(new_timer);

  // Check that the informational timer is preserved
  ASSERT_EQ("different-id", insert_pair.information_timer->cluster_view_id);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
  delete insert_pair.information_timer;
}

// Test that the information timers are preserved when adding a timer if the
// existing timer is re-added (because it has a higher sequence number)
TEST_F(TestTimerHandlerAddAndReturn, PreserveInformationTimersNoUpdateSeqNum)
{
  // Set up the timers. The new timer has a lower sequence number than the
  // existing timer
  Timer* timer_active = default_timer(1);
  timer_active->sequence_number = 3;
  Timer* timer_info = default_timer(1);
  timer_info->cluster_view_id = "different-id";
  TimerPair insert_pair;
  insert_pair.active_timer = timer_active;
  insert_pair.information_timer = timer_info;
  Timer* new_timer = default_timer(1);
  new_timer->sequence_number = 2;

  // Update the replica tracker. This should only change the information
  // timer
  EXPECT_CALL(*_store, fetch(timer_active->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer_active->id, timer_active->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(new_timer);

  // Check that the informational timer is preserved
  ASSERT_EQ("different-id", insert_pair.information_timer->cluster_view_id);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
  delete insert_pair.information_timer;
}

// Add a tombstone with the same id as a timer in the store. The new tombstone
// should exist for the same length of time as the original timer
TEST_F(TestTimerHandlerAddAndReturn, AddTombstoneToExisting)
{
  // Set up the timers. Set up the timer to be longer than the tombstone to
  // start with.
  Timer* timer = default_timer(1);
  Timer* tombstone = default_timer(1);
  timer->interval_ms = tombstone->interval_ms * 10;
  timer->repeat_for = tombstone->repeat_for * 10;
  tombstone->tags["NEWTAG"]++;
  tombstone->become_tombstone();
  TimerPair insert_pair;

  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // Now add the tombstone. This should decrement the tags/counts from the
  // removed timer, not from the tombstone tags
  EXPECT_CALL(*_store, fetch(tombstone->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, tombstone->id, _, _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("NEWTAG", 1)).Times(0);
  EXPECT_CALL(*_mock_scalar_table, decrement("NEWTAG", 1)).Times(0);
  _th->add_timer(tombstone);

  // Check that the new tombstone has the correct interval
  EXPECT_EQ(insert_pair.active_timer->interval_ms, (unsigned)1000000);
  EXPECT_EQ(insert_pair.active_timer->repeat_for, (unsigned)1000000);
  EXPECT_TRUE(insert_pair.active_timer->is_tombstone());

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Add a tombstone with a new ID (mimicking the case where a resync can cause deletes
// to be sent to more locations than necessary)
TEST_F(TestTimerHandlerAddAndReturn, AddNewTombstone)
{
  Timer* tombstone = default_timer(1);
  tombstone->become_tombstone();
  TimerPair insert_pair;

  // Add the tombstone - this shouldn't affect the statistics
  EXPECT_CALL(*_store, fetch(tombstone->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(0);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(0);
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(0);
  EXPECT_CALL(*_store, insert(_, tombstone->id, _, _)).
                       WillOnce(SaveArg<0>(&insert_pair));

  _th->add_timer(tombstone);

  // Check that the added timer is the tombstone
  EXPECT_TRUE(insert_pair.active_timer->is_tombstone());

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Test that a new, similar, timer with a sequence number lower than the
// existing timer doesn't override an existing timer
TEST_F(TestTimerHandlerAddAndReturn, AddLowerSequenceNumber)
{
  // Set up the timers
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(1);
  timer1->sequence_number=2;
  timer2->sequence_number=1;
  TimerPair insert_pair;

  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  EXPECT_CALL(*_store, fetch(timer1->id, _)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer1->id, timer1->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer1);
  EXPECT_EQ(insert_pair.active_timer->sequence_number, (unsigned)2);

  // Add a timer with a lower sequence number - this timer should not replace
  // the existing timer
  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer2->id, _, _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer2);

  // Check that the sequence number hasn't changed
  EXPECT_EQ(insert_pair.active_timer->sequence_number, (unsigned)2);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Return a timer to the handler as if it has been passed back from HTTPCallback and will pop again.
TEST_F(TestTimerHandlerAddAndReturn, ReturnTimerWillPopAgain)
{
  Timer* timer = default_timer(1);
  TimerPair insert_pair;

  // The timer is being returned from a callback. This shouldn't change any
  // counts/tags
  EXPECT_CALL(*_store, fetch(timer->id, _)).WillOnce(Return(false));
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->return_timer(timer);

  // The timer is successfully added. As it's a new timer (as the pop would have
  // removed it from the store) it's passed through to the store unchanged.
  EXPECT_EQ(insert_pair.active_timer, timer);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Return a timer to the handler as if it has been passed back from HTTPCallback and wont pop again.
// The timer should be tombstoned before being put back into the store.
TEST_F(TestTimerHandlerAddAndReturn, ReturnTimerWontPopAgain)
{
  Timer* timer = default_timer(1);
  // Set timer up so that it wouldn't pop again.
  timer->sequence_number = 1;
  timer->interval_ms = 100;
  timer->repeat_for = 100;

  TimerPair insert_pair;

  // The timer is being returned from a callback, and won't pop again.
  // This should update statistics, and be returned to the store as a tombstone.
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, fetch(timer->id, _)).WillOnce(Return(false));
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->return_timer(timer);

  // The timer is successfully added. As it's a new timer (as the pop would have
  // removed it from the store) it's passed through to the store unchanged.
  EXPECT_EQ(insert_pair.active_timer, timer);
  EXPECT_TRUE(insert_pair.active_timer->is_tombstone());

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Return a timer to the handler as if it has been passed back from
// HTTPCallback and wont pop again. Give the timer an interval and
// repeat_for value of 0. The timer should be tombstoned before
// being put back into the store.
TEST_F(TestTimerHandlerAddAndReturn, TombstoneZeroIntervalAndRepeatForTimer)
{
  Timer* timer = default_timer(1);
  // Set timer up so that it wouldn't pop again.
  timer->sequence_number = 1;
  timer->interval_ms = 0;
  timer->repeat_for = 0;

  TimerPair insert_pair;

  // The timer is being returned from a callback, and won't pop again.
  // This should update statistics, and be returned to the store as a tombstone.
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, fetch(timer->id, _)).WillOnce(Return(false));
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->return_timer(timer);

  // The timer is successfully added. As it's a new timer (as the pop would have
  // removed it from the store) it's passed through to the store unchanged.
  EXPECT_EQ(insert_pair.active_timer, timer);
  EXPECT_TRUE(insert_pair.active_timer->is_tombstone());

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Test that the handle_callback_success function fetches the timer specified,
// replicates it, and re-inserts it into the store
TEST_F(TestTimerHandlerAddAndReturn, HandleCallbackSuccess)
{
  // Add a timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  Timer* info_timer = default_timer(1);
  TimerPair insert_pair;
  TimerID id = timer->id;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                              WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // The timer is successfully added. As it's a new timer it's passed through to
  // the store unchanged.
  EXPECT_EQ(insert_pair.active_timer, timer);

  timer = NULL;

  // Add an info timer to the pair, to check that the cluster view id vector can be built correctly.
  insert_pair.information_timer = info_timer;

  // Now call handle_successful_callback as if called from http_callback
  EXPECT_CALL(*_store, fetch(id, _)).Times(1).
              WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_replicator, replicate(insert_pair.active_timer));
  EXPECT_CALL(*_store, insert(_, insert_pair.active_timer->id, insert_pair.active_timer->next_pop_time(), _)).
                              WillOnce(SaveArg<0>(&insert_pair));
  _th->handle_successful_callback(id);

  delete insert_pair.active_timer;
  delete insert_pair.information_timer;
}

// Test that the handle_failed_callback function correctly handles updating statistics,
// and then does not put it back into the store.
TEST_F(TestTimerHandlerAddAndReturn, HandleCallbackFailure)
{
  // Add a timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  TimerPair insert_pair;
  TimerID id = timer->id;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                              WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // The timer is successfully added. As it's a new timer it's passed through to
  // the store unchanged.
  EXPECT_EQ(insert_pair.active_timer, timer);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  timer = NULL;

  // Now call handle_failed_callback as if called from http_callback
  EXPECT_CALL(*_store, fetch(id, _)).Times(1).
              WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, insert(_, id, _, _)).Times(0);

  _th->handle_failed_callback(id);
  // Do not delete timer as this is already done in the function
}

// Test that marking some of the replicas as being informed
// doesn't change the timer if it's got an up-to-date
// cluster view ID
TEST_F(TestTimerHandlerAddAndReturn, UpdateReplicaTrackerValueForNewTimer)
{
  // Add the timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  timer->_replica_tracker = 15;
  TimerPair insert_pair;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // Try to update the replica tracker value. This shouldn't change the timer
  EXPECT_CALL(*_store, fetch(timer->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->update_replica_tracker_for_timer(1u, 1);

  // Check that the replica tracker hasn't changed
  EXPECT_EQ(15u, insert_pair.active_timer->_replica_tracker);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Test that marking some of the replicas as being informed
// changes the replica tracker if the cluster view ID is
// different
TEST_F(TestTimerHandlerAddAndReturn, UpdateReplicaTrackerValueForOldActiveTimer)
{
  // Add the timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  timer->_replica_tracker = 15;
  timer->cluster_view_id = "different-id";
  TimerPair insert_pair;
  EXPECT_CALL(*_store, fetch(timer->id, _));
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, insert(_, timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->add_timer(timer);

  // Try to update the replica tracker value. This should change the timer
  EXPECT_CALL(*_store, fetch(timer->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_,timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->update_replica_tracker_for_timer(1u, 3);

  // Check that the tracker value has updated
  ASSERT_EQ(7u, insert_pair.active_timer->_replica_tracker);
  ASSERT_EQ("different-id", insert_pair.active_timer->cluster_view_id);

  // Now mark all replicas as being informed. This mimics a window condition
  // where the timer hasn't been replaced by a new timer/tombstone. The timer
  // should not be deleted in this case
  EXPECT_CALL(*_store, fetch(timer->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_,timer->id, timer->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->update_replica_tracker_for_timer(1u, 0);

  ASSERT_EQ(0u, insert_pair.active_timer->_replica_tracker);
  ASSERT_EQ("different-id", insert_pair.active_timer->cluster_view_id);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Test that marking some of the replicas as being informed
// changes the replica tracker if there's an informational timer
TEST_F(TestTimerHandlerAddAndReturn, UpdateReplicaTrackerValueForInformationTimer)
{
  // Set up the timers
  Timer* timer_active = default_timer(1);
  timer_active->_replica_tracker = 15;
  Timer* timer_info = default_timer(1);
  timer_info->_replica_tracker = 15;
  timer_info->cluster_view_id = "different-id";
  TimerPair insert_pair;
  insert_pair.active_timer = timer_active;
  insert_pair.information_timer = timer_info;

  // Update the replica tracker. This should only change the information
  // timer
  EXPECT_CALL(*_store, fetch(timer_active->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer_active->id, timer_active->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->update_replica_tracker_for_timer(1u, 2);
  ASSERT_EQ(15u, insert_pair.active_timer->_replica_tracker);
  ASSERT_EQ(3u, insert_pair.information_timer->_replica_tracker);

  // Now mark all replicas as being informed. This causes the information timer
  // to be deleted, but doesn't change the active timer.
  EXPECT_CALL(*_store, fetch(timer_active->id, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, timer_active->id, timer_active->next_pop_time(), _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->update_replica_tracker_for_timer(1u, 0);
  ASSERT_EQ(15u, insert_pair.active_timer->_replica_tracker);
  ASSERT_TRUE(insert_pair.information_timer == NULL);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
}

// Test that if there's an out-of-date active timer, that's updated in
// preference to any informational timer.
TEST_F(TestTimerHandlerAddAndReturn, UpdateReplicaTrackerValueForOldActiveTimerWithInfoTimer)
{
  // Set up the timers
  Timer* timer_active = default_timer(1);
  timer_active->_replica_tracker = 15;
  timer_active->cluster_view_id = "different-id";
  Timer* timer_info = default_timer(1);
  timer_info->_replica_tracker = 15;
  timer_info->cluster_view_id = "different-id";
  TimerPair insert_pair;
  insert_pair.active_timer = timer_active;
  insert_pair.information_timer = timer_info;

  // Update the replica tracker. This should update the active timer.
  EXPECT_CALL(*_store, fetch(_, _)).
                       WillOnce(DoAll(SetArgReferee<1>(insert_pair),Return(true)));
  EXPECT_CALL(*_store, insert(_, _, _, _)).
                       WillOnce(SaveArg<0>(&insert_pair));
  _th->update_replica_tracker_for_timer(1u, 0);
  ASSERT_EQ(0u, insert_pair.active_timer->_replica_tracker);
  ASSERT_EQ(15u, insert_pair.information_timer->_replica_tracker);

  // Delete the timers (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_pair.active_timer;
  delete insert_pair.information_timer;
}

// Timer handler tests with a real timer store. This allows better tests of resync
class TestTimerHandlerRealStore : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();
    cwtest_completely_control_time();

    _health_checker = new HealthChecker();
    _store = new TimerStore(_health_checker);
    _callback = new MockCallback();
    _replicator = new MockReplicator();
    _mock_tag_table = new MockInfiniteTable();
    _mock_scalar_table = new MockInfiniteScalarTable();
    _mock_increment_table = new MockIncrementTable();

    _th = new TimerHandler(_store,
                           _callback,
                           _replicator,
                           _mock_increment_table,
                           _mock_tag_table,
                           _mock_scalar_table);
  }

  void TearDown()
  {
    delete _th;
    delete _store;
    delete _health_checker;
    delete _replicator;
    delete _mock_tag_table;
    delete _mock_scalar_table;
    delete _mock_increment_table;
    // _callback is deleted by the timer handler.

    cwtest_reset_time();
    Base::TearDown();
  }

  // Accessor functions into the timer handler's private variables
  MockPThreadCondVar* _cond() { return (MockPThreadCondVar*)_th->_cond; }

  MockInfiniteTable* _mock_tag_table;
  MockInfiniteScalarTable* _mock_scalar_table;
  MockIncrementTable* _mock_increment_table;
  HealthChecker* _health_checker;
  TimerStore* _store;
  MockCallback* _callback;
  MockReplicator* _replicator;
  TimerHandler* _th;
};

// Test that getting timers for a node returns the set of timers
TEST_F(TestTimerHandlerRealStore, GetTimersForNode)
{
  // Add a single timer to the store
  Timer* timer1 = default_timer(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  _th->add_timer(timer1);

  // Now update the current cluster view ID
  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  // There should be one returned timer. We check this by matching the JSON
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 2, updated_cluster_view_id, 0, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":1,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"start-time-delta\".*,\"sequence-number\":0,\"interval\":100,\"repeat-for\":100},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback1\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]},\"statistics\":\\\{\"tag-info\":\\\[\\\{\"type\":\"TAG1\",\"count\":1}]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that if there are no timers for the requesting node,
// that trying to get the timers returns an empty list
TEST_F(TestTimerHandlerRealStore, SelectTimersNoMatchesReqNode)
{
  // Add a single timer to the store
  Timer* timer1 = default_timer(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  _th->add_timer(timer1);

  // Now update the current cluster view ID
  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  // Now just call get_timers_for_node (as if someone had done a resync without
  // changing the cluster configuration). No timers should be returned. Use
  // a maximum timer count of 1, so that if this does pick up the single timer
  // it would return 206, so we can detect that error in this UT as well.
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.4:9999", 1, updated_cluster_view_id, 0, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that getting timers for a node returns the set of timers
// (up to the maximum requested)
TEST_F(TestTimerHandlerRealStore, GetTimersForNodeNoClusterChange)
{
  // Add a single timer to the store
  Timer* timer1 = default_timer(1);
  timer1->interval_ms = 100;
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  _th->add_timer(timer1);

  // Now just call get_timers_for_node (as if someone had done a resync without
  // changing the cluster configuration). No timers should be returned
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 2, "cluster-view-id", 0, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that getting timers for a node returns the set of timers
// (up to the maximum requested)
TEST_F(TestTimerHandlerRealStore, GetTimersForNodeHitMaxResponses)
{
  // Add a single timer to the store
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);

  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(2);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG2", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG2", 1)).Times(1);
  _th->add_timer(timer1);
  _th->add_timer(timer2);

  // Now update the current cluster view ID
  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 1, updated_cluster_view_id, 0, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":2,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"start-time-delta\".*,\"sequence-number\":0,\"interval\":100,\"repeat-for\":100},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback2\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]},\"statistics\":\\\{\"tag-info\":\\\[\\\{\"type\":\"TAG2\",\"count\":1}]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 206);
}

// Test that getting timers for a node returns the set of timers
// (up to the maximum requested)
TEST_F(TestTimerHandlerRealStore, GetTimersForNodeInformationalTimers)
{
  // Add a single timer to the store
  Timer* timer1 = default_timer(1);
  timer1->_replica_tracker = 3;
  timer1->cluster_view_id = "cluster-view-id";

  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  _th->add_timer(timer1);

  // Now update the current cluster view ID
  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  // Update the timer in the store (by making it a tombstone)
  Timer* timer2 = default_timer(1);
  timer2->become_tombstone();
  timer2->cluster_view_id = "updated-cluster-view-id";
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  _th->add_timer(timer2);

  // Now call get_timers_for_node. This returns the informational timer
  // (so there's still a body)
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 2, updated_cluster_view_id, 0, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":1,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"start-time-delta\".*,\"sequence-number\":0,\"interval\":100,\"repeat-for\":100},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback1\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]},\"statistics\":\\\{\"tag-info\":\\\[\\\{\"type\":\"TAG1\",\"count\":1}]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}
