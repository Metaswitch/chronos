/**
 * @file test_timer.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version, along with the "Special Exception" for use of the program
 * along with SSL, set forth below. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. You should have received a copy
 * of the GNU General Public License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by post
 * at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception Metaswitch Networks Ltd  grants you permission to copy,
 * modify, propagate, and distribute a work formed by combining OpenSSL with
 * The Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise violate
 * the terms of the GPL. You must comply with the GPL in all respects for all
 * of the code used other than OpenSSL.  "OpenSSL" means OpenSSL toolkit
 * software distributed by the OpenSSL Project and licensed under the OpenSSL
 * Licenses, or a work based on such software and licensed under the OpenSSL
 * Licenses.  "OpenSSL Licenses" means the OpenSSL License and Original SSLeay
 * License under which the OpenSSL Project distributes the OpenSSL toolkit
 * software, as those licenses appear in the file LICENSE-OPENSSL.
 */

#include "timer.h"
#include "globals.h"
#include "base.h"
#include "test_interposer.hpp"

#include <gtest/gtest.h>
#include <map>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/
class WithReplicas {
  static void set_timer_id_format() {
    Globals::TimerIDFormat timer_id_format = Globals::TimerIDFormat::WITH_REPLICAS;
    __globals->set_timer_id_format(timer_id_format);
  }
};

class WithoutReplicas {
  static void set_timer_id_format() {
    Globals::TimerIDFormat timer_id_format = Globals::TimerIDFormat::WITHOUT_REPLICAS;
    __globals->set_timer_id_format(timer_id_format);
  }
};

template <class T>
class TestTimerIDFormats : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();
    T::set_timer_id_format();
  }

  virtual void TearDown()
  {
    Globals::TimerIDFormat timer_id_format = __globals->default_id_format();
    __globals->set_timer_id_format(timer_id_format);
    Base::TearDown();
  }

  // Helper function to access timer private variables
  int get_replication_factor(Timer* t)
  {
    return t->_replication_factor;
  }
};

/*****************************************************************************/
/* Class functions                                                           */
/*****************************************************************************/

typedef ::testing::Types<WithReplicas, WithoutReplicas> TimerIDFormatTypes;
TYPED_TEST_CASE(TestTimerIDFormats, TimerIDFormatTypes);

TYPED_TEST(TestTimerIDFormats, FromJSONTests)
{
  // The following tests depend on the current time, so install the shim
  cwtest_completely_control_time();

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  uint32_t mono_time = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
  clock_gettime(CLOCK_REALTIME, &ts);
  uint32_t real_time = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);

  std::vector<std::string> failing_test_data;

  failing_test_data.push_back("{}");

  failing_test_data.push_back("{\"timing\"}");

  failing_test_data.push_back("{\"timing\": []}");

  failing_test_data.push_back("{\"timing\": [], \"callback\": []}");

  failing_test_data.push_back("{\"timing\": [], \"callback\": [], \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": {}, \"callback\": [], \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": \"hello\" }, \"callback\": [], \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": \"hello\", \"repeat-for\": \"hello\" }, \"callback\": [], \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": \"hello\" }, \"callback\": [], \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": [], \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": {}, \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": []}, \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": {}}, \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": [] }}, \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": [], \"opaque\": [] }}, \"reliability\": []}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": [] }}, \"reliability\": []}");

  failing_test_data.push_back( "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": []}");

  failing_test_data.push_back( "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replication-factor\": \"hello\" }}");

  failing_test_data.push_back("{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [] }}");

  failing_test_data.push_back( "{\"timing\": { \"interval\": 0, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}");

  // Reliability can be ignored by the client to use default replication.
  std::string default_repl_factor = "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}";

  // Reliability can be specified as empty by the client to use default
  // replication.
  std::string default_repl_factor2 = "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {}}";

  // Or you can pass a custom replication factor.
  std::string custom_repl_factor = "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replication-factor\": 3 }}";

  // Or you can pass specific replicas to use.
  std::string specific_replicas = "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"cluster-view-id\": \"cluster-view-id\", \"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.2:9999\" ] }}";

  // You can skip the `repeat-for` to set up a one-shot timer.
  std::string no_repeat_for = "{\"timing\": { \"interval\": 100 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replication-factor\": 2 }}";

  // You can (should) specify start time by relative delta, not absolute
  // timestamp, the relative number should be preferred.
  std::string delta_start_time = "{\"timing\": { \"start-time\": 100, \"start-time-delta\":-200, \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {}}";

  // For backwards compatibility, we have to be accepting of nodes that don't
  // include "start-time-delta" in their JSON.
  std::ostringstream absolute_start_time_s; absolute_start_time_s << "{\"timing\": { \"start-time\":" << real_time - 300 << ", \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {}}";
  std::string absolute_start_time = absolute_start_time_s.str();

  // Each of the failing json blocks should not parse to a timer.
  for (std::vector<std::string>::iterator it = failing_test_data.begin();
       it != failing_test_data.end();
       ++it)
  {
    std::string err; bool replicated;
    EXPECT_EQ((void*)NULL, Timer::from_json(1, 0, 0, *it, err, replicated)) << *it;
    EXPECT_NE("", err);
  }

  std::string err; bool replicated; Timer* timer;

  // If you don't specify a replication-factor, use 2.
  timer = Timer::from_json(1, 0, 0, default_repl_factor, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_FALSE(replicated);
  EXPECT_EQ(2, TestFixture::get_replication_factor(timer));
  EXPECT_EQ(2u, timer->replicas.size());
  delete timer;

  timer = Timer::from_json(1, 0, 0, default_repl_factor2, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_FALSE(replicated);
  EXPECT_EQ(2, TestFixture::get_replication_factor(timer));
  EXPECT_EQ(2u, timer->replicas.size());
  delete timer;

  // If you do specify a replication-factor, use that.
  timer = Timer::from_json(1, 0, 0, custom_repl_factor, err, replicated);
  EXPECT_NE((void*)NULL, timer); EXPECT_EQ("", err); EXPECT_FALSE(replicated);
  EXPECT_EQ(3, TestFixture::get_replication_factor(timer));
  EXPECT_EQ(3u, timer->replicas.size());
  delete timer;

  // Get the replicas from the bloom filter if given
  timer = Timer::from_json(1, 2, 0x11011100011101, default_repl_factor, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_FALSE(replicated);
  EXPECT_EQ(2, TestFixture::get_replication_factor(timer));
  delete timer;

  timer = Timer::from_json(1, 3, 0x11011100011101, custom_repl_factor, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_FALSE(replicated);
  EXPECT_EQ(3, TestFixture::get_replication_factor(timer));
  delete timer;

  // If the replication factor on the URL (in this case 2) doesn't match the
  // replication factor in the JSON body, reject the JSON.
  timer = Timer::from_json(1, 2, 0x11011100011101, custom_repl_factor, err, replicated);
  EXPECT_EQ((void*)NULL, timer);
  EXPECT_NE("", err);
  err = "";
  delete timer;

  // If specific replicas are specified, use them (regardless of presence of
  // bloom hash).
  timer = Timer::from_json(1, 2, 0x11011100011101, specific_replicas, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_TRUE(replicated);
  EXPECT_EQ(2, TestFixture::get_replication_factor(timer));
  delete timer;

  // If no repeat for was specifed, use the interval
  timer = Timer::from_json(1, 2, 0x11011100011101, no_repeat_for, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);
  EXPECT_EQ(timer->interval_ms, timer->repeat_for);
  delete timer;

  // If delta-start-time was provided, use that
  timer = Timer::from_json(1, 2, 0x11011100011101, delta_start_time, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err); EXPECT_EQ(mono_time - 200, timer->start_time_mono_ms);
  delete timer;

  // If absolute start time was proved (and no delta-time), use that.
  timer = Timer::from_json(1, 2, 0x11011100011101, absolute_start_time, err, replicated);
  EXPECT_NE((void*)NULL, timer);
  EXPECT_EQ("", err);

  // Note that this compares to monotonic time (but the offest is the same as
  // the offset to realtime when we made the JSON string).
  EXPECT_EQ(mono_time - 300, timer->start_time_mono_ms);
  delete timer;

  // Restore real time
  cwtest_reset_time();
}

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/

class TestTimer : public Base
{
protected:
  virtual void SetUp()
  {
    Base::SetUp();

    std::vector<std::string> replicas;
    replicas.push_back("10.0.0.1:9999");
    replicas.push_back("10.0.0.2:9999");
    std::vector<std::string> tags;
    tags.push_back("TAG1");
    tags.push_back("TAG2");
    TimerID id = (TimerID)UINT_MAX + 10;
    uint32_t interval_ms = 100;
    uint32_t repeat_for = 200;

    t1 = new Timer(id, interval_ms, repeat_for);
    t1->start_time_mono_ms = 1000000;
    t1->sequence_number = 0;
    t1->replicas = replicas;
    t1->_replication_factor = 2;
    t1->tags = tags;
    t1->callback_url = "http://localhost:80/callback";
    t1->callback_body = "stuff stuff stuff";
    t1->cluster_view_id = "cluster-view-id";
  }

  virtual void TearDown()
  {
    delete t1;
    Base::TearDown();
  }

  // Helper function to access timer private variables
  int get_replication_factor(Timer* t)
  {
    return t->_replication_factor;
  }

  Timer* t1;
};

// Utility thread function to test thread-safeness of the unique generation
// algorithm.
void* generate_ids(void* arg)
{
  std::vector<TimerID>* output = (std::vector<TimerID>*)arg;
  for (int ii = 0; ii < 1000; ++ii)
  {
    TimerID t = Timer::generate_timer_id();
    output->push_back(t);
  }

  std::sort(output->begin(), output->end());

  return NULL;
}

TEST_F(TestTimer, GenerateTimerIDTests)
{
  const int concurrency = 50;

  pthread_t thread_ids[concurrency];
  std::vector<TimerID> ids[concurrency];
  std::vector<TimerID> all_ids;

  // Generate multiple (sorted) arrays of IDs in multiple threads.
  for (int ii = 0; ii < concurrency; ++ii)
  {
    ASSERT_EQ(0, pthread_create(&thread_ids[ii], NULL, generate_ids, &ids[ii]));
  }

  // Wait for the threads to finish.
  for (int ii = 0; ii < concurrency; ++ii)
  {
    ASSERT_EQ(0, pthread_join(thread_ids[ii], NULL));
  }

  // Merge all the (sorted) ID lists together.
  for (int ii = 0; ii < concurrency; ++ii)
  {
    int midpoint = all_ids.size();
    all_ids.insert(all_ids.end(), ids[ii].begin(), ids[ii].end());
    std::inplace_merge(all_ids.begin(), all_ids.begin() + midpoint, all_ids.end());
  }

  // Assert that no pairs are equal.
  for(int ii = 1; ii < concurrency * 1000; ++ii)
  {
    EXPECT_NE(all_ids[ii], all_ids[ii-1]);
  }
}

/*****************************************************************************/
/* Instance Functions                                                        */
/*****************************************************************************/

TEST_F(TestTimer, URLWithoutReplicas)
{
  Globals::TimerIDFormat stored_timer_id;
  Globals::TimerIDFormat new_timer_id = Globals::TimerIDFormat::WITHOUT_REPLICAS;
  __globals->get_timer_id_format(stored_timer_id);

  __globals->set_timer_id_format(new_timer_id);
  EXPECT_EQ("http://hostname:9999/timers/0000000100000009-2", t1->url("hostname:9999"));
  EXPECT_EQ("http://hostname:9999/timers/0000000100000009-2", t1->url("hostname"));
  __globals->set_timer_id_format(stored_timer_id);
}

TEST_F(TestTimer, URLWithReplicas)
{
  Globals::TimerIDFormat stored_timer_id;
  Globals::TimerIDFormat new_timer_id = Globals::TimerIDFormat::WITH_REPLICAS;
  __globals->get_timer_id_format(stored_timer_id);

  __globals->set_timer_id_format(new_timer_id);
  EXPECT_EQ("http://hostname:9999/timers/00000001000000090010011000011001", t1->url("hostname:9999"));
  EXPECT_EQ("http://hostname:9999/timers/00000001000000090010011000011001", t1->url("hostname"));
  __globals->set_timer_id_format(stored_timer_id);
}

TEST_F(TestTimer, ToJSON)
{
  // Test this by rendering as JSON, then parsing back to a timer and
  // comparing.

  // Need to completely control time here (as we encode the time as a
  // delta against "now" in the JSON and need that to come back the same
  // afterwards).
  cwtest_completely_control_time();

  // We need to use a new timer here, because the values we use in
  // testing (100ms and 200ms) are too short to be specified on the
  // JSON interface (which counts in seconds).
  uint32_t interval_ms = 1000;
  uint32_t repeat_for = 2000;

  Timer* t2 = new Timer(1, interval_ms, repeat_for);
  t2->start_time_mono_ms = 1000000;
  t2->sequence_number = 0;
  t2->replicas = t1->replicas;
  t2->tags = t1->tags;
  t2->callback_url = "http://localhost:80/callback";
  t2->callback_body = "{\"stuff\": \"stuff\"}";
  t2->cluster_view_id = "cluster-view-id";

  // Move time forward a bit, to check this this is correctly
  // compensated for by the start-time-delta.
  cwtest_advance_time_ms(1000);
  std::string json = t2->to_json();

  std::string err;
  bool replicated;
  Timer* t3 = Timer::from_json(2, 0, 0, json, err, replicated);
  EXPECT_EQ(err, "");
  EXPECT_TRUE(replicated);
  ASSERT_NE((void*)NULL, t2);

  EXPECT_EQ(2u, t3->id) << json;
  EXPECT_EQ(1000000u, t3->start_time_mono_ms) << json;
  EXPECT_EQ(t2->interval_ms, t3->interval_ms) << json;
  EXPECT_EQ(t2->repeat_for, t3->repeat_for) << json;
  EXPECT_EQ(2, get_replication_factor(t3)) << json;
  EXPECT_EQ(t2->replicas, t3->replicas) << json;
  EXPECT_EQ(t2->tags, t3->tags) << json;
  EXPECT_EQ(t2->cluster_view_id, t3->cluster_view_id) << json;
  EXPECT_EQ("http://localhost:80/callback", t3->callback_url) << json;
  EXPECT_EQ("{\"stuff\": \"stuff\"}", t3->callback_body) << json;
  delete t2;
  delete t3;

  cwtest_reset_time();
}

TEST_F(TestTimer, IsLocal)
{
  EXPECT_TRUE(t1->is_local("10.0.0.1:9999"));
  EXPECT_FALSE(t1->is_local("10.0.0.1:9998"));
  EXPECT_FALSE(t1->is_local("20.0.0.1:9999"));
}

TEST_F(TestTimer, IsTombstone)
{
  Timer* t2 = Timer::create_tombstone(100, 0);
  EXPECT_NE(0u, t2->start_time_mono_ms);
  EXPECT_TRUE(t2->is_tombstone());
  delete t2;
}

TEST_F(TestTimer, BecomeTombstone)
{
  EXPECT_FALSE(t1->is_tombstone());
  t1->become_tombstone();
  EXPECT_TRUE(t1->is_tombstone());
  EXPECT_EQ(1000000u, t1->start_time_mono_ms);
  EXPECT_EQ(100u, t1->interval_ms);
  EXPECT_EQ(100u, t1->repeat_for);
}

TEST_F(TestTimer, MatchingClusterID)
{
  EXPECT_TRUE(t1->is_matching_cluster_view_id("cluster-view-id"));
  EXPECT_FALSE(t1->is_matching_cluster_view_id("not-cluster-id"));
}

TEST_F(TestTimer, IsLastReplica)
{
  EXPECT_FALSE(t1->is_last_replica());

  std::vector<std::string> replicas;
  replicas.push_back("10.0.0.2:9999");
  replicas.push_back("10.0.0.1:9999");

  Timer* t2 = new Timer(2, 100, 200);
  t2->replicas = replicas;
  EXPECT_TRUE(t2->is_last_replica());
  delete t2;
}
