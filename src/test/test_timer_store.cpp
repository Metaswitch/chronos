#include "timer_store.h"
#include "timer_helper.h"
#include "test_interposer.hpp"
#include "base.h"

#include <gtest/gtest.h>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TestTimerStore : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();

    ts = new TimerStore();

    // Force the current time to a known value.

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    for (int ii = 0; ii < 3; ii++)
    {
      timers[ii] = default_timer(ii + 1);
      timers[ii]->start_time = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));
    }

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
    delete ts;
    Base::TearDown();
  }

  // Accessors into private variables.
  std::unordered_set<Timer*>& _ten_ms_buckets(int ii) { return (ts->_ten_ms_buckets[ii]); }

  std::unordered_set<Timer*>& _s_buckets(int ii) { return (ts->_s_buckets[ii]); }

  std::vector<Timer*>& _extra_heap() { return (ts->_extra_heap); }

  // Variables under test.
  TimerStore* ts;
  Timer* timers[3];
  Timer* tombstone;
};

/*****************************************************************************/
/* Instance Functions                                                        */
/*****************************************************************************/

TEST_F(TestTimerStore, AddTimerTest)
{
  ts->add_timer(timers[0]);
  for (int ii = 0; ii < 100; ii++)
  {
    // Timer one pops in 100ms so is in bucket 10.
    if (ii != 10)
    {
      EXPECT_TRUE(_ten_ms_buckets(ii).empty()) << "Bucket " << ii << " should be empty";
    }
    else
    {
      EXPECT_EQ(1, _ten_ms_buckets(ii).size()) << "The timer should be in bucket 10";
    }
  }

  for (int ii = 0; ii < NUM_SECOND_BUCKETS; ii++)
  {
    EXPECT_TRUE(_s_buckets(ii).empty()) << "Bucket " << ii << " should be empty";
  }

  EXPECT_TRUE(_extra_heap().empty());

  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, AddTimersTest)
{
  std::unordered_set<Timer*> timer_set;
  timer_set.insert(timers[0]);
  timer_set.insert(timers[1]);
  ts->add_timers(timer_set);

  for (int ii = 0; ii < 100; ii++)
  {
    // Timer one pops in 100ms so is in bucket 10.
    if (ii != 10)
    {
      EXPECT_TRUE(_ten_ms_buckets(ii).empty()) << "Bucket " << ii << " should be empty";
    }
    else
    {
      EXPECT_EQ(1, _ten_ms_buckets(ii).size()) << "The timer should be in bucket " << ii;
    }
  }

  for (int ii = 0; ii < NUM_SECOND_BUCKETS; ii++)
  {
    // Timer 2 pops in 10100ms so is in the 9th second bucket.
    if (ii != 9)
    {
      EXPECT_TRUE(_s_buckets(ii).empty()) << "Bucket " << ii << " should be empty";
    }
    else
    {
      EXPECT_EQ(1, _s_buckets(ii).size()) << "The timer should be in bucket " << ii;
    }
  }

  EXPECT_TRUE(_extra_heap().empty());

  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, AddLongTimerTest)
{
  ts->add_timer(timers[2]);
  for (int ii = 0; ii < 100; ii++)
  {
    EXPECT_TRUE(_ten_ms_buckets(ii).empty()) << "Bucket " << ii << " should be empty";
  }

  for (int ii = 0; ii < NUM_SECOND_BUCKETS; ii++)
  {
    EXPECT_TRUE(_s_buckets(ii).empty()) << "Bucket " << ii << " should be empty";
  }

  EXPECT_EQ(1, _extra_heap().size());

  delete timers[0];
  delete timers[1];
  delete tombstone;
}

TEST_F(TestTimerStore, NearGetNextTimersTest)
{
  // I mark the hours, every one, Nor have I yet outrun the Sun.
  // My use and value, unto you, Are gauged by what you have to do.
  cwtest_completely_control_time();

  ts->add_timer(timers[0]);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ASSERT_EQ(0, next_timers.size());
  cwtest_advance_time_ms(1000);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size());
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1, timers[0]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, MidGetNextTimersTest)
{
  cwtest_completely_control_time();

  ts->add_timer(timers[1]);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ASSERT_EQ(0, next_timers.size());
  cwtest_advance_time_ms(100000);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size());
  timers[1] = *next_timers.begin();
  EXPECT_EQ(2, timers[1]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, LongGetNextTimersTest)
{
  cwtest_completely_control_time();

  ts->add_timer(timers[2]);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ASSERT_EQ(0, next_timers.size());
  cwtest_advance_time_ms((timers[2]->interval) + 10);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size());
  timers[2] = *next_timers.begin();
  EXPECT_EQ(3, timers[2]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, MultiNearGetNextTimersTest)
{
  cwtest_completely_control_time();

  // Shorten timer two to be under 1 second.
  timers[1]->interval = 400;

  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(1000);

  ts->get_next_timers(next_timers);

  ASSERT_EQ(2, next_timers.size()) << "Bucket should have 2 timers";

  next_timers.clear();

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, ClashingMultiMidGetNextTimersTest)
{
  cwtest_completely_control_time();

  // Lengthen timer one to be in the same second bucket as timer two but different ms
  // buckets.
  timers[0]->interval = 10000 + 100;

  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1, timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(timers[1]->interval - timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[1] = *next_timers.begin();
  EXPECT_EQ(2, timers[1]->id);

  next_timers.clear();

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, SeparateMultiMidGetNextTimersTest)
{
  cwtest_completely_control_time();

  // Lengthen timer one to be in a different second bucket than timer two.
  timers[0]->interval = 9000 + 100;

  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1, timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(timers[1]->interval - timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[1] = *next_timers.begin();
  EXPECT_EQ(2, timers[1]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, MultiLongGetTimersTest)
{
  cwtest_completely_control_time();

  // Lengthen timer one and two to be in the extra heap.
  timers[0]->interval = (3600 * 1000) + 100;
  timers[1]->interval = (3600 * 1000) + 200;

  ts->add_timer(timers[0]);
  ts->add_timer(timers[1]);
  ts->add_timer(timers[2]);

  std::unordered_set<Timer*> next_timers;

  cwtest_advance_time_ms(timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1, timers[0]->id);

  next_timers.clear();

  cwtest_advance_time_ms(timers[1]->interval - timers[0]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[1] = *next_timers.begin();
  EXPECT_EQ(2, timers[1]->id);

  next_timers.clear();

  cwtest_advance_time_ms(timers[2]->interval - timers[1]->interval);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[2] = *next_timers.begin();
  EXPECT_EQ(3, timers[2]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, ReallyLongTimer)
{
  cwtest_completely_control_time();

  // Lengthen timer three to really long (10 hours)
  timers[2]->interval = (3600 * 1000) * 10;
  ts->add_timer(timers[2]);

  std::unordered_set<Timer*> next_timers;

  ts->get_next_timers(next_timers);
  ASSERT_EQ(0, next_timers.size());

  cwtest_advance_time_ms((3600 * 1000) * 10);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[2] = *next_timers.begin();
  EXPECT_EQ(3, timers[2]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, DeleteNearTimer)
{
  ts->add_timer(timers[0]);
  ts->delete_timer(1);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DeleteMidTimer)
{
  ts->add_timer(timers[1]);
  ts->delete_timer(2);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());
  delete timers[0];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DeleteLongTimer)
{
  ts->add_timer(timers[2]);
  ts->delete_timer(3);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());
  delete timers[0];
  delete timers[1];
  delete tombstone;
}

TEST_F(TestTimerStore, UpdateTimer)
{
  cwtest_completely_control_time();

  ts->add_timer(timers[0]);

  // Replace timer one, using a newer timer with the same ID.
  timers[1]->id = 1;
  timers[1]->start_time++;
  ts->add_timer(timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer one was deleted when it was overwritten
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, DontUpdateTimerAge)
{
  cwtest_completely_control_time();

  ts->add_timer(timers[0]);

  // Attempt to replace timer one but the replacement is older
  timers[1]->id = 1;
  timers[1]->start_time--;
  ts->add_timer(timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer two was deleted when it failed to overwrite timer one
  delete timers[0];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, DontUpdateTimerSeqNo)
{
  cwtest_completely_control_time();

  timers[0]->sequence_number++;
  ts->add_timer(timers[0]);

  // Attempt to replace timer one but the replacement has a lower SeqNo
  timers[1]->id = 1;
  ts->add_timer(timers[1]);
  cwtest_advance_time_ms(1000000);

  // Fetch the newly updated timer.
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1, next_timers.size());

  // Now the timer store is empty.
  next_timers.clear();
  ts->get_next_timers(next_timers);
  EXPECT_TRUE(next_timers.empty());

  // Timer two was deleted when it failed to overwrite timer one
  delete timers[0];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, AddTombstone)
{
  cwtest_completely_control_time();

  ts->add_timer(tombstone);

  std::unordered_set<Timer*> next_timers;
  cwtest_advance_time_ms(100000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1, next_timers.size());

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

TEST_F(TestTimerStore, OverwriteWithTombstone)
{
  cwtest_completely_control_time();

  ts->add_timer(timers[0]);
  ts->add_timer(tombstone);

  std::unordered_set<Timer*> next_timers;
  cwtest_advance_time_ms(100000);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size());

  Timer* extracted = *next_timers.begin();
  EXPECT_TRUE(extracted->is_tombstone());
  EXPECT_EQ(100, extracted->interval);
  EXPECT_EQ(100, extracted->repeat_for);

  delete timers[1];
  delete timers[2];
  delete tombstone;

  cwtest_reset_time();
}

