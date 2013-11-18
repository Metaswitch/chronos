#include "timer.h"

#include <gtest/gtest.h>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TimerTest : public ::testing::Test
{
protected:
  virtual void SetUp()
  {
    t1 = new Timer(1,
                   1000000,
                   100,
                   200,
                   0,
                   std::vector<std::string>(1, "10.0.0.1"),
                   "http://localhost:80/callback",
                   "stuff stuff stuff");
  }

  virtual void TearDown()
  {
    delete t1;
  }

  Timer* t1;
};

/*****************************************************************************/
/* Class functions                                                           */
/*****************************************************************************/

TEST_F(TimerTest, FromJSONTests)
{
  std::vector<std::string> failing_test_data;
  failing_test_data.push_back(
      "{}");
  failing_test_data.push_back(
      "{\"timing\"}");
  failing_test_data.push_back(
      "{\"timing\": []}");
  failing_test_data.push_back(
      "{\"timing\": [], \"callback\": []}");
  failing_test_data.push_back(
      "{\"timing\": [], \"callback\": [], \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": {}, \"callback\": [], \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": \"hello\" }, \"callback\": [], \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": \"hello\", \"repeat-for\": \"hello\" }, \"callback\": [], \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": \"hello\" }, \"callback\": [], \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": [], \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": {}, \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": []}, \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": {}}, \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": [] }}, \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": [], \"opaque\": [] }}, \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": [] }}, \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": []}");
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replication-factor\": \"hello\" }}");

  std::vector<std::string> passing_test_data;
  // Reliability can be specified as empty by the client to use default replication.
  passing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {}}");

  // Or you can pass a custom replication factor.
  passing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replication-factor\": 3 }}");

  // Each of the failing json blocks should not parse to a timer.
  for (auto it = failing_test_data.begin(); it != failing_test_data.end(); it++)
  {
    std::string err;
    EXPECT_EQ((void*)NULL, Timer::from_json(1, std::vector<std::string>(), *it, err)) << *it;
    EXPECT_NE("", err);
  }

  // Each of the valid json blocks should return a timer object.
  for (auto it = passing_test_data.begin(); it != passing_test_data.end(); it++)
  {
    std::string err;
    Timer* timer = Timer::from_json(1, std::vector<std::string>(), *it, err);
    EXPECT_NE((void*)NULL, timer) << *it;
    EXPECT_EQ("", err);
    delete timer;
  }
}

TEST_F(TimerTest, GenerateTimerIDTests)
{
  TimerID id1 = Timer::generate_timer_id();
  TimerID id2 = Timer::generate_timer_id();
  TimerID id3 = Timer::generate_timer_id();

  EXPECT_NE(id1, id2);
  EXPECT_NE(id2, id3);
  EXPECT_NE(id1, id3);
}

/*****************************************************************************/
/* Instance Functions                                                        */
/*****************************************************************************/

TEST_F(TimerTest, NextPopTime)
{
  EXPECT_EQ(1000000 + 100, t1->next_pop_time());

  struct timespec ts;
  t1->next_pop_time(ts);
  EXPECT_EQ(1000000 / 1000, ts.tv_sec);
  EXPECT_EQ(100 * 1000 * 1000, ts.tv_nsec);
}

TEST_F(TimerTest, URL)
{
  EXPECT_EQ("http://hostname/timers/1-10.0.0.1", t1->url("hostname"));
}

TEST_F(TimerTest, ToJSON)
{
  // Test this by rendering as JSON, then parsing back to a timer
  // and comparing.
  std::string json = t1->to_json();
  std::string err;

  // Note that we have to supply the correct replicas here as they're ignored
  // by the JSON parser.  Also use a new ID for sanity.
  Timer* t2 = Timer::from_json(2, std::vector<std::string>(1, "10.0.0.1"), json, err);
  EXPECT_EQ(err, "");
  ASSERT_NE((void*)NULL, t2);

  EXPECT_EQ(2, t2->id) << json;
  EXPECT_EQ(1000000, t2->start_time) << json;
  EXPECT_EQ(100, t2->interval) << json;
  EXPECT_EQ(200, t2->repeat_for) << json;
  EXPECT_EQ(1, t2->replication_factor) << json;
  EXPECT_EQ(std::vector<std::string>(1, "10.0.0.1"), t2->replicas) << json;
  EXPECT_EQ("http://localhost:80/callback", t2->callback_url) << json;
  EXPECT_EQ("stuff stuff stuff", t2->callback_body) << json;
  delete t2;
}

TEST_F(TimerTest, IsLocal)
{
  EXPECT_TRUE(t1->is_local("10.0.0.1"));
  EXPECT_FALSE(t1->is_local("10.0.0.2"));
}
