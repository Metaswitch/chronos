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

static uint32_t get_time_ms()
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  // Overflow to a 32 bit number is intentional
  uint64_t time = now.tv_sec;
  time *= 1000;
  time += now.tv_nsec / 1000000;
  return time;
}


// The timer store has a granularity of 10ms. This means that timers may pop up
// to 10ms late. As a result the timer store tests often add this granularity
// when advancing time to guarantee that a timer has popped.
const int TIMER_GRANULARITY_MS = 8;

class Overflow2h {
  static void set_time() {
    // Align with overflow point minus 2 hours
    cwtest_advance_time_ms(( - get_time_ms()) - (2 * 60 * 60 * 1000));
  }
};

class Overflow45m {
  static void set_time() {
    // Align with overflow point minus 45 minutes
    cwtest_advance_time_ms(( - get_time_ms()) - (45 * 60 * 1000));
  }
};

class Overflow45s {
  static void set_time() {
    // Align with overflow point minus 45 seconds
    cwtest_advance_time_ms(( - get_time_ms()) - (45000));
  }
};

class Overflow45ms {
  static void set_time() {
    // Align with overflow point minus 45ms
    cwtest_advance_time_ms(( - get_time_ms()) - 45);
  }
};

class NoOverflow {
  static void set_time() {
    // Set the time to be just after overflow
    cwtest_advance_time_ms( - get_time_ms());
  }
};


/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

template <class T>
class TestTimerStore : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();

    // I mark the hours, every one, Nor have I yet outrun the Sun.
    // My use and value, unto you, Are gauged by what you have to do.
    cwtest_completely_control_time();
    T::set_time();

    // We are now at a point in time where timestamps will exist in 32
    // bit integers, but will overflow in a specified amount of time.

    hc = new HealthChecker();
    ts = new TimerStore(hc);

    // Default some timers to short, mid and long
    for (int ii = 0; ii < 3; ++ii)
    {
      timers[ii] = default_timer(ii + 1);
      timers[ii]->start_time_mono_ms = get_time_ms();
    }

    // Timer 1 will pop in 100ms.
    timers[0]->interval_ms = 100;

    // Timer 2 will pop strictly after 1 second.
    timers[1]->interval_ms = 10000 + 200;

    // Timer 3 will pop strictly after 1 hour
    timers[2]->interval_ms = (3600 * 1000) + 300;

    // Create an out of the blue tombstone for timer one.
    tombstone = Timer::create_tombstone(1, 0);
    tombstone->start_time_mono_ms = timers[0]->start_time_mono_ms + 50;

    tombstonepair.active_timer = tombstone;
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

  void ts_insert_helper(TimerPair pair)
  {
    TimerID id = pair.active_timer->id;
    uint32_t next_pop_time = pair.active_timer->next_pop_time();
    std::vector<std::string> cluster_view_id_vector;
    cluster_view_id_vector.push_back(pair.active_timer->cluster_view_id);
    if (pair.information_timer)
    {
      cluster_view_id_vector.push_back(pair.information_timer->cluster_view_id);
    }

    ts->insert(pair, id, next_pop_time, cluster_view_id_vector);
  }

  void ts_insert_helper(Timer* timer)
  {
    TimerPair pair;
    pair.active_timer = timer;
    ts_insert_helper(pair);
  }

  // Variables under test.
  TimerStore* ts;
  Timer* timers[3];
  Timer* tombstone;
  TimerPair tombstonepair;
  HealthChecker* hc;
};

/*****************************************************************************/
/* Instance Functions                                                        */
/*****************************************************************************/

using testing::Types;

typedef ::testing::Types<Overflow2h, Overflow45m, Overflow45s, Overflow45ms, NoOverflow> TimeTypes;
TYPED_TEST_CASE(TestTimerStore, TimeTypes);

TYPED_TEST(TestTimerStore, NearGetNextTimersTest)
{
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);

  ASSERT_EQ(0u, next_timers.size());
  cwtest_advance_time_ms(100 + TIMER_GRANULARITY_MS);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  TestFixture::timers[0] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(1u, TestFixture::timers[0]->id);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, NearGetNextTimersOffsetTest)
{
  TestFixture::timers[0]->interval_ms = 1600;

  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  std::unordered_set<TimerPair> next_timers;

  cwtest_advance_time_ms(1500);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(0u, next_timers.size()) << "Bucket should have 0 timers";

  next_timers.clear();

  cwtest_advance_time_ms(100 + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timers";

  next_timers.clear();

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, MidGetNextTimersTest)
{
  TestFixture::ts_insert_helper(TestFixture::timers[1]);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);

  ASSERT_EQ(0u, next_timers.size());
  cwtest_advance_time_ms(100000);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  TestFixture::timers[1] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(2u, TestFixture::timers[1]->id);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, LongGetNextTimersTest)
{
  TestFixture::ts_insert_helper(TestFixture::timers[2]);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);

  ASSERT_EQ(0u, next_timers.size());
  cwtest_advance_time_ms(TestFixture::timers[2]->interval_ms + TIMER_GRANULARITY_MS);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  TestFixture::timers[2] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(3u, TestFixture::timers[2]->id);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, MultiNearGetNextTimersTest)
{
  // Shorten timer two to be under 1 second.
  TestFixture::timers[1]->interval_ms = 400;

  TestFixture::ts_insert_helper(TestFixture::timers[0]);
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  std::unordered_set<TimerPair> next_timers;

  cwtest_advance_time_ms(1000 + TIMER_GRANULARITY_MS);

  TestFixture::ts->fetch_next_timers(next_timers);

  ASSERT_EQ(2u, next_timers.size()) << "Bucket should have 2 timers";

  next_timers.clear();

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, ClashingMultiMidGetNextTimersTest)
{
  // Lengthen timer one to be in the same second bucket as timer two but different ms
  // buckets.
  TestFixture::timers[0]->interval_ms = 10000 + 100;

  TestFixture::ts_insert_helper(TestFixture::timers[0]);
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  std::unordered_set<TimerPair> next_timers;

  cwtest_advance_time_ms(TestFixture::timers[0]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  TestFixture::timers[0] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(1u, TestFixture::timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(TestFixture::timers[1]->interval_ms - TestFixture::timers[0]->interval_ms);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  TestFixture::timers[1] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(2u, TestFixture::timers[1]->id);

  next_timers.clear();

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, SeparateMultiMidGetNextTimersTest)
{
  // Lengthen timer one to be in a different second bucket than timer two.
  TestFixture::timers[0]->interval_ms = 9000 + 100;

  TestFixture::ts_insert_helper(TestFixture::timers[0]);
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  std::unordered_set<TimerPair> next_timers;

  cwtest_advance_time_ms(TestFixture::timers[0]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  TestFixture::timers[0] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(1u, TestFixture::timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(TestFixture::timers[1]->interval_ms - TestFixture::timers[0]->interval_ms);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  TestFixture::timers[1] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(2u, TestFixture::timers[1]->id);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, MultiLongGetTimersTest)
{
  // Lengthen timer one and two to be in the extra heap.
  TestFixture::timers[0]->interval_ms = (3600 * 1000) + 100;
  TestFixture::timers[1]->interval_ms = (3600 * 1000) + 200;

  TestFixture::ts_insert_helper(TestFixture::timers[0]);
  TestFixture::ts_insert_helper(TestFixture::timers[1]);
  TestFixture::ts_insert_helper(TestFixture::timers[2]);

  std::unordered_set<TimerPair> next_timers;

  cwtest_advance_time_ms(TestFixture::timers[0]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  TestFixture::timers[0] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(1u, TestFixture::timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(TestFixture::timers[1]->interval_ms - TestFixture::timers[0]->interval_ms);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  TestFixture::timers[1] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(2u, TestFixture::timers[1]->id);

  next_timers.clear();

  cwtest_advance_time_ms(TestFixture::timers[2]->interval_ms - TestFixture::timers[1]->interval_ms);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  TestFixture::timers[2] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(3u, TestFixture::timers[2]->id);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, ReallyLongTimer)
{
  // Lengthen timer three to really long (10 hours)
  TestFixture::timers[2]->interval_ms = (3600 * 1000) * 10;
  TestFixture::ts_insert_helper(TestFixture::timers[2]);

  std::unordered_set<TimerPair> next_timers;

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(0u, next_timers.size());

  cwtest_advance_time_ms(((3600 * 1000) * 10) + TIMER_GRANULARITY_MS);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size()) << "Bucket should have 1 timer";
  TestFixture::timers[2] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(3u, TestFixture::timers[2]->id);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, DeleteNearTimer)
{
  uint32_t interval_ms = TestFixture::timers[0]->interval_ms;
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  TimerPair to_delete;
  TestFixture::ts->fetch(1, to_delete);

  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);

  EXPECT_TRUE(next_timers.empty());
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, DeleteMidTimer)
{
  uint32_t interval_ms = TestFixture::timers[2]->interval_ms;
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  TimerPair to_delete;
  TestFixture::ts->fetch(2, to_delete);

  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);

  EXPECT_TRUE(next_timers.empty());
  delete TestFixture::timers[0];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, DeleteLongTimer)
{
  uint32_t interval_ms = TestFixture::timers[2]->interval_ms;
  TestFixture::ts_insert_helper(TestFixture::timers[2]);

  TimerPair to_delete;
  TestFixture::ts->fetch(3, to_delete);

  cwtest_advance_time_ms(interval_ms + TIMER_GRANULARITY_MS);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);

  EXPECT_TRUE(next_timers.empty());
  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, FetchNonExistentTimer)
{
  TestFixture::ts_insert_helper(TestFixture::timers[2]);

  TimerPair to_delete;
  bool succ = TestFixture::ts->fetch(4, to_delete);

  EXPECT_FALSE(succ);
  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::tombstone;
}


TYPED_TEST(TestTimerStore, UpdateTimer)
{
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  // Replace timer one, using a newer timer with the same ID.
  TestFixture::timers[1]->id = 1;
  TestFixture::timers[1]->start_time_mono_ms++;
  TestFixture::ts_insert_helper(TestFixture::timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer one was deleted when it was overwritten
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, DontUpdateTimerAge)
{
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  // Attempt to replace timer one but the replacement is older
  TestFixture::timers[1]->id = 1;
  TestFixture::timers[1]->start_time_mono_ms--;

  TestFixture::ts_insert_helper(TestFixture::timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer two was deleted when it failed to overwrite timer one
  delete TestFixture::timers[0];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, DontUpdateTimerSeqNo)
{
  TestFixture::timers[0]->sequence_number++;
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  // Attempt to replace timer one but the replacement has a lower SeqNo
  TestFixture::timers[1]->id = 1;
  TestFixture::ts_insert_helper(TestFixture::timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer two was deleted when it failed to overwrite timer one
  delete TestFixture::timers[0];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, AddTombstone)
{
  TestFixture::ts_insert_helper(TestFixture::tombstonepair);

  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(1000000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, MixtureOfTimerLengths)
{
  // Add timers that all pop at the same time, but in such a way that one ends
  // up in the short wheel, one in the long wheel, and one in the heap.  Check
  // they pop at the same time.
  std::unordered_set<TimerPair> next_timers;

  // Timers all pop 1hr, 1s, 500ms from the start of the test.
  // Set timer 1.
  TestFixture::timers[0]->interval_ms = ((60 * 60 * 1000) + (1 * 1000) + 500);
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  // Move on by 1hr. Nothing has popped.
  cwtest_advance_time_ms(60 * 60 * 1000);
  TestFixture::timers[1]->start_time_mono_ms += (60 * 60 * 1000);
  TestFixture::timers[2]->start_time_mono_ms += (60 * 60 * 1000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Timer 2 pops in 1s, 500ms
  TestFixture::timers[1]->interval_ms = ((1 * 1000) + 500);
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  // Move on by 1s. Nothing has popped.
  cwtest_advance_time_ms(1 * 1000);
  TestFixture::timers[2]->start_time_mono_ms += (1 * 1000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Timer 3 pops in 500ms.
  TestFixture::timers[2]->interval_ms = 500;
  TestFixture::ts_insert_helper(TestFixture::timers[2]);

  // Move on by 500ms. All timers pop.
  cwtest_advance_time_ms(500 + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(3u, next_timers.size());
  next_timers.clear();

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, TimerPopsOnTheHour)
{
  std::unordered_set<TimerPair> next_timers;
  uint32_t pop_time_ms;

  pop_time_ms = (TestFixture::timers[0]->start_time_mono_ms / (60 * 60 * 1000));
  pop_time_ms += 2;
  pop_time_ms *= (60 * 60 * 1000);
  TestFixture::timers[0]->interval_ms = pop_time_ms - TestFixture::timers[0]->start_time_mono_ms;
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  // Move on to the pop time. The timer pops correctly.
  cwtest_advance_time_ms(pop_time_ms - TestFixture::timers[0]->start_time_mono_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());
  next_timers.clear();

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, PopOverdueTimer)
{
  cwtest_advance_time_ms(500);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);

  TestFixture::ts_insert_helper(TestFixture::timers[0]);
  TestFixture::ts->fetch_next_timers(next_timers);

  ASSERT_EQ(1u, next_timers.size());
  TestFixture::timers[0] = (*next_timers.begin()).active_timer;
  EXPECT_EQ(1u, TestFixture::timers[0]->id);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, DeleteOverdueTimer)
{
  cwtest_advance_time_ms(500);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);

  TestFixture::ts_insert_helper(TestFixture::timers[0]);
  TimerPair to_delete;
  TestFixture::ts->fetch(1, to_delete);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(0u, next_timers.size());

  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, AddClusterViewId)
{
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  std::map<std::string, std::unordered_set<TimerID>>::iterator map_it;
  map_it = TestFixture::ts->_timer_view_id_table.find("cluster-view-id");
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_view_id_table.end());
  EXPECT_EQ(1u, map_it->second.size());

  TimerPair pair = TestFixture::ts->_timer_lookup_id_table[*(map_it->second.begin())];
  EXPECT_EQ(1u, pair.active_timer->id);
}

TYPED_TEST(TestTimerStore, GetByViewId)
{
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  EXPECT_FALSE(TestFixture::ts->_timer_view_id_table.empty());

  std::vector<TimerPair> view_timers;
  bool succ = TestFixture::ts->get_by_view_id("updated-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_EQ(1u, view_timers.size());
  EXPECT_EQ(1u, view_timers.front().active_timer->id);

  // Check this timer hasn't been removed by this operation
  view_timers.clear();
  succ = TestFixture::ts->get_by_view_id("updated-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_EQ(1u, view_timers.size());
  EXPECT_EQ(1u, view_timers.front().active_timer->id);

  // Add another timer with the new cluster id
  TestFixture::timers[1]->cluster_view_id = "updated-cluster-view-id";
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  view_timers.clear();
  succ = TestFixture::ts->get_by_view_id("updated-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_EQ(1u, view_timers.size());
  EXPECT_EQ(1u, view_timers.front().active_timer->id);

  // Get both timers
  view_timers.clear();
  succ = TestFixture::ts->get_by_view_id("updated-again-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_EQ(2u, view_timers.size());
  EXPECT_EQ(1u, view_timers.front().active_timer->id);
  EXPECT_EQ(2u, view_timers.back().active_timer->id);

  // Only return 1 if that is our maximum
  view_timers.clear();
  succ = TestFixture::ts->get_by_view_id("updated-again-cluster-view-id", 1, view_timers);
  EXPECT_FALSE(succ);
  EXPECT_EQ(1u, view_timers.size());
}

TYPED_TEST(TestTimerStore, UpdateViewId)
{
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  EXPECT_FALSE(TestFixture::ts->_timer_view_id_table.empty());

  std::vector<TimerPair> view_timers;
  bool succ = TestFixture::ts->get_by_view_id("updated-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_EQ(1u, view_timers.size());
  EXPECT_EQ(1u, view_timers.front().active_timer->id);

  // Fetch the relevant timer (mimicking the Timer Handler)
  TimerPair pair;
  TestFixture::ts->fetch(1u, pair);

  EXPECT_TRUE(pair.active_timer);

  // Ensure it has been removed from view id index
  view_timers.clear();
  succ = TestFixture::ts->get_by_view_id("updated-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_TRUE(view_timers.empty());

  // Update view id and return to store
  pair.active_timer->cluster_view_id = "updated-cluster-view-id";
  TestFixture::ts_insert_helper(pair);

  // This should now not appear when we search in the same epoch
  view_timers.clear();
  succ = TestFixture::ts->get_by_view_id("updated-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_TRUE(view_timers.empty());

  // If the epoch changes again we should be able to find it
  view_timers.clear();
  succ = TestFixture::ts->get_by_view_id("updated-again-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_EQ(1u, view_timers.size());
  EXPECT_EQ(1u, view_timers.front().active_timer->id);
}

// When we insert a TimerPair with an information timer, we should put it in
// both cluster view ids.
TYPED_TEST(TestTimerStore, ClusterViewDoubleReference)
{
  Timer* info_timer = default_timer(1);
  info_timer->cluster_view_id = "old-cluster-view-id";

  TimerPair pair;
  pair.active_timer = TestFixture::timers[0];
  pair.information_timer = info_timer;

  TestFixture::ts_insert_helper(pair);

  std::vector<TimerPair> view_timers;
  bool succ = TestFixture::ts->get_by_view_id("random-cluster-view-id", 5, view_timers);
  EXPECT_TRUE(succ);
  EXPECT_EQ(2u, view_timers.size());
  EXPECT_EQ(1u, view_timers.front().active_timer->id);

}


// Test that updating a timer with a new cluster ID causes the original
// timer to be saved off.
//
// WARNING: In this test we look directly in the timer store as there's no
// other way to test what's in the timer map (when it's not also in the timer
// wheel)
TYPED_TEST(TestTimerStore, UpdateClusterViewID)
{
  // Add the first timer with ID 1
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  // Find this timer in the store, and check there's no saved timers
  // associated with it
  std::map<TimerID, TimerPair>::iterator map_it = TestFixture::ts->_timer_lookup_id_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_id_table.end());
  EXPECT_EQ(NULL, map_it->second.information_timer);
  EXPECT_EQ(1u, map_it->second.active_timer->id);

  // Add a new timer with the same ID, and an updated Cluster View ID
  TestFixture::timers[1]->id = 1;
  TestFixture::timers[1]->cluster_view_id = "updated-cluster-view-id";

  TimerPair pair1;
  pair1.active_timer = TestFixture::timers[1];
  pair1.information_timer = TestFixture::timers[0];

  TestFixture::ts_insert_helper(pair1);

  // Find this timer in the store, and check there's a saved timer. The saved
  // timer has the old cluster view ID, and the new timer has the new one.
  map_it = TestFixture::ts->_timer_lookup_id_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_id_table.end());
  EXPECT_TRUE(map_it->second.information_timer);
  EXPECT_EQ(1u, map_it->second.active_timer->id);
  EXPECT_EQ("updated-cluster-view-id", map_it->second.active_timer->cluster_view_id);
  EXPECT_EQ(1u, map_it->second.information_timer->id);
  EXPECT_EQ("cluster-view-id", map_it->second.information_timer->cluster_view_id);

  // Add a new timer with the same ID, an updated Cluster View ID,
  // and make it a tombstone
  TestFixture::timers[2]->id = 1;
  TestFixture::timers[2]->cluster_view_id = "updated-again-cluster-view-id";
  TestFixture::timers[2]->become_tombstone();

  TimerPair pair2;
  pair2.active_timer = TestFixture::timers[2];
  pair2.information_timer = TestFixture::timers[1];

  TestFixture::ts_insert_helper(pair2);

  // Find this timer in the store, and check there's a saved timer. The saved
  // timer has the old cluster view ID, and the new timer has the new one.
  map_it = TestFixture::ts->_timer_lookup_id_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_id_table.end());
  EXPECT_TRUE(map_it->second.information_timer);
  EXPECT_EQ(1u, map_it->second.active_timer->id);
  EXPECT_EQ("updated-again-cluster-view-id", map_it->second.active_timer->cluster_view_id);
  EXPECT_TRUE(map_it->second.active_timer->is_tombstone());
  EXPECT_EQ(1u, map_it->second.information_timer->id);
  EXPECT_EQ("updated-cluster-view-id", map_it->second.information_timer->cluster_view_id);
  EXPECT_FALSE(map_it->second.information_timer->is_tombstone());

  delete TestFixture::tombstone;
}


// Test that if there is an information timer with an old view,
// we return it even if the active timer is current
TYPED_TEST(TestTimerStore, SelectTimersTakeInformationalTimers)
{
  TestFixture::timers[0]->cluster_view_id = "old-cluster-view-id";
  TestFixture::ts_insert_helper(TestFixture::timers[0]);

  TestFixture::timers[1]->id = 1;
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  std::map<std::string, std::unordered_set<TimerID>>::iterator map_it;
  map_it = TestFixture::ts->_timer_view_id_table.find("cluster-view-id");
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_view_id_table.end());
  EXPECT_EQ(1u, map_it->second.size());

  TimerPair pair = TestFixture::ts->_timer_lookup_id_table[*(map_it->second.begin())];
  EXPECT_EQ(1u, pair.active_timer->id);
}


/*
// Test that the store uses the saved timers (rather than the timers in the
// timer wheel) when updating the replica tracker or handling get requests.
//
// WARNING: In this test we look directly in the timer store as there's no
// other way to test what's in the timer map (when it's not also in the timer
// wheel)
TYPED_TEST(TestTimerStore, ModifySavedTimers)
{
  // Add a timer to the store with an old cluster ID and three replicas
  TestFixture::timers[0]->cluster_view_id = "old-cluster-view-id";
  TestFixture::timers[0]->_replica_tracker = 7;
  TestFixture::timers[0]->replicas.push_back("10.0.0.2:9999");
  TestFixture::timers[0]->replicas.push_back("10.0.0.3:9999");
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

  // Add a timer to the store with the same ID as the previous timer,
  // but an updated cluster-view ID. This will take the original timer
  // out of the timer wheel and save it just in the map
  TestFixture::timers[1]->id = 1;
  TestFixture::timers[1]->_replica_tracker = 7;
  TestFixture::timers[1]->replicas.push_back("10.0.0.2:9999");
  TestFixture::timers[1]->replicas.push_back("10.0.0.3:9999");
  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::timerpairs[1].information_timer = TextFixture::timerpairs[0].active_timer;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

  // Update the replica tracker for Timer ID 1. This should update the
  // saved timer to mark that the third replica has been informed, not
  // the new first timer.
  TestFixture::ts->update_replica_tracker_for_timer(1u, 2);
  std::map<TimerID, TimerPair>::iterator map_it =
                                                    TestFixture::ts->_timer_lookup_id_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_id_table.end());
  EXPECT_EQ(2u, map_it->second.size());
  EXPECT_EQ(7u, map_it->second.front()->_replica_tracker);
  EXPECT_EQ(3u, map_it->second.back()->_replica_tracker);

  // Now update the timer. This should change the first timer but not the
  // second timer in the timer map
  TestFixture::timers[2]->id = 1;
  TestFixture::timers[2]->_replica_tracker = 7;
  TimerID id = TestFixture::timers[2]->id;
  uint32_t next_pop_time = TestFixture::timers[2]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);
  map_it = TestFixture::ts->_timer_lookup_id_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_id_table.end());
  EXPECT_EQ(2u, map_it->second.size());
  EXPECT_EQ(7u, map_it->second.front()->_replica_tracker);
  EXPECT_EQ(1u, map_it->second.front()->replicas.size());
  EXPECT_EQ(3u, map_it->second.back()->_replica_tracker);
  EXPECT_EQ(3u, map_it->second.back()->replicas.size());

  // Finally, update the replica tracker to mark all replicas
  // as having been informed for Timer ID 1. This should
  // delete the saved timer.
  TestFixture::ts->update_replica_tracker_for_timer(1u, 0);
  map_it = TestFixture::ts->_timer_lookup_id_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_id_table.end());
  EXPECT_EQ(1u, map_it->second.size());
  std::string get_response;
  TestFixture::ts->get_timers_for_node("10.0.0.1:9999", 1, "cluster-view-id", get_response);
  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  delete TestFixture::tombstone;
}
*/

