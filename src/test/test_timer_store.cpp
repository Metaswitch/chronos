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

  // Since the Timer is not allowed to delete timers, we can clean them up here.
  virtual void TearDown()
  {
    delete timers[0]; timers[0] = NULL;
    delete timers[1]; timers[1] = NULL;
    delete timers[2]; timers[2] = NULL;
    delete tombstone; tombstone = NULL;

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
}

TYPED_TEST(TestTimerStore, MultipleReallyLongTimers)
{
  // Lengthen timers two and three to really long (10 hours)
  TestFixture::timers[1]->interval_ms = (3600 * 1000) * 10;
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  TestFixture::timers[2]->interval_ms = (3600 * 1000) * 10;
  TestFixture::ts_insert_helper(TestFixture::timers[2]);

  std::unordered_set<TimerPair> next_timers;

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(0u, next_timers.size());

  cwtest_advance_time_ms(((3600 * 1000) * 10) + TIMER_GRANULARITY_MS);

  TestFixture::ts->fetch_next_timers(next_timers);
  ASSERT_EQ(2u, next_timers.size()) << "Bucket should have 2 timers";
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
}

TYPED_TEST(TestTimerStore, DeleteMidTimer)
{
  uint32_t interval_ms = TestFixture::timers[1]->interval_ms;
  TestFixture::ts_insert_helper(TestFixture::timers[1]);

  TimerPair to_delete;
  TestFixture::ts->fetch(2, to_delete);

  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);

  EXPECT_TRUE(next_timers.empty());
}

TYPED_TEST(TestTimerStore, DeleteLongTimer)
{
  uint32_t interval_ms = TestFixture::timers[2]->interval_ms;
  TestFixture::ts_insert_helper(TestFixture::timers[2]);

  TimerPair to_delete;
  bool succ = TestFixture::ts->fetch(3, to_delete);

  EXPECT_TRUE(succ);

  cwtest_advance_time_ms(interval_ms + TIMER_GRANULARITY_MS);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);

  EXPECT_TRUE(next_timers.empty());
}

TYPED_TEST(TestTimerStore, DeleteHeapTimer)
{
  TestFixture::timers[2]->interval_ms = (3600 * 1000) * 10;
  uint32_t interval_ms = TestFixture::timers[2]->interval_ms;
  TestFixture::ts_insert_helper(TestFixture::timers[2]);

  TimerPair to_delete;
  bool succ = TestFixture::ts->fetch(3, to_delete);

  EXPECT_TRUE(succ);

  cwtest_advance_time_ms(interval_ms + TIMER_GRANULARITY_MS);
  std::unordered_set<TimerPair> next_timers;
  TestFixture::ts->fetch_next_timers(next_timers);

  EXPECT_TRUE(next_timers.empty());
}

TYPED_TEST(TestTimerStore, FetchNonExistentTimer)
{
  TimerPair to_delete;
  bool succ = TestFixture::ts->fetch(4, to_delete);

  EXPECT_FALSE(succ);
}

TYPED_TEST(TestTimerStore, AddTombstone)
{
  TestFixture::ts_insert_helper(TestFixture::tombstonepair);

  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(1000000);
  TestFixture::ts->fetch_next_timers(next_timers);
  EXPECT_EQ(1u, next_timers.size());

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


  // Get the timer out of the store to clean up properly
  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(TestFixture::timers[0]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
}

// Check to see we can find timers by their cluster ID
TYPED_TEST(TestTimerStore, GetByViewId)
{
  TimerPair pair1;
  pair1.active_timer = TestFixture::timers[0];
  TestFixture::ts_insert_helper(pair1);

  // This timer should exist in our mapping
  EXPECT_FALSE(TestFixture::ts->_timer_view_id_table.empty());

  // We should be able to find it by querying a difference cluster ID
  TimerStore::TSIterator it = TestFixture::ts->begin("updated-cluster-view-id");
  EXPECT_EQ(pair1, *it);
  EXPECT_EQ(++it, TestFixture::ts->end());

  // Check this timer hasn't been removed by this operation
  it = TestFixture::ts->begin("updated-cluster-view-id");
  EXPECT_EQ(pair1, *it);
  EXPECT_EQ(++it, TestFixture::ts->end());

  // Add another timer with the new cluster id
  TimerPair pair2;
  pair2.active_timer = TestFixture::timers[1];
  TestFixture::timers[1]->cluster_view_id = "updated-cluster-view-id";
  TestFixture::ts_insert_helper(pair2);

  it = TestFixture::ts->begin("updated-cluster-view-id");
  EXPECT_EQ(pair1, *it);
  EXPECT_EQ(++it, TestFixture::ts->end());

  // Get both timers
  it = TestFixture::ts->begin("updated-again-cluster-view-id");
  EXPECT_EQ(pair1, *it);
  EXPECT_EQ(pair2, *++it);
  EXPECT_EQ(++it, TestFixture::ts->end());

  // Get the timers out of the store to clean up properly
  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(TestFixture::timers[0]->interval_ms +
                         TestFixture::timers[1]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
}

TYPED_TEST(TestTimerStore, UpdateViewId)
{
  TimerPair pair1;
  pair1.active_timer = TestFixture::timers[0];
  TestFixture::ts_insert_helper(pair1);

  EXPECT_FALSE(TestFixture::ts->_timer_view_id_table.empty());

  TimerStore::TSIterator it = TestFixture::ts->begin("updated-cluster-view-id");
  EXPECT_EQ(*it, pair1);
  EXPECT_EQ(++it, TestFixture::ts->end());

  // Fetch the relevant timer (mimicking the Timer Handler)
  TimerPair fetch_pair;
  TestFixture::ts->fetch(1u, fetch_pair);

  EXPECT_TRUE(fetch_pair.active_timer);
  EXPECT_TRUE(TestFixture::ts->_timer_view_id_table.empty());

  // Ensure it has been removed from view id index
  TimerStore::TSIterator it2 = TestFixture::ts->begin("updated-cluster-view-id");
  EXPECT_EQ(it2, TestFixture::ts->end());

  // Update view id and return to store
  fetch_pair.active_timer->cluster_view_id = "updated-cluster-view-id";
  TestFixture::ts_insert_helper(fetch_pair);

  // This should now not appear when we search in the same epoch
  it = TestFixture::ts->begin("updated-cluster-view-id");
  EXPECT_EQ(it, TestFixture::ts->end());

  // If the epoch changes again we should be able to find it
  it = TestFixture::ts->begin("updated-again-cluster-view-id");
  EXPECT_EQ(*it, fetch_pair);
  EXPECT_EQ(++it, TestFixture::ts->end());

  // Get the timers out of the store to clean up properly
  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(TestFixture::timers[0]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
}


// A TimerPair with a different cluster view id on active timer and information
// timer
TYPED_TEST(TestTimerStore, MultipleViewId)
{
  TimerPair pair;

  TestFixture::timers[1]->cluster_view_id = "updated-cluster-view-id";
  TestFixture::timers[1]->id = TestFixture::timers[0]->id;

  pair.active_timer = TestFixture::timers[0];
  pair.information_timer = TestFixture::timers[1];

  TestFixture::ts_insert_helper(pair);

  // We should be able to get the TimerPair using the alternative timer to the
  // one requested
  TimerStore::TSIterator it = TestFixture::ts->begin("updated-cluster-view-id");
  EXPECT_EQ(*it, pair);
  EXPECT_EQ(++it, TestFixture::ts->end());

  it = TestFixture::ts->begin("cluster-view-id");
  EXPECT_EQ(*it, pair);
  EXPECT_EQ(++it, TestFixture::ts->end());

  // But we should only get each timer pair at most once
  it = TestFixture::ts->begin("any-view-id");
  EXPECT_EQ(*it, pair);
  EXPECT_EQ(*(++it), pair);
  EXPECT_EQ(++it, TestFixture::ts->end());

  TimerPair return_pair;
  TestFixture::ts->fetch(1u, return_pair);

  EXPECT_EQ("cluster-view-id", return_pair.active_timer->cluster_view_id);
  EXPECT_EQ("updated-cluster-view-id", return_pair.information_timer->cluster_view_id);

  // Ensure it has been removed from both view id indices
  it = TestFixture::ts->begin("any-cluster-view-id");
  EXPECT_EQ(it, TestFixture::ts->end());

  // Get the timers out of the store to clean up properly
  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(TestFixture::timers[1]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
}

// When a timer pops the view ID should also be removed
TYPED_TEST(TestTimerStore, RemovingViewIdOnPop)
{
  TimerPair pair;
  TestFixture::timers[1]->id = 1;
  TestFixture::timers[1]->cluster_view_id = "updated-cluster-view-id";

  pair.active_timer = TestFixture::timers[0];
  pair.information_timer = TestFixture::timers[1];

  TestFixture::ts_insert_helper(pair);

  cwtest_advance_time_ms(100 + TIMER_GRANULARITY_MS);

  std::unordered_set<TimerPair> next_timers;

  TestFixture::ts->fetch_next_timers(next_timers);

  EXPECT_EQ(1u, next_timers.size());

  std::unordered_set<TimerPair> view_timers;

  TimerStore::TSIterator it = TestFixture::ts->begin("any-cluster-id");
  EXPECT_EQ(it, TestFixture::ts->end());
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

  TimerPair return_pair;
  TestFixture::ts->fetch(pair1.active_timer->id, return_pair);
  EXPECT_EQ(return_pair.active_timer, TestFixture::timers[0]);

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

  return_pair.active_timer = NULL;
  return_pair.information_timer = NULL;
  TestFixture::ts->fetch(pair2.active_timer->id, return_pair);
  EXPECT_EQ(return_pair, pair1);

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

  // Get the timers out of the store to clean up properly
  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(TestFixture::timers[0]->interval_ms +
                         TestFixture::timers[1]->interval_ms +
                         TestFixture::timers[2]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
}


// Test that if there is an information timer with an old view,
// we return it even if the active timer is current
TYPED_TEST(TestTimerStore, SelectTimersTakeInformationalTimers)
{
  TestFixture::timers[0]->cluster_view_id = "old-cluster-view-id";
  TestFixture::timers[1]->id = 1;

  TimerPair insert_pair;
  insert_pair.active_timer = TestFixture::timers[1];
  insert_pair.information_timer = TestFixture::timers[0];

  TestFixture::ts_insert_helper(insert_pair);

  std::map<std::string, std::unordered_set<TimerID>>::iterator map_it;
  map_it = TestFixture::ts->_timer_view_id_table.find("cluster-view-id");
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_view_id_table.end());
  EXPECT_EQ(1u, map_it->second.size());

  TimerPair pair = TestFixture::ts->_timer_lookup_id_table[*(map_it->second.begin())];
  EXPECT_EQ(1u, pair.active_timer->id);

  map_it = TestFixture::ts->_timer_view_id_table.find("old-cluster-view-id");
  EXPECT_TRUE(map_it != TestFixture::ts->_timer_view_id_table.end());
  EXPECT_EQ(1u, map_it->second.size());

  // Get the timers out of the store to clean up properly
  std::unordered_set<TimerPair> next_timers;
  cwtest_advance_time_ms(TestFixture::timers[0]->interval_ms +
                         TestFixture::timers[1]->interval_ms +
                         TestFixture::timers[2]->interval_ms + TIMER_GRANULARITY_MS);
  TestFixture::ts->fetch_next_timers(next_timers);
}
