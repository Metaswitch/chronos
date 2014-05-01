#include "timer_store.h"
#include "timer_helper.h"
#include "test_interposer.hpp"
#include "base.h"

#include <gtest/gtest.h>

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
    ts = new TimerStore();

    // Default some timers to short, mid and long.
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    for (int ii = 0; ii < 3; ii++)
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
    cwtest_reset_time();
    Base::TearDown();
  }

  // Variables under test.
  TimerStore* ts;
  Timer* timers[3];
  Timer* tombstone;

};

/*****************************************************************************/
/* Instance Functions                                                        */
/*****************************************************************************/

TEST_F(TestTimerStore, NearGetNextTimersTest)
{
  ts->add_timer(timers[0]);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ASSERT_EQ(0, next_timers.size());
  cwtest_advance_time_ms(1000 + TIMER_GRANULARITY_MS);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size());
  timers[0] = *next_timers.begin();
  EXPECT_EQ(1, timers[0]->id);

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
  ASSERT_EQ(0, next_timers.size()) << "Bucket should have 0 timers";

  next_timers.clear();

  cwtest_advance_time_ms(100 + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timers";

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
}

TEST_F(TestTimerStore, LongGetNextTimersTest)
{
  ts->add_timer(timers[2]);
  std::unordered_set<Timer*> next_timers;
  ts->get_next_timers(next_timers);

  ASSERT_EQ(0, next_timers.size());
  cwtest_advance_time_ms(timers[2]->interval + TIMER_GRANULARITY_MS);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size());
  timers[2] = *next_timers.begin();
  EXPECT_EQ(3, timers[2]->id);

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

  ASSERT_EQ(2, next_timers.size()) << "Bucket should have 2 timers";

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
}

TEST_F(TestTimerStore, ReallyLongTimer)
{
  // Lengthen timer three to really long (10 hours)
  timers[2]->interval = (3600 * 1000) * 10;
  ts->add_timer(timers[2]);

  std::unordered_set<Timer*> next_timers;

  ts->get_next_timers(next_timers);
  ASSERT_EQ(0, next_timers.size());

  cwtest_advance_time_ms(((3600 * 1000) * 10) + TIMER_GRANULARITY_MS);

  ts->get_next_timers(next_timers);
  ASSERT_EQ(1, next_timers.size()) << "Bucket should have 1 timer";
  timers[2] = *next_timers.begin();
  EXPECT_EQ(3, timers[2]->id);

  delete timers[0];
  delete timers[1];
  delete timers[2];
  delete tombstone;
}

TEST_F(TestTimerStore, DeleteNearTimer)
{
  ts->add_timer(timers[0]);
  ts->delete_timer(1);
  std::unordered_set<Timer*> next_timers;
  cwtest_advance_time_ms(timers[0]->interval + TIMER_GRANULARITY_MS);
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
  cwtest_advance_time_ms(timers[1]->interval + TIMER_GRANULARITY_MS);
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
  cwtest_advance_time_ms(timers[2]->interval + TIMER_GRANULARITY_MS);
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
  EXPECT_EQ(1, next_timers.size());

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
  EXPECT_EQ(1, next_timers.size());

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
  EXPECT_EQ(1, next_timers.size());

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
  EXPECT_EQ(1, next_timers.size());

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
  ASSERT_EQ(1, next_timers.size());

  Timer* extracted = *next_timers.begin();
  EXPECT_TRUE(extracted->is_tombstone());
  EXPECT_EQ(100, extracted->interval);
  EXPECT_EQ(100, extracted->repeat_for);

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
  EXPECT_EQ(0, next_timers.size());

  // Now, to prove the timer store can still find the timer, update it to
  // a tombstone.
  ts->add_timer(tombstone);

  // No timers are ready to pop yet
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0, next_timers.size());

  // Move on till the tombstone should pop (50 ms offset from timer[0])
  cwtest_advance_time_ms(150 + TIMER_GRANULARITY_MS);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1, next_timers.size());
  next_timers.clear();

  // Move on again to ensure there are no more timers in the store.
  cwtest_advance_time_ms(100000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0, next_timers.size());

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
  EXPECT_EQ(0, next_timers.size());

  // Now, to prove the timer store can still find the timer, update it to
  // a tombstone.
  tombstone->id = timers[1]->id;
  ts->add_timer(tombstone);

  // No timers are ready to pop yet
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0, next_timers.size());

  // Move on till the timer should pop
  cwtest_advance_time_ms(100000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(1, next_timers.size());
  next_timers.clear();

  // Move on again to ensure there are no more timers in the store.
  cwtest_advance_time_ms(100000);
  ts->get_next_timers(next_timers);
  EXPECT_EQ(0, next_timers.size());

  // timer[1] was deleted when it was updated in the timer store.
  delete timers[0];
  delete timers[2];
  delete tombstone;
}
