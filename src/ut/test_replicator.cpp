/**
 * @file test_replicator.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "globals.h"
#include "replicator.h"
#include "base.h"
#include "fakecurl.hpp"
#include "fakehttpresolver.hpp"
#include "mockcommunicationmonitor.h"
#include "timer_helper.h"

using ::testing::_;

/// Fixture for ReplicatorTest.
class TestReplicator : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();

    _resolver = new FakeHttpResolver("10.42.42.42");
    _replicator = new Replicator(_resolver, NULL);

    fakecurl_responses.clear();
    fakecurl_requests.clear();
  }

  void TearDown()
  {
    delete _replicator;
    delete _resolver;

    Base::TearDown();
  }

  FakeHttpResolver* _resolver;
  Replicator* _replicator;
};

// Test that a local-only timer replica is not replicated
TEST_F(TestReplicator, LocalReplica)
{
  Timer* timer1 = default_timer(1);
  EXPECT_TRUE(timer1->replicas.size() == 1);
  fakecurl_responses["http://10.0.0.1:9999/timers/0000000000000001-2"] = CURLE_OK;

  _replicator->replicate(timer1);

  // We are expecting no timer to be sent, but want to sleep a little, just in
  // case we actually send one, then check fakecurl. We don't want to wait the
  // full 10 seconds the other tests can, as we would always hit that here.
  sleep(3);
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://10.0.0.1:9999/timers/0000000000000001-2");

  // Assert that the replicator didn't send any requests out
  ASSERT_TRUE(it == fakecurl_requests.end());

  delete timer1; timer1 = NULL;
}

// Test that a non-local timer replica is replicated successfully
TEST_F(TestReplicator, StandardReplica)
{
  Timer* timer1 = default_timer(1);
  timer1->_replication_factor = 2;
  timer1->replicas.push_back("10.0.0.2:9999");
  EXPECT_TRUE(timer1->replicas.size() == 2);
  fakecurl_responses["http://10.0.0.2:9999/timers/0000000000000001-2"] = CURLE_OK;

  _replicator->replicate(timer1);

  // The timer's been sent when fakecurl records the request. Sleep until then.
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://10.0.0.2:9999/timers/0000000000000001-2");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://10.0.0.2:9999/timers/0000000000000001-2");
  }

  if (count >= 10)
  {
    printf("No request was sent that matched the expected timer\n");
  }
  ASSERT_TRUE(it != fakecurl_requests.end());

  // Look at the body sent on the request. Check that it doesn't have any
  // replica information, and that it makes a valid timer
  Request& request = it->second;
  rapidjson::Document doc;
  doc.Parse<0>(request._body.c_str());
  EXPECT_FALSE(doc.HasParseError());
  ASSERT_TRUE(doc.HasMember("reliability"));
  EXPECT_TRUE(doc["reliability"].HasMember("replicas"));

  std::string error;
  bool replicated;
  bool gr_replicated;

  Timer* timer2 = Timer::from_json(1, 1, 0, request._body, error, replicated, gr_replicated);
  EXPECT_TRUE(timer2);

  delete timer1; timer1 = NULL;
  delete timer2; timer2 = NULL;
}

// Test that timer extra-replicas are replicated successfully
TEST_F(TestReplicator, ExtraReplica)
{
  Timer* timer1 = default_timer(1);
  timer1->_replication_factor = 2;
  timer1->extra_replicas.push_back("10.0.0.3:9999");
  EXPECT_TRUE(timer1->replicas.size() == 1);
  EXPECT_TRUE(timer1->extra_replicas.size() == 1);
  fakecurl_responses["http://10.0.0.3:9999/timers/0000000000000001-2"] = CURLE_OK;

  _replicator->replicate(timer1);

  // The timer's been sent when fakecurl records the request. Sleep until then.
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://10.0.0.3:9999/timers/0000000000000001-2");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://10.0.0.3:9999/timers/0000000000000001-2");
  }

  if (count >= 10)
  {
    printf("No request was sent that matched the expected timer\n");
  }
  ASSERT_TRUE(it != fakecurl_requests.end());

  // Look at the body sent on the request. Check that it doesn't have any
  // replica information, and that it makes a valid timer
  Request& request = it->second;
  rapidjson::Document doc;
  doc.Parse<0>(request._body.c_str());
  EXPECT_FALSE(doc.HasParseError());
  ASSERT_TRUE(doc.HasMember("reliability"));
  EXPECT_TRUE(doc["reliability"].HasMember("replicas"));

  std::string error;
  bool replicated;
  bool gr_replicated;

  Timer* timer2 = Timer::from_json(1, 1, 0, request._body, error, replicated, gr_replicated);
  EXPECT_TRUE(timer2);

  delete timer1; timer1 = NULL;
  delete timer2; timer2 = NULL;
}

// Test that replication failure is handled correctly
TEST_F(TestReplicator, FailedReplica)
{
  Timer* timer1 = default_timer(1);
  timer1->_replication_factor = 2;
  timer1->replicas.push_back("10.0.0.2:9999");
  EXPECT_TRUE(timer1->replicas.size() == 2);
  fakecurl_responses["http://10.0.0.2:9999/timers/0000000000000001-2"] = CURLE_HTTP_RETURNED_ERROR;

  _replicator->replicate(timer1);

  // The timer's been sent when fakecurl records the request. Sleep until then.
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://10.0.0.2:9999/timers/0000000000000001-2");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://10.0.0.2:9999/timers/0000000000000001-2");
  }

  if (count >= 10)
  {
    printf("No request was sent that matched the expected timer\n");
  }
  ASSERT_TRUE(it != fakecurl_requests.end());

  // Look at the body sent on the request. Check that it doesn't have any
  // replica information, and that it makes a valid timer
  Request& request = it->second;
  rapidjson::Document doc;
  doc.Parse<0>(request._body.c_str());
  EXPECT_FALSE(doc.HasParseError());
  ASSERT_TRUE(doc.HasMember("reliability"));
  EXPECT_TRUE(doc["reliability"].HasMember("replicas"));

  std::string error;
  bool replicated;
  bool gr_replicated;

  Timer* timer2 = Timer::from_json(1, 1, 0, request._body, error, replicated, gr_replicated);
  EXPECT_TRUE(timer2);

  delete timer1; timer1 = NULL;
  delete timer2; timer2 = NULL;
}

// Test that a non-local timer replica is replicated successfully
TEST_F(TestReplicator, ReplicateToNode)
{
  Timer* timer1 = default_timer(1);
  timer1->_replication_factor = 1;
  EXPECT_TRUE(timer1->replicas.size() == 1);
  fakecurl_responses["http://ReplicateToNode:9999/timers/0000000000000001-1"] = CURLE_OK;
  // TODO get fakecurl to recognise ReplicateToNode, rather than seeing it as 10.42.42.42 
  fakecurl_responses["http://10.42.42.42:9999/timers/0000000000000001-1"] = CURLE_OK;

  _replicator->replicate_timer_to_node(timer1,"ReplicateToNode:9999");

  // The timer's been sent when fakecurl records the request. Sleep until then.
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://ReplicateToNode:9999/timers/0000000000000001-1");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://ReplicateToNode:9999/timers/0000000000000001-1");
  }

  if (count >= 10)
  {
    printf("No request was sent that matched the expected timer\n");
  }
  ASSERT_TRUE(it != fakecurl_requests.end());

  // Look at the body sent on the request. Check that it doesn't have any
  // replica information, and that it makes a valid timer
  Request& request = it->second;
  rapidjson::Document doc;
  doc.Parse<0>(request._body.c_str());
  EXPECT_FALSE(doc.HasParseError());
  ASSERT_TRUE(doc.HasMember("reliability"));
  EXPECT_TRUE(doc["reliability"].HasMember("replicas"));

  std::string error;
  bool replicated;
  bool gr_replicated;

  Timer* timer2 = Timer::from_json(1, 1, 0, request._body, error, replicated, gr_replicated);
  EXPECT_TRUE(timer2);

  delete timer1; timer1 = NULL;
  delete timer2; timer2 = NULL;
}


