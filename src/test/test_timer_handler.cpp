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
#include "mock_callback.h"
#include "mock_replicator.h"
#include "base.h"
#include "test_interposer.hpp"

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
  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));
  _th = new TimerHandler(_store, _callback);
  _cond()->block_till_waiting();
}

TEST_F(TestTimerHandler, PopOneTimer)
{
  std::unordered_set<Timer*> timers;
  Timer* timer = default_timer(1);
  timers.insert(timer);

  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer));

  _th = new TimerHandler(_store, _callback);
  _cond()->block_till_waiting();
  delete timer;
}

TEST_F(TestTimerHandler, PopRepeatedTimer)
{
  std::unordered_set<Timer*> timers;
  Timer* timer = default_timer(1);
  timer->repeat_for = timer->interval * 2;
  timers.insert(timer);

  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer)).Times(2);

  _th = new TimerHandler(_store, _callback);
  _cond()->block_till_waiting();
  delete timer;
}

TEST_F(TestTimerHandler, PopMultipleTimersSimultaneously)
{
  std::unordered_set<Timer*> timers;
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timers.insert(timer1);
  timers.insert(timer2);

  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandler, PopMultipleTimersSeries)
{
  std::unordered_set<Timer*> timers1;
  std::unordered_set<Timer*> timers2;
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timers1.insert(timer1);
  timers2.insert(timer2);

  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandler, PopMultipleRepeatingTimers)
{
  std::unordered_set<Timer*> timers1;
  std::unordered_set<Timer*> timers2;
  Timer* timer1 = default_timer(1);
  timer1->repeat_for = timer1->interval * 2;
  Timer* timer2 = default_timer(2);
  timer2->repeat_for = timer2->interval * 2;
  timers1.insert(timer1);
  timers2.insert(timer2);

  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer1)).Times(2);
  EXPECT_CALL(*_callback, perform(timer2)).Times(2);

  _th = new TimerHandler(_store, _callback);
  _cond()->block_till_waiting();
  delete timer1;
  delete timer2;
}

TEST_F(TestTimerHandler, EmptyStore)
{
  std::unordered_set<Timer*> timers1;
  std::unordered_set<Timer*> timers2;
  Timer* timer1 = default_timer(1);
  Timer* timer2 = default_timer(2);
  timers1.insert(timer1);
  timers2.insert(timer2);

  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers1)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(timers2)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));

  EXPECT_CALL(*_callback, perform(timer1));
  EXPECT_CALL(*_callback, perform(timer2));

  _th = new TimerHandler(_store, _callback);
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

  // Once we add the timer, we'll poll the store for a new timer, expect an extra
  // call to get_next_timers().
  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));
  EXPECT_CALL(*_store, add_timer(timer)).Times(1);
  _th = new TimerHandler(_store, _callback);
  _cond()->block_till_waiting();

  _th->add_timer(timer);
  _cond()->block_till_waiting();

  delete timer;
}

TEST_F(TestTimerHandler, LeakTest)
{
  Timer* timer = default_timer(1);
  std::unordered_set<Timer*> timers;
  timers.insert(timer);

  // Make sure that the final call to get_next_timers actually returns some.  This
  // test should still pass valgrind's checking without leaking the timer.
  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(timers));

  _th = new TimerHandler(_store, _callback);
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
  timer->interval = 100;
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
  EXPECT_CALL(*_store, get_next_timers(_)).
                       WillOnce(SetArgReferee<0>(timers)).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>())).
                       WillOnce(SetArgReferee<0>(std::unordered_set<Timer*>()));
  EXPECT_CALL(*_callback, perform(_));

  _th = new TimerHandler(_store, _callback);
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
