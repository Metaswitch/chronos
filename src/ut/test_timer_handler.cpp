/**
 * @file test_timer_handler.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "timer_helper.h"
#include "pthread_cond_var_helper.h"
#include "mock_timer_store.h"
#include "mock_timer_handler.h"
#include "mock_callback.h"
#include "mock_replicator.h"
#include "mock_gr_replicator.h"
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
    _gr_replicator = new MockGRReplicator();
    _mock_tag_table = new MockInfiniteTable();
    _mock_scalar_table = new MockInfiniteScalarTable();
    _mock_increment_table = new MockIncrementTable();
  }

  void TearDown()
  {
    delete _th;
    delete _store;
    delete _replicator;
    delete _gr_replicator;
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
  MockGRReplicator* _gr_replicator;
  TimerHandler* _th;
};


/*****************************************************************************/
/* Instance function tests                                                   */
/*****************************************************************************/

TEST_F(TestTimerHandlerFetchAndPop, StartUpAndShutDown)
{
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));
  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
}

TEST_F(TestTimerHandlerFetchAndPop, PopOneTimer)
{
  std::unordered_set<Timer*> timers;
  Timer* timer = default_timer(1);
  timers.insert(timer);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer));

  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer; timer = NULL;
}

TEST_F(TestTimerHandlerFetchAndPop, PopRepeatedTimer)
{
  std::unordered_set<Timer*> timers;
  Timer* timer = default_timer(1);
  timer->repeat_for = timer->interval_ms * 2;
  timers.insert(timer);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer)).Times(2);

  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer;
}

TEST_F(TestTimerHandlerFetchAndPop, PopMultipleTimersSimultaneously)
{
  std::unordered_set<Timer*> timers;
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timers.insert(timer1);
  timers.insert(timer2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandlerFetchAndPop, PopMultipleTimersSeries)
{
  std::unordered_set<Timer*> timers1;
  std::unordered_set<Timer*> timers2;
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timers1.insert(timer1);
  timers2.insert(timer2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandlerFetchAndPop, PopMultipleRepeatingTimers)
{
  std::unordered_set<Timer*> timers1;
  std::unordered_set<Timer*> timers2;
  Timer* timer1 = default_timer(1);
  timer1->repeat_for = timer1->interval_ms * 2;
  Timer* timer2 = default_timer(2);
  timer2->repeat_for = timer2->interval_ms * 2;
  timers1.insert(timer1);
  timers2.insert(timer2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer1)).Times(2);
  EXPECT_CALL(*_callback, perform(timer2)).Times(2);

  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandlerFetchAndPop, EmptyStore)
{
  std::unordered_set<Timer*> timers1;
  std::unordered_set<Timer*> timers2;
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timers1.insert(timer1);
  timers2.insert(timer2);

  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
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
  std::unordered_set<Timer*> timers;
  Timer* timer = default_timer(1);
  timers.insert(timer);

  // Make sure that the final call to fetch_next_timers actually returns some.  This
  // test should still pass valgrind's checking without leaking the timer.
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(timers));

  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
  _cond()->block_till_waiting();
}

TEST_F(TestTimerHandlerFetchAndPop, FutureTimerPop)
{
  Timer* timer = default_timer(1);
  timer->interval_ms = 100;
  timer->repeat_for = 100;

  // Start the timer right now.
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  timer->start_time_mono_ms = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));

  // Since we only allocates timers on millisecond intervals, round the
  // time down to a millisecond.
  ts.tv_nsec = ts.tv_nsec - (ts.tv_nsec % (1000 * 1000));

  std::unordered_set<Timer*> timers;
  timers.insert(timer);

  // After the timer pops, we'd expect to get a call back to get the next set of timers.
  // Then the standard one more check during termination.
  EXPECT_CALL(*_store, fetch_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));
  EXPECT_CALL(*_callback, perform(_));

  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
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
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));
  _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
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
    _gr_replicator = new MockGRReplicator();
    _mock_tag_table = new MockInfiniteTable();
    _mock_scalar_table = new MockInfiniteScalarTable();
    _mock_increment_table = new MockIncrementTable();

    // Set up the Timer Handler
    EXPECT_CALL(*_store, fetch_next_timers(_)).
                         WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                         WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));
    _th = new TimerHandler(_store, _callback, _replicator, _gr_replicator, _mock_increment_table, _mock_tag_table, _mock_scalar_table);
    _cond()->block_till_waiting();
  }

  void TearDown()
  {
    delete _th;
    delete _store;
    delete _replicator;
    delete _gr_replicator;
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
  MockGRReplicator* _gr_replicator;
  TimerHandler* _th;
};

// Tests adding a single timer
TEST_F(TestTimerHandlerAddAndReturn, AddTimer)
{
  // Add the timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  Timer* insert_timer;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // The timer is successfully added. As it's a new timer it's passed through to
  // the store unchanged.
  EXPECT_EQ(insert_timer, timer);

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
  Timer* insert_timer;

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
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // Update the timer. Make sure the newer timer is picked by giving it a later
  // start time
  Timer* timer2 = default_timer(1);
  timer2->start_time_mono_ms = insert_timer->start_time_mono_ms + 100;
  // Add tags of different count values
  timer2->tags["REG"] = 1;
  timer2->tags["SUB"] = 3;
  timer2->tags["BIND"] = 10;

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(SetArgPointee<1>(insert_timer));
  // Expect correct tag increments and decrements
  EXPECT_CALL(*_mock_tag_table, decrement("SUB", 2)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("SUB", 2)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("BIND", 3)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("BIND", 3)).Times(1);

  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer2, 0);

  // The timer is successfully updated
  EXPECT_EQ(insert_timer, timer2);

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
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer3, 0);

  // The timer is successfully updated
  EXPECT_EQ(insert_timer, timer3);

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
  Timer* insert_timer;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // Update the timer. Make sure the newer timer is picked by giving it a later
  // start time.
  Timer* timer2 = default_timer(1);
  timer2->start_time_mono_ms = insert_timer->start_time_mono_ms + 100;
  timer2->tags.clear();
  timer2->tags["NEWTAG"]++;
  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_mock_tag_table, increment("NEWTAG", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("NEWTAG", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer2, 0);

  // The timer is successfully updated
  EXPECT_EQ(insert_timer, timer2);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete timer2;
}

// Tests updating a timer, and having the sites change on the update
TEST_F(TestTimerHandlerAddAndReturn, UpdateTimerChangeSites)
{
  // Add the first timer.
  Timer* timer = default_timer(1);
  timer->tags.clear();
  timer->sites.push_back("remote_site_2_name");
  Timer* insert_timer;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  ASSERT_EQ(insert_timer->sites.size(), 3);
  EXPECT_EQ(insert_timer->sites[0], "local_site_name");
  EXPECT_EQ(insert_timer->sites[1], "remote_site_1_name");
  EXPECT_EQ(insert_timer->sites[2], "remote_site_2_name");

  // Update the site information. Make sure the newer timer is picked by
  // giving it a later start time.
  Timer* timer2 = default_timer(1);
  timer2->start_time_mono_ms = insert_timer->start_time_mono_ms + 100;
  timer2->tags.clear();
  timer2->sites.clear();
  timer2->sites.push_back("remote_site_4_name");
  timer2->sites.push_back("remote_site_1_name");
  timer2->sites.push_back("local_site_name");
  timer2->sites.push_back("remote_site_3_name");

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer2, 0);

  // The timer is successfully updated, and the site ordering uses the existing
  // site ordering for any existing sites
  ASSERT_EQ(insert_timer->sites.size(), 4);
  EXPECT_EQ(insert_timer->sites[0], "local_site_name");
  EXPECT_EQ(insert_timer->sites[1], "remote_site_1_name");
  EXPECT_EQ(insert_timer->sites[2], "remote_site_4_name");
  EXPECT_EQ(insert_timer->sites[3], "remote_site_3_name");

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete timer2;
}

// Tests updating a timer, and having the new timer have the sites in a
// different order. Check that the existing order is maintained.
TEST_F(TestTimerHandlerAddAndReturn, UpdateTimerChangeSiteOrdering)
{
  // Add the first timer.
  Timer* timer = default_timer(1);
  timer->tags.clear();
  timer->sites.push_back("remote_site_2_name");
  Timer* insert_timer;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  ASSERT_EQ(insert_timer->sites.size(), 3);
  EXPECT_EQ(insert_timer->sites[0], "local_site_name");
  EXPECT_EQ(insert_timer->sites[1], "remote_site_1_name");
  EXPECT_EQ(insert_timer->sites[2], "remote_site_2_name");

  // Update the site information. Make sure the newer timer is picked by
  // giving it a later start time.
  Timer* timer2 = default_timer(1);
  timer2->start_time_mono_ms = insert_timer->start_time_mono_ms + 100;
  timer2->tags.clear();
  timer2->sites.clear();
  timer2->sites.push_back("remote_site_2_name");
  timer2->sites.push_back("remote_site_1_name");
  timer2->sites.push_back("local_site_name");

  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer2, 0);

  // The timer is successfully updated, and the site ordering uses the existing
  // site ordering
  ASSERT_EQ(insert_timer->sites.size(), 3);
  EXPECT_EQ(insert_timer->sites[0], "local_site_name");
  EXPECT_EQ(insert_timer->sites[1], "remote_site_1_name");
  EXPECT_EQ(insert_timer->sites[2], "remote_site_2_name");

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete timer2;
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
  Timer* insert_timer;

  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // Add an older timer. This doesn't change the stored timer
  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(0);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));

  _th->add_timer(timer2, 0);

  EXPECT_EQ(insert_timer->interval_ms, (unsigned)10000);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_timer;
}

// Test that adding a timer with an up-to-date cluster ID always wins
// over an existing timer
TEST_F(TestTimerHandlerAddAndReturn, AddUpToDateTimer)
{
  // Set up the timers. Make timer2 older than timer 1. Give them different
  // intervals (so we can easily tell what timer we have).
  Timer* timer = default_timer(1);
  Timer* timer2 = default_timer(1);
  timer->start_time_mono_ms = timer2->start_time_mono_ms + 100;
  timer->interval_ms = 10000;
  timer2->interval_ms = 20000;
  timer2->cluster_view_id = "updated-cluster-view-id";
  Timer* insert_timer;

  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // Now update the current cluster view ID
  std::string updated_cluster_view_id = "updated-cluster-view-id";
  __globals->lock();
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  // Now add the older timer. This wouldn't normally update the timer, but
  // it does because the cluster ID is up to date.
  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(0);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));

  _th->add_timer(timer2, 0);

  // Check that the timer time was updated.
  EXPECT_EQ(insert_timer->interval_ms, (unsigned)20000);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_timer;
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
  Timer* insert_timer;

  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // Now add the tombstone. This should decrement the tags/counts from the
  // removed timer, not from the tombstone tags
  EXPECT_CALL(*_store, fetch(tombstone->id, _)).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("NEWTAG", 1)).Times(0);
  EXPECT_CALL(*_mock_scalar_table, decrement("NEWTAG", 1)).Times(0);
  _th->add_timer(tombstone, 0);

  // Check that the new tombstone has the correct interval
  EXPECT_EQ(insert_timer->interval_ms, (unsigned)1000000);
  EXPECT_EQ(insert_timer->repeat_for, (unsigned)1000000);
  EXPECT_TRUE(insert_timer->is_tombstone());

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_timer;
}

// Add a tombstone with a new ID (mimicking the case where a resync can cause deletes
// to be sent to more locations than necessary)
TEST_F(TestTimerHandlerAddAndReturn, AddNewTombstone)
{
  Timer* tombstone = default_timer(1);
  tombstone->become_tombstone();
  Timer* insert_timer;

  // Add the tombstone - this shouldn't affect the statistics
  EXPECT_CALL(*_store, fetch(tombstone->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(0);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(0);
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(0);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));

  _th->add_timer(tombstone, 0);

  // Check that the added timer is the tombstone
  EXPECT_TRUE(insert_timer->is_tombstone());

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_timer;
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
  Timer* insert_timer;

  // Add the first timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  EXPECT_CALL(*_store, fetch(timer1->id, _)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer1, 0);
  EXPECT_EQ(insert_timer->sequence_number, (unsigned)2);

  // Add a timer with a lower sequence number - this timer should not replace
  // the existing timer
  EXPECT_CALL(*_store, fetch(timer2->id, _)).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer2, 0);

  // Check that the sequence number hasn't changed
  EXPECT_EQ(insert_timer->sequence_number, (unsigned)2);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_timer;
}

// Return a timer to the handler as if it has been passed back from HTTPCallback and will pop again.
TEST_F(TestTimerHandlerAddAndReturn, ReturnTimerWillPopAgain)
{
  Timer* timer = default_timer(1);
  Timer* insert_timer;

  // The timer is being returned from a callback. This shouldn't change any
  // counts/tags
  EXPECT_CALL(*_store, fetch(timer->id, _));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->return_timer(timer);

  // The timer is successfully added. As it's a new timer (as the pop would have
  // removed it from the store) it's passed through to the store unchanged.
  EXPECT_EQ(insert_timer, timer);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_timer;
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

  Timer* insert_timer;

  // The timer is being returned from a callback, and won't pop again.
  // This should update statistics, and be returned to the store as a tombstone.
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, fetch(timer->id, _));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->return_timer(timer);

  // The timer is successfully added. As it's a new timer (as the pop would have
  // removed it from the store) it's passed through to the store unchanged.
  EXPECT_EQ(insert_timer, timer);
  EXPECT_TRUE(insert_timer->is_tombstone());

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_timer;
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

  Timer* insert_timer;

  // The timer is being returned from a callback, and won't pop again.
  // This should update statistics, and be returned to the store as a tombstone.
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, fetch(timer->id, _));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->return_timer(timer);

  // The timer is successfully added. As it's a new timer (as the pop would have
  // removed it from the store) it's passed through to the store unchanged.
  EXPECT_EQ(insert_timer, timer);
  EXPECT_TRUE(insert_timer->is_tombstone());

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  delete insert_timer;
}

// Test that the handle_callback_success function fetches the timer specified,
// replicates it, and re-inserts it into the store
TEST_F(TestTimerHandlerAddAndReturn, HandleCallbackSuccess)
{
  // Add a timer. This is a new timer, so should cause the stats to
  // increment (counts and tags).
  Timer* timer = default_timer(1);
  TimerID id = timer->id;
  Timer* insert_timer;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // The timer is successfully added. As it's a new timer it's passed through to
  // the store unchanged.
  EXPECT_EQ(insert_timer, timer);
  timer = NULL;

  // Now call handle_successful_callback as if called from http_callback
  EXPECT_CALL(*_store, fetch(id, _)).Times(1).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_replicator, replicate(insert_timer));
  EXPECT_CALL(*_gr_replicator, replicate(insert_timer));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->handle_successful_callback(id);

  delete insert_timer;
}

// Test that the handle_callback_success function updates the site information
// correctly
TEST_F(TestTimerHandlerAddAndReturn, HandleCallbackSuccessSiteChanges)
{
  // Add a timer. Clear out any tags as we don't care about them for this test
  Timer* timer = default_timer(1);
  timer->tags.clear();
  Timer* insert_timer;
  TimerID id = timer->id;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // The timer is successfully added. As it's a new timer it's passed through to
  // the store unchanged.
  EXPECT_EQ(insert_timer, timer);

  timer = NULL;

  // Change the local and remote sites (so we can check that the sites are
  // updated correctly)
  std::vector<std::string> remote_site_names;
  remote_site_names.push_back("remote_site_2_name");
  std::string local_site_name = "new_local_site_name";

  __globals->lock();
  __globals->set_remote_site_names(remote_site_names);
  __globals->set_local_site_name(local_site_name);
  __globals->unlock();

  // Now call handle_successful_callback as if called from http_callback
  EXPECT_CALL(*_store, fetch(_, _)).Times(1).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_gr_replicator, replicate(_));
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->handle_successful_callback(id);

  // Check that the sites have been changed correctly
  ASSERT_EQ(insert_timer->sites.size(), 2);
  EXPECT_EQ(insert_timer->sites[0], "new_local_site_name");
  EXPECT_EQ(insert_timer->sites[1], "remote_site_2_name");

  delete insert_timer;
}

// Test that the handle_failed_callback function correctly handles updating statistics,
// and then does not put it back into the store.
TEST_F(TestTimerHandlerAddAndReturn, HandleCallbackFailure)
{
  // Add a timer. This is a new timer, so should cause the stats to
  // increment (counts and tags)
  Timer* timer = default_timer(1);
  Timer* insert_timer;
  TimerID id = timer->id;
  EXPECT_CALL(*_store, fetch(timer->id, _)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).WillOnce(SaveArg<0>(&insert_timer));
  _th->add_timer(timer, 0);

  // The timer is successfully added. As it's a new timer it's passed through to
  // the store unchanged.
  EXPECT_EQ(insert_timer, timer);

  // Delete the timer (this is normally done by the insert call, but this
  // is mocked out)
  timer = NULL;

  // Now call handle_failed_callback as if called from http_callback
  EXPECT_CALL(*_store, fetch(id, _)).Times(1).
                       WillOnce(SetArgPointee<1>(insert_timer));
  EXPECT_CALL(*_replicator, replicate(insert_timer)).Times(0);
  EXPECT_CALL(*_gr_replicator, replicate(insert_timer)).Times(0);
  EXPECT_CALL(*_mock_increment_table, decrement(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, decrement("TAG1", 1)).Times(1);
  EXPECT_CALL(*_store, insert(_)).Times(0);

  _th->handle_failed_callback(id);
  // Do not delete timer as this is already done in the function
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
    _gr_replicator = new MockGRReplicator();
    _mock_tag_table = new MockInfiniteTable();
    _mock_scalar_table = new MockInfiniteScalarTable();
    _mock_increment_table = new MockIncrementTable();

    _th = new TimerHandler(_store,
                           _callback,
                           _replicator,
                           _gr_replicator,
                           _mock_increment_table,
                           _mock_tag_table,
                           _mock_scalar_table);
    _cond()->block_till_waiting();
  }

  void TearDown()
  {
    delete _th;
    delete _store;
    delete _health_checker;
    delete _replicator;
    delete _gr_replicator;
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
  MockGRReplicator* _gr_replicator;
  TimerHandler* _th;
};

// Test that getting timers for a node returns the set of timers
TEST_F(TestTimerHandlerRealStore, GetTimersForNode)
{
  uint32_t current_time = Utils::get_time();

  // Add a single timer to the store
  Timer* timer1 = default_timer(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  _th->add_timer(timer1, 0);

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
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 2, updated_cluster_view_id, current_time, get_response);
 std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":1,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"start-time-delta\".*,\"sequence-number\":0,\"interval\":100,\"repeat-for\":100},\"callback\":\\\{\"http\":\\\{\"uri\":\"http://localhost:80/callback1\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"],\"sites\":\\\[\"local_site_name\",\"remote_site_1_name\"]},\"statistics\":\\\{\"tag-info\":\\\[\\\{\"type\":\"TAG1\",\"count\":1}]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that if there are no timers for the requesting node,
// that trying to get the timers returns an empty list
TEST_F(TestTimerHandlerRealStore, SelectTimersNoMatchesReqNode)
{
  uint32_t current_time = Utils::get_time();

  // Add a single timer to the store
  Timer* timer1 = default_timer(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  _th->add_timer(timer1, 0);

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
  int rc = _th->get_timers_for_node("10.0.0.4:9999", 1, updated_cluster_view_id, current_time, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that getting timers for a node returns the set of timers
// (up to the maximum requested)
TEST_F(TestTimerHandlerRealStore, GetTimersForNodeNoClusterChange)
{
  uint32_t current_time = Utils::get_time();

  // Add a single timer to the store
  Timer* timer1 = default_timer(1);
  timer1->interval_ms = 100;
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  _th->add_timer(timer1, 0);

  // Now just call get_timers_for_node (as if someone had done a resync without
  // changing the cluster configuration). No timers should be returned
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 2, "cluster-view-id", current_time, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that getting timers for a node returns the set of timers
// (up to the maximum requested)
TEST_F(TestTimerHandlerRealStore, GetTimersForNodeHitMaxResponses)
{
  uint32_t current_time = Utils::get_time();

  // Add two timers to the store. Extend the length of the first timer
  // to ensure that we should always pick the second timer
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timer1->interval_ms = 200000;
  timer1->repeat_for = 200000;

  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(2);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG2", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG2", 1)).Times(1);
  _th->add_timer(timer1, 0);
  _th->add_timer(timer2, 0);

  // Now update the current cluster view ID
  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 1, updated_cluster_view_id, current_time, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":2,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"start-time-delta\".*,\"sequence-number\":0,\"interval\":100,\"repeat-for\":100},\"callback\":\\\{\"http\":\\\{\"uri\":\"http://localhost:80/callback2\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"],\"sites\":\\\[\"local_site_name\",\"remote_site_1_name\"]},\"statistics\":\\\{\"tag-info\":\\\[\\\{\"type\":\"TAG2\",\"count\":1}]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 206);
}

// Test that getting timers for a node returns a set of timers greater
// than the maximum requested if they've got the same pop time (to prevent
// getting stuck in a loop).
TEST_F(TestTimerHandlerRealStore, GetTimersForNodeMaxResponsesAndSamePopTime)
{
  uint32_t current_time = Utils::get_time();

  // Add three timers to the store with the same pop time, three that have
  // different pop times, and three tombstones.
  Timer* same_timer1 = default_timer(1);
  Timer* same_timer2 = default_timer(2);
  Timer* same_timer3 = default_timer(3);
  Timer* tombstone1 = default_timer(11);
  Timer* tombstone2 = default_timer(22);
  Timer* tombstone3 = default_timer(33);
  Timer* diff_timer1 = default_timer(111);
  Timer* diff_timer2 = default_timer(222);
  Timer* diff_timer3 = default_timer(333);

  diff_timer1->interval_ms = 200000;
  diff_timer1->repeat_for = 200000;
  diff_timer2->interval_ms = 200000;
  diff_timer2->repeat_for = 200000;
  diff_timer3->interval_ms = 200000;
  diff_timer3->repeat_for = 200000;

  tombstone1->become_tombstone();
  tombstone2->become_tombstone();
  tombstone3->become_tombstone();

  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(6);
  EXPECT_CALL(*_mock_tag_table, increment(_, 1)).Times(6);
  EXPECT_CALL(*_mock_scalar_table, increment(_, 1)).Times(6);

  _th->add_timer(diff_timer1, 0);
  _th->add_timer(diff_timer2, 0);
  _th->add_timer(diff_timer3, 0);
  _th->add_timer(tombstone1, 0);
  _th->add_timer(tombstone2, 0);
  _th->add_timer(tombstone3, 0);
  _th->add_timer(same_timer1, 0);
  _th->add_timer(same_timer2, 0);
  _th->add_timer(same_timer3, 0);

  // Now update the current cluster view ID
  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  // Ask for one timer - it should return timers 1, 2 and 3 as they have the
  // same pop time. It shouldn't return any tombstones, even though they have
  // the same pop time.
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 1, updated_cluster_view_id, current_time, get_response);

  // Parse the response
  rapidjson::Document doc;
  doc.Parse<0>(get_response.c_str());
  EXPECT_FALSE(doc.HasParseError());
  const rapidjson::Value& ids_arr = doc["Timers"];
  EXPECT_EQ(ids_arr.Size(), 3);
  std::vector<uint64_t> timer_ids;
  for (rapidjson::Value::ConstValueIterator ids_it = ids_arr.Begin();
       ids_it != ids_arr.End();
       ++ids_it)
  {
    const rapidjson::Value& id_arr = *ids_it;
    uint64_t timer_id = id_arr["TimerID"].GetInt64();
    timer_ids.push_back(timer_id);
  }

  std::vector<uint64_t> expected_timer_ids;
  expected_timer_ids.push_back(1);
  expected_timer_ids.push_back(2);
  expected_timer_ids.push_back(3);
  EXPECT_THAT(expected_timer_ids, UnorderedElementsAreArray(timer_ids));

  EXPECT_EQ(rc, 206);
}

// Test that getting timers from the long wheel orders by time correctly
TEST_F(TestTimerHandlerRealStore, GetMultipleTimersFromLongWheel)
{
  uint32_t current_time = Utils::get_time();

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  // Add three timers to the overdue timers. We don't care about stats/tags in
  // this test.
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(3);
  EXPECT_CALL(*_mock_tag_table, increment(_, _)).Times(3);
  EXPECT_CALL(*_mock_scalar_table, increment(_, _)).Times(3);

  Timer* timer1 = default_timer(1);
  timer1->interval_ms += 3000;
  timer1->repeat_for += 3000;
  _th->add_timer(timer1, 0);

  Timer* timer2 = default_timer(2);
  timer2->interval_ms += 2000;
  timer2->repeat_for += 2000;
  _th->add_timer(timer2, 0);

  Timer* timer3 = default_timer(3);
  _th->add_timer(timer3, 0);

  // Now update the current cluster nodes (to ensure that we're also picked
  // as a replica in the tests
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->unlock();

  // There should be three timers - they should be ordered by the time to pop
  // (3,2,1), not ordered by time they were added.
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 7, "cluster_view_id", current_time, get_response);

  // We don't check the contents of the timers in this test - only check the
  // timer IDs so we can be sure that the timers were returned in the right
  // order
  std::string exp_timer1 = "\\\{\"TimerID\":1,.*}";
  std::string exp_timer2 = "\\\{\"TimerID\":2,.*}";
  std::string exp_timer3 = "\\\{\"TimerID\":3,.*}";
  std::string exp_rsp = "\\\{\"Timers\":\\\[" + exp_timer3 + "," +
                                                exp_timer2 + "," +
                                                exp_timer1 +
                         "]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that getting timers for a node returns the set of timers, where the
// timers are spread out over all the data structures
TEST_F(TestTimerHandlerRealStore, GetTimersForNodeFromAllStructures)
{
  uint32_t current_time = Utils::get_time();

  // Add a single timer that will end up in the long wheel
  Timer* timer1 = default_timer(1);
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG1", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG1", 1)).Times(1);
  _th->add_timer(timer1, 0);

  // Add a single timer that will end up in the short wheel
  Timer* timer2 = default_timer(2);
  timer2->interval_ms = 0;
  timer2->repeat_for = 0;
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG2", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG2", 1)).Times(1);
  _th->add_timer(timer2, 0);

  // Add a single timer that will end up in the heap
  Timer* timer3 = default_timer(3);
  timer3->interval_ms = 20000000;
  timer3->repeat_for = 20000000;
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG3", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG3", 1)).Times(1);
  _th->add_timer(timer3, 0);

  // Add a single timer that will end up in the heap
  Timer* timer4 = default_timer(4);
  timer4->interval_ms = 30000000;
  timer4->repeat_for = 30000000;
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG4", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG4", 1)).Times(1);
  _th->add_timer(timer4, 0);

  // Add a single timer that will end up in the heap
  Timer* timer5 = default_timer(5);
  timer5->interval_ms = 10000000;
  timer5->repeat_for = 10000000;
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(1);
  EXPECT_CALL(*_mock_tag_table, increment("TAG5", 1)).Times(1);
  EXPECT_CALL(*_mock_scalar_table, increment("TAG5", 1)).Times(1);
  _th->add_timer(timer5, 0);

  // Now update the current cluster nodes (to ensure that we're also picked
  // as a replica in the tests
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->unlock();

  // There should be five timers - they should be ordered by the time to pop
  // (2,1,5,3,4), not ordered by time they were added.
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 7, "cluster_view_id", current_time, get_response);

  // We don't check the contents of the timers in this test - only check the
  // timer IDs so we can be sure that the timers were returned in the right
  // order
  std::string exp_timer1 = "\\\{\"TimerID\":1,.*}";
  std::string exp_timer2 = "\\\{\"TimerID\":2,.*}";
  std::string exp_timer3 = "\\\{\"TimerID\":3,.*}";
  std::string exp_timer4 = "\\\{\"TimerID\":4,.*}";
  std::string exp_timer5 = "\\\{\"TimerID\":5,.*}";
  std::string exp_rsp = "\\\{\"Timers\":\\\[" + exp_timer2 + "," +
                                                exp_timer1 + "," +
                                                exp_timer5 + "," +
                                                exp_timer3 + "," +
                                                exp_timer4 +
                         "]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that getting timers from the short wheel honours the time-from
TEST_F(TestTimerHandlerRealStore, TimeFromShortWheelTimers)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  // Add a timer to the short wheel. We don't care about stats/tags in
  // this test.
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(2);
  EXPECT_CALL(*_mock_tag_table, increment(_, _)).Times(2);
  EXPECT_CALL(*_mock_scalar_table, increment(_, _)).Times(2);

  Timer* timer1 = default_timer(1);
  timer1->interval_ms = 0;
  timer1->repeat_for = 0;
  _th->add_timer(timer1, 0);

  Timer* timer2 = default_timer(2);
  timer2->interval_ms = 10;
  timer2->repeat_for = 10;
  _th->add_timer(timer2, 0);

  // Now update the current cluster nodes (to ensure that we're also picked
  // as a replica in the tests
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->unlock();

  // Check that only one timer is returned
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 7, "cluster_view_id", ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + 5, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":2,.*}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that getting timers from the long wheel honours the time-from
TEST_F(TestTimerHandlerRealStore, TimeFromLongWheelTimers)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  // Add timers to the long wheel. We don't care about stats/tags in
  // this test.
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(2);
  EXPECT_CALL(*_mock_tag_table, increment(_, _)).Times(2);
  EXPECT_CALL(*_mock_scalar_table, increment(_, _)).Times(2);

  Timer* timer1 = default_timer(1);
  _th->add_timer(timer1, 0);

  Timer* timer2 = default_timer(2);
  timer2->interval_ms = 200000;
  timer2->repeat_for = 200000;
  _th->add_timer(timer2, 0);

  // Now update the current cluster nodes (to ensure that we're also picked
  // as a replica in the tests
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->unlock();

  // Check that only one timer is returned
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 7, "cluster_view_id", ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + 150000, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":2,.*}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}

// Test that getting timers from the heap honours the time-from
TEST_F(TestTimerHandlerRealStore, TimeFromHeapTimers)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  // Add timers to the heap. We don't care about stats/tags in
  // this test.
  EXPECT_CALL(*_mock_increment_table, increment(1)).Times(2);
  EXPECT_CALL(*_mock_tag_table, increment(_, _)).Times(2);
  EXPECT_CALL(*_mock_scalar_table, increment(_, _)).Times(2);

  Timer* timer1 = default_timer(1);
  timer1->interval_ms = 10000000;
  timer1->repeat_for = 10000000;
  _th->add_timer(timer1, 0);

  Timer* timer2 = default_timer(2);
  timer2->interval_ms = 20000000;
  timer2->repeat_for = 20000000;
  _th->add_timer(timer2, 0);

  // Now update the current cluster nodes (to ensure that we're also picked
  // as a replica in the tests
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(cluster_addresses);
  __globals->unlock();

  // Check that only one timer is returned
  std::string get_response;
  int rc = _th->get_timers_for_node("10.0.0.1:9999", 7, "cluster_view_id", ts.tv_sec * 1000 + ts.tv_nsec / 1000000 + 15000000, get_response);
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":2,.*}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));
  EXPECT_EQ(rc, 200);
}
