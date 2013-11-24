#include "timer.h"
#include "globals.h"

#include <gtest/gtest.h>
#include <map>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TimerTest : public ::testing::Test
{
protected:
  virtual void SetUp()
  {
    std::vector<std::string> replicas;
    replicas.push_back("10.0.0.1");
    replicas.push_back("10.0.0.2");
    t1 = new Timer(1,
                   1000000,
                   100,
                   200,
                   0,
                   replicas,
                   "http://localhost:80/callback",
                   "stuff stuff stuff");
    __globals.lock();
    std::string localhost = "10.0.0.1";
    __globals.set_local_ip(localhost);
    std::vector<std::string> cluster_addresses;
    cluster_addresses.push_back("10.0.0.1");
    cluster_addresses.push_back("10.0.0.2");
    cluster_addresses.push_back("10.0.0.3");
    __globals.set_cluster_addresses(cluster_addresses);
    std::map<std::string, uint64_t> cluster_hashes;
    cluster_hashes["10.0.0.1"] = 0x00010000010001;
    cluster_hashes["10.0.0.2"] = 0x10001000001000;
    cluster_hashes["10.0.0.3"] = 0x01000100000100;
    __globals.set_cluster_hashes(cluster_hashes);
    __globals.unlock();
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
  failing_test_data.push_back(
      "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [] }}");

  // Reliability can be specified as empty by the client to use default replication.
  std::string default_repl_factor = "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {}}";

  // Or you can pass a custom replication factor.
  std::string custom_repl_factor = "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replication-factor\": 3 }}";

  // Or you can pas specific replicas to use.
  std::string specific_replicas = "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1\", \"10.0.0.2\" ] }}";

  // Each of the failing json blocks should not parse to a timer.
  for (auto it = failing_test_data.begin(); it != failing_test_data.end(); it++)
  {
    std::string err;
    bool replicated;
    EXPECT_EQ((void*)NULL, Timer::from_json(1, 0, *it, err, replicated)) << *it;
    EXPECT_NE("", err);
  }

  std::string err;
  bool replicated;
  Timer* timer;

  // If you don't specify a repliability, use 2.
  timer = Timer::from_json(1, 0, default_repl_factor, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_FALSE(replicated);
  EXPECT_EQ(2, timer->replication_factor);
  EXPECT_EQ(2, timer->replicas.size());
  delete timer;
  
  // If you do specify a repliability, use that.
  timer = Timer::from_json(1, 0, custom_repl_factor, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_FALSE(replicated);
  EXPECT_EQ(3, timer->replication_factor);
  EXPECT_EQ(3, timer->replicas.size());
  delete timer;

  // Regardless of replication factor, try to guess the replicas from the bloom filter if given
  timer = Timer::from_json(1, 0x11011100011101, default_repl_factor, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_FALSE(replicated);
  EXPECT_EQ(3, timer->replication_factor);
  delete timer;

  timer = Timer::from_json(1, 0x11011100011101, custom_repl_factor, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_FALSE(replicated);
  EXPECT_EQ(3, timer->replication_factor);
  delete timer;

  // If specifc replicas are specified, use them (regardless of presence of bloom hash).
  timer = Timer::from_json(1, 0x11011100011101, specific_replicas, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_TRUE(replicated);
  EXPECT_EQ(2, timer->replication_factor);
  delete timer;
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
  EXPECT_EQ("http://hostname/timers/1-10.0.0.1-10.0.0.2", t1->url("hostname"));
}

TEST_F(TimerTest, ToJSON)
{
  // Test this by rendering as JSON, then parsing back to a timer
  // and comparing.
  std::string json = t1->to_json();
  std::string err;
  bool replicated;

  Timer* t2 = Timer::from_json(2, 0, json, err, replicated);
  EXPECT_EQ(err, "");
  EXPECT_TRUE(replicated);
  ASSERT_NE((void*)NULL, t2);

  EXPECT_EQ(2, t2->id) << json;
  EXPECT_EQ(1000000, t2->start_time) << json;
  EXPECT_EQ(100, t2->interval) << json;
  EXPECT_EQ(200, t2->repeat_for) << json;
  EXPECT_EQ(2, t2->replication_factor) << json;
  EXPECT_EQ(t1->replicas, t2->replicas) << json;
  EXPECT_EQ("http://localhost:80/callback", t2->callback_url) << json;
  EXPECT_EQ("stuff stuff stuff", t2->callback_body) << json;
  delete t2;
}

TEST_F(TimerTest, IsLocal)
{
  EXPECT_TRUE(t1->is_local("10.0.0.1"));
  EXPECT_FALSE(t1->is_local("20.0.0.1"));
}

TEST_F(TimerTest, IsTombstone)
{
  EXPECT_FALSE(t1->is_tombstone());
  Timer* t2 = Timer::create_tombstone(100, 0);
  EXPECT_TRUE(t2->is_tombstone());
  delete t2;
}

TEST_F(TimerTest, BecomeTombstone)
{
  t1->become_tombstone();
  EXPECT_TRUE(t1->is_tombstone());
  EXPECT_EQ(1000000, t1->start_time);
  EXPECT_EQ(100, t1->interval);
  EXPECT_EQ(100, t1->repeat_for);
}
