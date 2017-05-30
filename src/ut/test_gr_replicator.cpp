/**
 * @file test_gr_replicator.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "globals.h"
#include "gr_replicator.h"
#include "base.h"
#include "fakecurl.hpp"
#include "fakehttpresolver.hpp"
#include "timer_helper.h"

/// Fixture for GRReplicatorTest.
class TestGRReplicator : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();

    _resolver = new FakeHttpResolver("10.42.42.42");
    _gr = new GRReplicator(_resolver, NULL);

    fakecurl_responses.clear();
    fakecurl_requests.clear();
  }

  void TearDown()
  {
    delete _gr;
    delete _resolver;

    Base::TearDown();
  }

  FakeHttpResolver* _resolver;
  GRReplicator* _gr;
};

// Test that a timer is replicated successfully
TEST_F(TestGRReplicator, ReplicateTimer)
{
  // Timer should have an ID of 1, and a replication factor of 1. If it doesn't
  // the send_put will fail in the Chronos GR connection
  fakecurl_responses["http://10.42.42.42:80/timers/0000000000000001-1"] = CURLE_OK;
  Timer* timer1 = default_timer(1);
  EXPECT_FALSE(timer1->replicas.empty());
  _gr->replicate(timer1);

  // The timer's been sent when fakecurl records the request. Sleep until then.
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://remote_site_1_dns_record:80/timers/0000000000000001-1");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://remote_site_1_dns_record:80/timers/0000000000000001-1");
  }

  if (count >= 10)
  {
    printf("No request was sent that matched the expected timer");
  }

  // Look at the body sent on the request. Check that it doesn't have any
  // replica information, and that it makes a valid timer
  Request& request = fakecurl_requests["http://remote_site_1_dns_record:80/timers/0000000000000001-1"];
  rapidjson::Document doc;
  doc.Parse<0>(request._body.c_str());
  EXPECT_FALSE(doc.HasParseError());
  ASSERT_TRUE(doc.HasMember("reliability"));
  EXPECT_FALSE(doc["reliability"].HasMember("replicas"));

  std::string error;
  bool replicated;
  bool gr_replicated;

  Timer* timer2 = Timer::from_json(1, 1, 0, request._body, error, replicated, gr_replicated);
  EXPECT_TRUE(timer2);

  delete timer1; timer1 = NULL;
  delete timer2; timer2 = NULL;
}
