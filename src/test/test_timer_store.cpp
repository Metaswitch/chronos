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
      TimerPair pair;
      pair.active_timer = timers[ii];
      timerpairs[ii] = pair;
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

  // Variables under test.
  TimerStore* ts;
  Timer* timers[3];
  TimerPair timerpairs[3];
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
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
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

  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

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
  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
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
  TimerID id = TestFixture::timers[2]->id;
  uint32_t next_pop_time = TestFixture::timers[2]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);
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

  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  id = TestFixture::timers[1]->id;
  next_pop_time = TestFixture::timers[1]->next_pop_time();
  cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

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

  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  id = TestFixture::timers[1]->id;
  next_pop_time = TestFixture::timers[1]->next_pop_time();
  cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

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

  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  id = TestFixture::timers[1]->id;
  next_pop_time = TestFixture::timers[1]->next_pop_time();
  cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

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

  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  id = TestFixture::timers[1]->id;
  next_pop_time = TestFixture::timers[1]->next_pop_time();
  cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
  id = TestFixture::timers[2]->id;
  next_pop_time = TestFixture::timers[2]->next_pop_time();
  cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);

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
  TimerID id = TestFixture::timers[2]->id;
  uint32_t next_pop_time = TestFixture::timers[2]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);

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
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
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
  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
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
  TimerID id = TestFixture::timers[2]->id;
  uint32_t next_pop_time = TestFixture::timers[2]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);
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

TYPED_TEST(TestTimerStore, UpdateTimer)
{
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

  // Replace timer one, using a newer timer with the same ID.
  TestFixture::timers[1]->id = 1;
  TestFixture::timers[1]->start_time_mono_ms++;
  id = TestFixture::timers[1]->id;
  next_pop_time = TestFixture::timers[1]->next_pop_time();
  cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
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
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

  // Attempt to replace timer one but the replacement is older
  TestFixture::timers[1]->id = 1;
  TestFixture::timers[1]->start_time_mono_ms--;
  id = TestFixture::timers[1]->id;
  next_pop_time = TestFixture::timers[1]->next_pop_time();
  cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
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
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

  // Attempt to replace timer one but the replacement has a lower SeqNo
  TestFixture::timers[1]->id = 1;
  id = TestFixture::timers[1]->id;
  next_pop_time = TestFixture::timers[1]->next_pop_time();
  cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
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
  TimerID id = TestFixture::tombstone->id;
  uint32_t next_pop_time = TestFixture::tombstone->next_pop_time();
  std::string cluster_view_id = TestFixture::tombstone->cluster_view_id;
  TestFixture::ts->insert(TestFixture::tombstonepair, id, next_pop_time, cluster_view_id);

  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(1000000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

TYPED_TEST(TestTimerStore, OverwriteWithTombstone)
{
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  id = TestFixture::tombstone->id;
  next_pop_time = TestFixture::tombstone->next_pop_time();
  cluster_view_id = TestFixture::tombstone->cluster_view_id;
  TestFixture::ts->insert(TestFixture::tombstonepair, id, next_pop_time, cluster_view_id);

  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(1000000);
  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());

  Timer* extracted = (*next_timers.begin()).active_timer;
  EXPECT_TRUE(extracted->is_tombstone());
  EXPECT_EQ(100u, extracted->interval_ms);
  EXPECT_EQ(100u, extracted->repeat_for);

  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

// Test for issue #19, even if time is moving in non-10ms steps
// we should be able to reliably update/TestFixture::tombstone timers.
TYPED_TEST(TestTimerStore, Non10msShortTimerUpdate)
{
  // Offset the interval_ms of the first timer so it's not a multiple of 10ms.
  TestFixture::timers[0]->interval_ms += 4;

  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

  // Move time on more than the timer's shift but less than 10ms, even
  // after this the timer store should know which bucket the timer is in.
  cwtest_advance_time_ms(8);

  // Attempting to get a set of timers updates the internal clock in the
  // timer store.
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Now, to prove the timer store can still find the timer, update it to
  // a TestFixture::tombstone.
  id = TestFixture::tombstone->id;
  next_pop_time = TestFixture::tombstone->next_pop_time();
  cluster_view_id = TestFixture::tombstone->cluster_view_id;
  TestFixture::ts->insert(TestFixture::tombstonepair, id, next_pop_time, cluster_view_id);

  // No timers are ready to pop yet
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Move on till the TestFixture::tombstone should pop (50 ms offset from timer[0])
  cwtest_advance_time_ms(150 + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());
  next_timers.clear();

  // Move on again to ensure there are no more timers in the store.
  cwtest_advance_time_ms(100000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // timer[0] was deleted when it was updated in the timer store.
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

// Test for issue #19
TYPED_TEST(TestTimerStore, Non10msMediumTimerUpdate)
{
  // Offset the interval of the second timer so it's not a multiple of 10ms.
  TestFixture::timers[1]->interval_ms += 4;

  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

  // Move time on to less than 1s but closer than the timer's offset, even
  // after this the timer store should know which bucket the timer is in.
  cwtest_advance_time_ms(990 + 8);

  // Attempting to get a set of timers updates the internal clock in the
  // timer store.
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Now, to prove the timer store can still find the timer, update it to
  // a TestFixture::tombstone.
  TestFixture::tombstone->id = TestFixture::timers[1]->id;
  id = TestFixture::tombstone->id;
  next_pop_time = TestFixture::tombstone->next_pop_time();
  cluster_view_id = TestFixture::tombstone->cluster_view_id;
  TestFixture::ts->insert(TestFixture::tombstonepair, id, next_pop_time, cluster_view_id);

  // No timers are ready to pop yet
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Move on till the timer should pop
  cwtest_advance_time_ms(100000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());
  next_timers.clear();

  // Move on again to ensure there are no more timers in the store.
  cwtest_advance_time_ms(100000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // timer[1] was deleted when it was updated in the timer store.
  delete TestFixture::timers[0];
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
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

  // Move on by 1hr. Nothing has popped.
  cwtest_advance_time_ms(60 * 60 * 1000);
  TestFixture::timers[1]->start_time_mono_ms += (60 * 60 * 1000);
  TestFixture::timers[2]->start_time_mono_ms += (60 * 60 * 1000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Timer 2 pops in 1s, 500ms
  TestFixture::timers[1]->interval_ms = ((1 * 1000) + 500);
  id = TestFixture::timers[1]->id;
  next_pop_time = TestFixture::timers[1]->next_pop_time();
  cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

  // Move on by 1s. Nothing has popped.
  cwtest_advance_time_ms(1 * 1000);
  TestFixture::timers[2]->start_time_mono_ms += (1 * 1000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(0u, next_timers.size());

  // Timer 3 pops in 500ms.
  TestFixture::timers[2]->interval_ms = 500;
  id = TestFixture::timers[2]->id;
  next_pop_time = TestFixture::timers[2]->next_pop_time();
  cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);

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
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

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

  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
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

  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  TimerPair to_delete;
  TestFixture::ts->fetch(1, to_delete);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(0u, next_timers.size());

  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}
/*
// Test that marking some of the replicas as being informed
// doesn't change the timer if it's got an up-to-date
// cluster view ID
TYPED_TEST(TestTimerStore, UpdateReplicaTrackerValueForNewTimer)
{
  cwtest_advance_time_ms(500);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::timers[0]->_replica_tracker = 15;
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  TestFixture::ts->update_replica_tracker_for_timer(1u, 3);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  TestFixture::timers[0] = (*next_timers.begin()).active_timer;
  ASSERT_EQ(15u, TestFixture::timers[0]->_replica_tracker);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

// Test that marking some of the replicas as being informed
// changes the replica tracker if the cluster view ID is
// different
TYPED_TEST(TestTimerStore, UpdateReplicaTrackerValueForOldTimer)
{
  cwtest_advance_time_ms(500);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::timers[0]->_replica_tracker = 15;
  TestFixture::timers[0]->cluster_view_id = "different-id";
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  TestFixture::ts->update_replica_tracker_for_timer(1u, 3);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(1u, next_timers.size());
  TestFixture::timers[0] = (*next_timers.begin()).active_timer;
  ASSERT_EQ(7u, TestFixture::timers[0]->_replica_tracker);

  delete TestFixture::timers[0];
  delete TestFixture::timers[1];
  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

// Test that getting timers for a node returns the set of timers
// (up to the maximum requested)
TYPED_TEST(TestTimerStore, SelectTimers)
{
  std::unordered_set<TimerPair> next_timers;
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
  TimerID id = TestFixture::timers[2]->id;
  uint32_t next_pop_time = TestFixture::timers[2]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);
  std::string get_response;

  std::string updated_cluster_view_id = "updated-cluster-view-id";
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_addresses(cluster_addresses);
  __globals->set_cluster_view_id(updated_cluster_view_id);
  __globals->unlock();

  TestFixture::ts->get_timers_for_node("10.0.0.1:9999", 2, updated_cluster_view_id, get_response);

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

  delete TestFixture::tombstone;
}

// Test that if there are no timers for the requesting node,
// that trying to get the timers returns an empty list
TYPED_TEST(TestTimerStore, SelectTimersTakeInformationalTimers)
{
  std::unordered_set<TimerPair> next_timers;

  // Add a timer to the store, then update it with a new cluster view ID.
  TestFixture::timers[0]->cluster_view_id = "old-cluster-view-id";
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  TestFixture::timers[1]->id = 1;
  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

  std::string get_response;
  TestFixture::ts->get_timers_for_node("10.0.0.3:9999", 1, "cluster-view-id", get_response);

  // Check that the response is based on the informational timer, rather than the timer
  // in the timer wheel (the uri should be callback1 rather than callback2)
  std::string exp_rsp = "\\\{\"Timers\":\\\[\\\{\"TimerID\":1,\"OldReplicas\":\\\[\"10.0.0.1:9999\"],\"Timer\":\\\{\"timing\":\\\{\"start-time\".*,\"sequence-number\":0,\"interval\":0,\"repeat-for\":0},\"callback\":\\\{\"http\":\\\{\"uri\":\"localhost:80/callback1\",\"opaque\":\"stuff stuff stuff\"}},\"reliability\":\\\{\"cluster-view-id\":\"cluster-view-id\",\"replicas\":\\\[\"10.0.0.3:9999\"]},\"statistics\":\\\{\"tags\":\\\[]}}}]}";
  EXPECT_THAT(get_response, MatchesRegex(exp_rsp));

  delete TestFixture::timers[2];
  delete TestFixture::tombstone;
}

// Test that if there are no timers for the requesting node,
// that trying to get the timers returns an empty list
TYPED_TEST(TestTimerStore, SelectTimersNoMatchesReqNode)
{
  std::unordered_set<TimerPair> next_timers;
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
  TimerID id = TestFixture::timers[2]->id;
  uint32_t next_pop_time = TestFixture::timers[2]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);
  std::string get_response;
  TestFixture::ts->get_timers_for_node("10.0.0.4:9999", 1, "cluster-view-id", get_response);

  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  delete TestFixture::tombstone;
}

// Test that if there are no timers with an out of date cluster
// ID then trying to get the timers returns an empty list
TYPED_TEST(TestTimerStore, SelectTimersNoMatchesClusterID)
{
  std::unordered_set<TimerPair> next_timers;
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);
  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);
  TimerID id = TestFixture::timers[2]->id;
  uint32_t next_pop_time = TestFixture::timers[2]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);
  std::string get_response;
  TestFixture::ts->get_timers_for_node("10.0.0.1:9999", 1, "cluster-view-id", get_response);

  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  delete TestFixture::tombstone;
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
  TimerID id = TestFixture::timers[0]->id;
  uint32_t next_pop_time = TestFixture::timers[0]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[0]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[0], id, next_pop_time, cluster_view_id);

  // Find this timer in the store, and check there's no saved timers
  // associated with it
  std::map<TimerID, TimerPair>::iterator map_it =
                                                    TestFixture::ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_table.end());
  EXPECT_EQ(1u, map_it->second.size());
  EXPECT_EQ(1u, map_it->second.front()->id);

  // Add a new timer with the same ID, and an updated Cluster View ID
  TestFixture::timers[1]->id = 1;
  TestFixture::timers[1]->cluster_view_id = "updated-cluster-view-id";
  TimerID id = TestFixture::timers[1]->id;
  uint32_t next_pop_time = TestFixture::timers[1]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[1]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

  // Find this timer in the store, and check there's a saved timer. The saved
  // timer has the old cluster view ID, and the new timer has the new one.
  map_it = TestFixture::ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_table.end());
  EXPECT_EQ(2u, map_it->second.size());
  EXPECT_EQ(1u, map_it->second.front()->id);
  EXPECT_EQ("updated-cluster-view-id", map_it->second.front()->cluster_view_id);
  EXPECT_EQ(1u, map_it->second.back()->id);
  EXPECT_EQ("cluster-view-id", map_it->second.back()->cluster_view_id);

  // Add a new timer with the same ID, an updated Cluster View ID,
  // and make it a TestFixture::tombstone
  TestFixture::timers[2]->id = 1;
  TestFixture::timers[2]->cluster_view_id = "updated-again-cluster-view-id";
  TestFixture::timers[2]->become_tombstone();
  TimerID id = TestFixture::timers[2]->id;
  uint32_t next_pop_time = TestFixture::timers[2]->next_pop_time();
  std::string cluster_view_id = TestFixture::timers[2]->cluster_view_id;
  TestFixture::ts->insert(TestFixture::timerpairs[2], id, next_pop_time, cluster_view_id);

  // Find this timer in the store, and check there's a saved timer. The saved
  // timer has the old cluster view ID, and the new timer has the new one.
  map_it = TestFixture::ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_table.end());
  EXPECT_EQ(2u, map_it->second.size());
  EXPECT_EQ(1u, map_it->second.front()->id);
  EXPECT_EQ("updated-again-cluster-view-id", map_it->second.front()->cluster_view_id);
  EXPECT_TRUE(map_it->second.front()->is_tombstone());
  EXPECT_EQ(1u, map_it->second.back()->id);
  EXPECT_EQ("updated-cluster-view-id", map_it->second.back()->cluster_view_id);
  EXPECT_FALSE(map_it->second.back()->is_tombstone());

  delete TestFixture::tombstone;
}

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
  TestFixture::ts->insert(TestFixture::timerpairs[1], id, next_pop_time, cluster_view_id);

  // Update the replica tracker for Timer ID 1. This should update the
  // saved timer to mark that the third replica has been informed, not
  // the new first timer.
  TestFixture::ts->update_replica_tracker_for_timer(1u, 2);
  std::map<TimerID, TimerPair>::iterator map_it =
                                                    TestFixture::ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_table.end());
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
  map_it = TestFixture::ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_table.end());
  EXPECT_EQ(2u, map_it->second.size());
  EXPECT_EQ(7u, map_it->second.front()->_replica_tracker);
  EXPECT_EQ(1u, map_it->second.front()->replicas.size());
  EXPECT_EQ(3u, map_it->second.back()->_replica_tracker);
  EXPECT_EQ(3u, map_it->second.back()->replicas.size());

  // Finally, update the replica tracker to mark all replicas
  // as having been informed for Timer ID 1. This should
  // delete the saved timer.
  TestFixture::ts->update_replica_tracker_for_timer(1u, 0);
  map_it = TestFixture::ts->_timer_lookup_table.find(1);
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_lookup_table.end());
  EXPECT_EQ(1u, map_it->second.size());
  std::string get_response;
  TestFixture::ts->get_timers_for_node("10.0.0.1:9999", 1, "cluster-view-id", get_response);
  ASSERT_EQ(get_response, "{\"Timers\":[]}");

  delete TestFixture::tombstone;
}*/

