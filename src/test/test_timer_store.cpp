/**
 * @file test_timer_store.cpp
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

#include "timer_store.h"
#include "timer_helper.h"
#include "test_interposer.hpp"
#include "base.h"
#include "health_checker.h"
#include "globals.h"

#include <gtest/gtest.h>
#include "gmock/gmock.h"

using ::testing::MatchesRegex;

// The timer store has a granularity of 10ms. This means that timers may pop up
// to 10ms late. As a result the timer store tests often add this granularity
// when advancing time to guarantee that a timer has popped.
const int TIMER_GRANULARITY_MS = 10;

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TestTimerStore : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();

    // I mark the hours, every one, Nor have I yet outrun the Sun.
    // My use and value, unto you, Are gauged by what you have to do.
    cwtest_completely_control_time();
    hc = new HealthChecker();
    ts = new TimerStore(hc);

    // Default some timers to short, mid and long.
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    for (int ii = 0; ii < 3; ++ii)
    {
      timers[ii] = default_timer(ii + 1);
      timers[ii]->start_time = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));
    }

    // Timer 1 will pop in 100ms.
    timers[0]->interval = 100;

    // Timer 2 will pop strictly after 1 second.
    timers[1]->interval = 10000 + 200;

    // Timer 3 will pop strictly after 1 hour
    timers[2]->interval = (3600 * 1000) + 300;

    // Create an out of the blue tombstone for timer one.
    tombstone = Timer::create_tombstone(1, 0);
    tombstone->start_time = timers[0]->start_time + 50;
  }

  // Since the Timer store owns timers after they've been added, the tests,
  // must clear up the timer instances.
  virtual void TearDown()
  {
    delete ts; ts = NULL;
    delete hc; hc = NULL;
    cwtest_reset_time();
    Base::TearDown();
  }

  // Variables under test.
  TimerStore* ts;
  Timer* timers[3];
  Timer* tombstone;
  HealthChecker* hc;
};

/*****************************************************************************/
/* Instance Functions                                                        */
/*****************************************************************************/

TEST_F(TestTimerStore, NearGetNextTimersTest)
{
  ts->add_timer(timers[0]);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ASSERT_EQ(0u, next_timers.size());
  cwtest_advance_time_ms(100 + TIMER_GRANULARITY_MS);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1u, timers[0]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, NearGetNextTimersOffsetTest)
{
  timers[0]->interval = 1600;

  ts->add_timer(timers[0]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(1500);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(0u, next_timers.size()) << "Bucket should have 0 timers";

  next_timers.clear();

  cwtest_advance_time_ms(100 + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timers";

  next_timers.clear();

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, MidGetNextTimersTest)
{
  ts->add_timer(timers[1]);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ASSERT_EQ(0u, next_timers.size());
  cwtest_advance_time_ms(100000);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  timers[1] = *next_timers.begin();
  EXPECT_EQ(2u, timers[1]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, LongGetNextTimersTest)
{
  ts->add_timer(timers[2]);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ASSERT_EQ(0u, next_timers.size());
  cwtest_advance_time_ms(timers[2]->interval + TIMER_GRANULARITY_MS);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  timers[2] = *next_timers.begin();
  EXPECT_EQ(3u, timers[2]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, MultiNearGetNextTimersTest)
{
  // Shorten timer two to be under 1 second.
  timers[1]->interval = 400;

  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(1000 + TIMER_GRANULARITY_MS);

  ts->get_next_timers(next_timers);

  ASSERT_EQ(2u, next_timers.size()) << "Bucket should have 2 timers";

  next_timers.clear();

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, ClashingMultiMidGetNextTimersTest)
{
  // Lengthen timer one to be in the same second bucket as timer two but different ms
  // buckets.
  timers[0]->interval = 10000 + 100;

  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(timers[0]->interval + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1u, timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(timers[1]->interval - timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  timers[1] = *next_timers.begin();
  EXPECT_EQ(2u, timers[1]->id);

  next_timers.clear();

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, SeparateMultiMidGetNextTimersTest)
{
  // Lengthen timer one to be in a different second bucket than timer two.
  timers[0]->interval = 9000 + 100;

  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(timers[0]->interval + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1u, timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(timers[1]->interval - timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  timers[1] = *next_timers.begin();
  EXPECT_EQ(2u, timers[1]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, MultiLongGetTimersTest)
{
  // Lengthen timer one and two to be in the extra heap.
  timers[0]->interval = (3600 * 1000) + 100;
  timers[1]->interval = (3600 * 1000) + 200;

  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);
  ts->add_timer(timers[2]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(timers[0]->interval + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1u, timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(timers[1]->interval - timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  timers[1] = *next_timers.begin();
  EXPECT_EQ(2u, timers[1]->id);

  next_timers.clear();

  cwtest_advance_time_ms(timers[2]->interval - timers[1]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  timers[2] = *next_timers.begin();
  EXPECT_EQ(3u, timers[2]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, ReallyLongTimer)
{
  // Lengthen timer three to really long (10 hours)
  timers[2]->interval = (3600 * 1000) * 10;
  ts->add_timer(timers[2]);

  std::unordered_set<Timer*> next_timers;

  ts->get_next_timers(next_timers);
  ASSERT_EQ(0u, next_timers.size());

  cwtest_advance_time_ms(((3600 * 1000) * 10) + TIMER_GRANULARITY_MS);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  timers[2] = *next_timers.begin();
  EXPECT_EQ(3u, timers[2]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DeleteNearTimer)
{
  uint64_t interval = timers[0]->interval;
  ts->add_timer(timers[0]);
  ts->delete_timer(1);
  std::unordered_set<Timer*> next_timers;
  cwtest_advance_time_ms(interval + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DeleteMidTimer)
{
  uint64_t interval = timers[2]->interval;
  ts->add_timer(timers[1]);
  ts->delete_timer(2);
  std::unordered_set<Timer*> next_timers;
  cwtest_advance_time_ms(interval + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());
  delete timers[0];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DeleteLongTimer)
{
  uint64_t interval = timers[2]->interval;
  ts->add_timer(timers[2]);
  ts->delete_timer(3);
  cwtest_advance_time_ms(interval + TIMER_GRANULARITY_MS);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());
  delete timers[0];
  delete timers[1];
  delete tombstone;
}

TEST_F(TestTimerStore, UpdateTimer)
{
  ts->add_timer(timers[0]);

  // Replace timer one, using a newer timer with the same ID.
  timers[1]->id = 1;
  timers[1]->start_time++;
  ts->add_timer(timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer one was deleted when it was overwritten
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DontUpdateTimerAge)
{
  ts->add_timer(timers[0]);

  // Attempt to replace timer one but the replacement is older
  timers[1]->id = 1;
  timers[1]->start_time--;
  ts->add_timer(timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer two was deleted when it failed to overwrite timer one
  delete timers[0];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DontUpdateTimerSeqNo)
{
  timers[0]->sequence_number++;
  ts->add_timer(timers[0]);

  // Attempt to replace timer one but the replacement has a lower SeqNo
  timers[1]->id = 1;
  ts->add_timer(timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer two was deleted when it failed to overwrite timer one
  delete timers[0];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, AddTombstone)
{
  ts->add_timer(tombstone);

  std::unordered_set<Timer*> next_timers;
  cwtest_advance_time_ms(1000000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, OverwriteWithTombstone)
{
  ts->add_timer(timers[0]);
  ts->add_timer(tombstone);

  std::unordered_set<Timer*> next_timers;
  cwtest_advance_time_ms(1000000);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());

  Timer* extracted = *next_timers.begin();
  EXPECT_TRUE(extracted->is_tombstone());
  EXPECT_EQ(100u, extracted->interval);
  EXPECT_EQ(100u, extracted->repeat_for);

  delete timers[1];
  delete timers[2];
  delete tombstone;
}

// Test for issue #19, even if time is moving in non-10ms steps
// we should be able to reliably update/tombstone timers.
TEST_F(TestTimerStore, Non10msShortTimerUpdate)
{
  // Offset the interval of the first timer so it's not a multiple of 10ms.
  timers[0]->interval += 4;

  ts->add_timer(timers[0]);

  // Move time on more than the timer's shift but less than 10ms, even
  // after this the timer store should know which bucket the timer is in.
  cwtest_advance_time_ms(8);

  // Attempting to get a set of timers updates the internal clock in the
  // timer store.
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Now, to prove the timer store can still find the timer, update it to
  // a tombstone.
  ts->add_timer(tombstone);

  // No timers are ready to pop yet
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Move on till the tombstone should pop (50 ms offset from timer[0])
  cwtest_advance_time_ms(150 + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());
  next_timers.clear();

  // Move on again to ensure there are no more timers in the store.
  cwtest_advance_time_ms(100000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // timer[0] was deleted when it was updated in the timer store.
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

// Test for issue #19
TEST_F(TestTimerStore, Non10msMediumTimerUpdate)
{
  // Offset the interval of the second timer so it's not a multiple of 10ms.
  timers[1]->interval += 4;

  ts->add_timer(timers[1]);

  // Move time on to less than 1s but closer than the timer's offset, even
  // after this the timer store should know which bucket the timer is in.
  cwtest_advance_time_ms(990 + 8);

  // Attempting to get a set of timers updates the internal clock in the
  // timer store.
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Now, to prove the timer store can still find the timer, update it to
  // a tombstone.
  tombstone->id = timers[1]->id;
  ts->add_timer(tombstone);

  // No timers are ready to pop yet
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Move on till the timer should pop
  cwtest_advance_time_ms(100000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());
  next_timers.clear();

  // Move on again to ensure there are no more timers in the store.
  cwtest_advance_time_ms(100000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // timer[1] was deleted when it was updated in the timer store.
  delete timers[0];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, MixtureOfTimerLengths)
{
  // Add timers that all pop at the same time, but in such a way that one ends
  // up in the short wheel, one in the long wheel, and one in the heap.  Check
  // they pop at the same time.
  std::unordered_set<Timer*> next_timers;

  // Timers all pop 1hr, 1s, 500ms from the start of the test.
  // Set timer 1.
  timers[0]->interval = ((60 * 60 * 1000) + (1 * 1000) + 500);
  ts->add_timer(timers[0]);

  // Move on by 1hr. Nothing has popped.
  cwtest_advance_time_ms(60 * 60 * 1000);
  timers[1]->start_time += (60 * 60 * 1000);
  timers[2]->start_time += (60 * 60 * 1000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Timer 2 pops in 1s, 500ms
  timers[1]->interval = ((1 * 1000) + 500);
  ts->add_timer(timers[1]);

  // Move on by 1s. Nothing has popped.
  cwtest_advance_time_ms(1 * 1000);
  timers[2]->start_time += (1 * 1000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Timer 3 pops in 500ms.
  timers[2]->interval = 500;
  ts->add_timer(timers[2]);

  // Move on by 500ms. All timers pop.
  cwtest_advance_time_ms(500 + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(3u, next_timers.size());
  next_timers.clear();

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, TimerPopsOnTheHour)
{
  std::unordered_set<Timer*> next_timers;
  uint64_t pop_time_ms;

  pop_time_ms = (timers[0]->start_time / (60 * 60 * 1000));
  pop_time_ms += 2;
  pop_time_ms *= (60 * 60 * 1000);
  timers[0]->interval = pop_time_ms - timers[0]->start_time;
  ts->add_timer(timers[0]);

  // Move on to the pop time. The timer pops correctly.
  cwtest_advance_time_ms(pop_time_ms - timers[0]->start_time + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());
  next_timers.clear();

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, PopOverdueTimer)
{
  cwtest_advance_time_ms(500);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ts->add_timer(timers[0]);
  ts->get_next_timers(next_timers);

  ASSERT_EQ(1u, next_timers.size());
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1u, timers[0]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DeleteOverdueTimer)
{
  cwtest_advance_time_ms(500);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ts->add_timer(timers[0]);
  ts->delete_timer(1);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(0u, next_timers.size());

  delete timers[1];
  delete timers[2];
  delete tombstone;
}

// Test that marking some of the replicas as being informed 
// doesn't change the timer if it's got an up-to-date 
// cluster view ID
TEST_F(TestTimerStore, UpdateReplicaTrackerValueForNewTimer)
{
  cwtest_advance_time_ms(500);
  std::unordered_set<Timer*> next_timers;
  timers[0]->_replica_tracker = 15;
  ts->add_timer(timers[0]);
  ts->update_replica_tracker_for_timer(1u, 3);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  timers[0] = *next_timers.begin();
  ASSERT_EQ(15u, timers[0]->_replica_tracker);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

// Test that marking some of the replicas as being informed
// changes the replica tracker if the cluster view ID is
// different
TEST_F(TestTimerStore, UpdateReplicaTrackerValueForOldTimer)
{
  cwtest_advance_time_ms(500);
  std::unordered_set<Timer*> next_timers;
  timers[0]->_replica_tracker = 15;
  timers[0]->cluster_view_id = "different-id";
  ts->add_timer(timers[0]);
  ts->update_replica_tracker_for_timer(1u, 3);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  timers[0] = *next_timers.begin();
  ASSERT_EQ(7u, timers[0]->_replica_tracker);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

// Test that getting timers for a node returns the set of timers
// (up to the maximum requested)
TEST_F(TestTimerStore, SelectTimers)
{
  std::unordered_set<Timer*> next_timers;
  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);
  ts->add_timer(timers[2]);
  std::string get_response;

  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  ts->get_timers_for_node("10.0.0.1:9999", 2, updated_cluster_view_id, get_response);

  // Check the GET has the right format. This is two timers out of the three available (as the
  // max number of timers is set to 2). We're using a simple regex here as we use JSON
  // parsing in the code.
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":1,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"sequence-number\":0,\"interval\":0,\"repeat-for\":0},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback1\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]}}},\\\{\"TimerID\":2,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\":.*,\"sequence-number\":0,\"interval\":10,\"repeat-for\":0},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback2\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"updated-cluster-view-id\",\"replicas\":\\\[\"10.0.0.1:9999\"]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));

  std::string cluster_view_id = "cluster-view-id";
  cluster_addresses.push_back("10.0.0.2:9999");
  cluster_addresses.push_back("10.0.0.3:9999");
  __globals->lock();
  __globals->set_cluster_view_id(cluster_view_id);
  __globals->set_cluster_addresses(cluster_addresses);
  __globals->unlock();

  delete tombstone;
}

// Test that if there are no timers for the requesting node, 
// that trying to get the timers returns an empty list
TEST_F(TestTimerStore, SelectTimersNoMatchesReqNode)
{
  std::unordered_set<Timer*> next_timers;
  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);
  ts->add_timer(timers[2]);
  std::string get_response;
  ts->get_timers_for_node("10.0.0.4:9999", 1, "updated-cluster-view-id", get_response);

  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  delete tombstone;
}

// Test that if there are no timers with an out of date cluster
// ID then trying to get the timers returns an empty list
TEST_F(TestTimerStore, SelectTimersNoMatchesClusterID)
{
  std::unordered_set<Timer*> next_timers;
  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);
  ts->add_timer(timers[2]);
  std::string get_response;
  ts->get_timers_for_node("10.0.0.1:9999", 1, "cluster-view-id", get_response);

  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  delete tombstone;
}

// Test that updating a timer with a new cluster ID causes the original 
// timer to be saved off. 
//
// WARNING: In this test we look directly in the timer store as there's no 
// other way to test what's in the timer map (when it's not also in the timer
// wheel)
TEST_F(TestTimerStore, UpdateClusterViewID)
{
  // Add the first timer with ID 1
  ts->add_timer(timers[0]);

  // Find this timer in the store, and check there's no saved timers
  // associated with it
  std::map<TimerID, std::vector<Timer*>>::iterator map_it =
                                                    ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != ts->_timer_lookup_table.end());
  EXPECT_EQ(1u, map_it->second.size());
  EXPECT_EQ(1u, map_it->second.front()->id);

  // Add a new timer with the same ID, and an updated Cluster View ID
  timers[1]->id = 1; 
  timers[1]->cluster_view_id = "updated-cluster-view-id"; 
  ts->add_timer(timers[1]);

  // Find this timer in the store, and check there's a saved timer. The saved
  // timer has the old cluster view ID, and the new timer has the new one. 
  map_it = ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != ts->_timer_lookup_table.end());
  EXPECT_EQ(2u, map_it->second.size());
  EXPECT_EQ(1u, map_it->second.front()->id);
  EXPECT_EQ("updated-cluster-view-id", map_it->second.front()->cluster_view_id);
  EXPECT_EQ(1u, map_it->second.back()->id);
  EXPECT_EQ("cluster-view-id", map_it->second.back()->cluster_view_id);

  // Add a new timer with the same ID, an updated Cluster View ID, 
  // and make it a tombstone
  timers[2]->id = 1;
  timers[2]->cluster_view_id = "updated-again-cluster-view-id";
  timers[2]->become_tombstone();
  ts->add_timer(timers[2]);

  // Find this timer in the store, and check there's a saved timer. The saved
  // timer has the old cluster view ID, and the new timer has the new one.
  map_it = ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != ts->_timer_lookup_table.end());
  EXPECT_EQ(2u, map_it->second.size());
  EXPECT_EQ(1u, map_it->second.front()->id);
  EXPECT_EQ("updated-again-cluster-view-id", map_it->second.front()->cluster_view_id);
  EXPECT_TRUE(map_it->second.front()->is_tombstone());
  EXPECT_EQ(1u, map_it->second.back()->id);
  EXPECT_EQ("updated-cluster-view-id", map_it->second.back()->cluster_view_id);
  EXPECT_FALSE(map_it->second.back()->is_tombstone());

  delete tombstone;
}

// Test that the store uses the saved timers (rather than the timers in the 
// timer wheel) when updating the replica tracker or handling get requests. 
//
// WARNING: In this test we look directly in the timer store as there's no
// other way to test what's in the timer map (when it's not also in the timer
// wheel)
TEST_F(TestTimerStore, ModifySavedTimers)
{
  // Add a timer to the store with an old cluster ID and three replicas
  timers[0]->cluster_view_id = "old-cluster-view-id"; 
  timers[0]->_replica_tracker = 7;
  timers[0]->replicas.push_back("10.0.0.2:9999");
  timers[0]->replicas.push_back("10.0.0.3:9999");
  ts->add_timer(timers[0]);

  // Add a timer to the store with the same ID as the previous timer, 
  // but an updated ID. This will take the original timer out of the 
  // timer wheel and save it just in the map
  timers[1]->id = 1; 
  timers[1]->_replica_tracker = 7;
  timers[1]->replicas.push_back("10.0.0.2:9999");
  timers[1]->replicas.push_back("10.0.0.3:9999");
  ts->add_timer(timers[1]);

  // Update the replica tracker for Timer ID 1. This should update the
  // saved timer to mark that the third replica has been informed, not 
  // the new first timer.
  ts->update_replica_tracker_for_timer(1u, 2);
  std::map<TimerID, std::vector<Timer*>>::iterator map_it =
                                                    ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != ts->_timer_lookup_table.end());
  EXPECT_EQ(2u, map_it->second.size());
  EXPECT_EQ(7u, map_it->second.front()->_replica_tracker);
  EXPECT_EQ(3u, map_it->second.back()->_replica_tracker);

  // Finally, update the replica tracker to mark all replicas
  // as having been informed for Timer ID 1. This should 
  // delete the saved timer. 
  ts->update_replica_tracker_for_timer(1u, 0);
  map_it = ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != ts->_timer_lookup_table.end());
  EXPECT_EQ(1u, map_it->second.size());
  std::string get_response;
  ts->get_timers_for_node("10.0.0.1:9999", 1, "cluster-view-id", get_response);
  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  delete timers[2];
  delete tombstone;
}

