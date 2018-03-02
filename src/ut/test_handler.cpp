/**
 * @file test_handler.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "handlers.h"
#include "mock_timer_handler.h"
#include "mock_replicator.h"
#include "mock_gr_replicator.h"
#include "mockhttpstack.hpp"
#include "base.h"
#include "test_interposer.hpp"
#include "timer_handler.h"
#include "globals.h"
#include <gtest/gtest.h>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/
using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::MatchesRegex;
using ::testing::ContainerEq;
using ::testing::UnorderedElementsAreArray;

class WithGR
{
  static MockGRReplicator* return_gr_replicator()
  {
    // Return a mock GR replicator.
    MockGRReplicator* gr_replicator = new MockGRReplicator();
    return gr_replicator;
  }
};

class WithoutGR
{
  static MockGRReplicator* return_gr_replicator()
  {
    // Return NULL. This is how GR is disabled.
    return NULL;
  }
};

template <class T>
class TestHandler : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();

    cwtest_completely_control_time();

    _replicator = new MockReplicator();
    _gr_replicator = T::return_gr_replicator();
    _th = new MockTimerHandler();
    _httpstack = new MockHttpStack();
  }

  void TearDown()
  {
    delete _req;
    delete _cfg;

    delete _httpstack;
    delete _th;
    delete _gr_replicator;
    delete _replicator;

    cwtest_reset_time();

    Base::TearDown();
  }

  void controller_request(std::string http_path,
                          htp_method method,
                          std::string body,
                          std::string parameters)
  {
    _req = new MockHttpStack::Request(_httpstack,
                                      http_path,
                                      "",
                                      parameters,
                                      body,
                                      method);

    _cfg = new ControllerTask::Config(_replicator, _gr_replicator, _th);
    _task = new ControllerTask(*_req, _cfg, 0);
  }

  MockReplicator* _replicator;
  MockGRReplicator* _gr_replicator;
  MockTimerHandler* _th;
  MockHttpStack* _httpstack;

  MockHttpStack::Request* _req;
  ControllerTask::Config* _cfg;
  ControllerTask* _task;
};

typedef ::testing::Types<WithGR, WithoutGR> GRTypes;
TYPED_TEST_CASE(TestHandler, GRTypes);

// Tests a valid request to delete an existing timer
TYPED_TEST(TestHandler, ValidJSONDeleteTimerWithoutReplicas)
{
  Timer* added_timer;

  TestFixture::controller_request("/timers/1234123412341234-2", htp_method_DELETE, "", "");

  // It's a valid timer so we expect it to be replicated in/cross-site, added
  // to this node, and have a 200 response
  EXPECT_CALL(*TestFixture::_replicator, replicate(_));
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_));
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _));
  TestFixture::_task->run();

  delete added_timer; added_timer = NULL;
}

// Tests that requests containing a URL of the form
// /timers/<timer_id>-<replication_factor><random> are accepted. This tests that
// the URL parsing is flexible, so the URL can be extended in the future if
// necessary. Currently this random content is ignored.
TYPED_TEST(TestHandler, ValidJSONDeleteTimerWithExtendedTimerID)
{
  Timer* added_timer;

  TestFixture::controller_request("/timers/1234123412341234-2RANDOMSTUFF!<>!!@123", htp_method_DELETE, "", "");

  // It's a valid timer so we expect it to be replicated in/cross-site, added
  // to this node, and have a 200 response
  EXPECT_CALL(*TestFixture::_replicator, replicate(_));
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_));
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _));
  TestFixture::_task->run();

  // Check that the timer ID and replication factor have been parsed correctly.
  EXPECT_EQ(added_timer->_replication_factor, 2);
  // The ID is the passed in ID converted to base 10.
  EXPECT_EQ(added_timer->id, 1311693406324658740);

  delete added_timer; added_timer = NULL;
}

// Tests a valid request to create a new timer that is on the local node
TYPED_TEST(TestHandler, ValidJSONCreateTimerOnNode)
{
  // Only have a single node in the cluster so we can guarantee the local node
  // is chosen as a replica
  std::vector<std::string> new_cluster_addresses;
  new_cluster_addresses.push_back("10.0.0.1:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(new_cluster_addresses);
  __globals->unlock();

  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  TestFixture::controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}", "");

  EXPECT_CALL(*TestFixture::_replicator, replicate(_));
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_));
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  TestFixture::_task->run();

  // Check that the timer is plausible.
  EXPECT_EQ(added_timer->callback_url, "localhost");
  EXPECT_EQ(added_timer->callback_body, "stuff");
  EXPECT_EQ(added_timer->repeat_for, (unsigned)200000);
  EXPECT_EQ(added_timer->interval_ms, (unsigned)100000);
  EXPECT_EQ(added_timer->sequence_number, (unsigned)0);
  std::string exp_rsp = "/timers/.*";
  EXPECT_THAT(std::string(evhtp_header_find(req.req()->headers_out, "Location")), MatchesRegex(exp_rsp));

  delete added_timer; added_timer = NULL;
}

// Tests a valid request to create a new timer that won't be on the local node
TYPED_TEST(TestHandler, ValidJSONCreateTimerNotOnNode)
{
  // Change the cluser replicas to not include the local node so we
  // can guarantee that the node isn't chosen as a replica
  std::vector<std::string> new_cluster_addresses;
  new_cluster_addresses.push_back("10.0.0.2:9999");
  __globals->lock();
  __globals->set_cluster_staying_addresses(new_cluster_addresses);
  __globals->unlock();

  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  TestFixture::controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}", "");
  EXPECT_CALL(*TestFixture::_replicator, replicate(_));
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_));
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  TestFixture::_task->run();

  // Check that the timer is a tombstone
  EXPECT_TRUE(added_timer->is_tombstone());
  delete added_timer; added_timer = NULL;
}

// Tests that get requests for timer references with a
// lead to the store being queried, using the range header if set.
TYPED_TEST(TestHandler, ValidTimerGetCurrentNodeNoRangeHeader)
{
  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*TestFixture::_th, get_timers_for_node("10.0.0.1:9999", 0, "cluster-view-id", _, _)).WillOnce(Return(200));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _));
  TestFixture::_task->run();
}

// Tests that get requests for timer references with a
// lead to the store being queried, using the range header if set.
TYPED_TEST(TestHandler, ValidTimerGetCurrentNodeRangeHeader)
{
  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=10000");
  TestFixture::_req->add_header_to_incoming_req("Range", "100");
  EXPECT_CALL(*TestFixture::_th, get_timers_for_node("10.0.0.1:9999", 100, _, _, _)).WillOnce(Return(206));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 206, _));
  TestFixture::_task->run();
}

// Tests that get requests for timer resync with a time-from parameter
// lead to the store being queried with the correct time-from value
TYPED_TEST(TestHandler, ValidTimerValidTimeFromParameter)
{
  // Get the current time (time is controlled in this test so we know it won't
  // move on unless we tell it).
  uint32_t current_time = Utils::get_time();

  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=12345", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=12345");
  EXPECT_CALL(*TestFixture::_th, get_timers_for_node("10.0.0.1:9999", _, _, current_time + 12345, _)).WillOnce(Return(200));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _));
  TestFixture::_task->run();
}

// Tests that get requests for timer references with no time-from parameter
// lead to the store being queried with time-from value of the current time
TYPED_TEST(TestHandler, ValidTimerGetNoTimeFromParameter)
{
  // Get the current time (time is controlled in this test so we know it won't
  // move on unless we tell it).
  uint32_t current_time = Utils::get_time();

  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id");
  EXPECT_CALL(*TestFixture::_th, get_timers_for_node("10.0.0.1:9999", _, _, current_time, _)).WillOnce(Return(200));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _));
  TestFixture::_task->run();
}

// Tests that get requests for timer references with an invalid time-from
// parameter leads to the store being queried with the time-from value being
// the current value
TYPED_TEST(TestHandler, ValidTimerGetInvalidTimeFromParameter)
{
  // Get the current time (time is controlled in this test so we know it won't
  // move on unless we tell it).
  uint32_t current_time = Utils::get_time();

  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=notanumber", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=notanumber");
  EXPECT_CALL(*TestFixture::_th, get_timers_for_node("10.0.0.1:9999", _, _, current_time, _)).WillOnce(Return(206));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 206, _));
  TestFixture::_task->run();
}

// Tests that get requests for timer references for a leaving node
// are correctly processed
TYPED_TEST(TestHandler, ValidTimerGetLeavingNode)
{
  // Set leaving addresses in globals so that we look there as well.
  std::vector<std::string> leaving_cluster_addresses;
  leaving_cluster_addresses.push_back("10.0.0.4:9999");

  __globals->lock();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  __globals->unlock();

  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.4:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.4:9999;cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*TestFixture::_th, get_timers_for_node("10.0.0.4:9999", _, _, _, _)).WillOnce(Return(200));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _));
  TestFixture::_task->run();
}

// Tests that get requests for timer references for a joining node
// are correctly processed
TYPED_TEST(TestHandler, ValidTimerGetJoiningNode)
{
  // Set joining addresses in globals so that we look there as well.
  std::vector<std::string> joining_cluster_addresses;
  joining_cluster_addresses.push_back("10.0.0.4:9999");

  __globals->lock();
  __globals->set_cluster_joining_addresses(joining_cluster_addresses);
  __globals->unlock();

  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.4:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.4:9999;cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*TestFixture::_th, get_timers_for_node("10.0.0.4:9999", _, _, _, _)).WillOnce(Return(200));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _));
  TestFixture::_task->run();
}

// Invalid request: Tests the case where we attempt to create a new timer,
// but we can't create the timer from the request
TYPED_TEST(TestHandler, InvalidNoTimerNoBody)
{
  TestFixture::controller_request("/timers/", htp_method_POST, "", "");
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 400, _));
  TestFixture::_task->run();
}

// Invalid request: Tests that requests to create a timer but the method is
// wrong get rejected.
TYPED_TEST(TestHandler, InvalidMethodNoTimer)
{
  TestFixture::controller_request("/timers/", htp_method_PUT, "", "");
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 405, _));
  TestFixture::_task->run();
}

// Invalid request: Tests that requests to create a timer but the method is
// wrong get rejected.
TYPED_TEST(TestHandler, InvalidMethodWithTimerWithoutReplicas)
{
  TestFixture::controller_request("/timers/1234123412341234-1", htp_method_POST, "", "");
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 405, _));
  TestFixture::_task->run();
}

// Invalid request: Tests that requests to modify a timer but timer ID is invalid
// get rejected.
TYPED_TEST(TestHandler, InvalidTimer)
{
  TestFixture::controller_request("/timers/1234", htp_method_PUT, "", "");
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 404, _));
  TestFixture::_task->run();
}

// Invalid request: Tests that get requests for timer references with a
// missing node-for-replicas parameter gets rejected
TYPED_TEST(TestHandler, InvalidTimerGetMissingRequestNode)
{
  TestFixture::controller_request("/timers?cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 400, _));
  TestFixture::_task->run();
}

// Invalid request: Tests that get requests for timer references with a
// missing cluster-view-id parameter gets rejected
TYPED_TEST(TestHandler, InvalidTimerGetMissingClusterID)
{
  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.1:9999;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;time-from=10000");
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 400, _));
  TestFixture::_task->run();
}

// Invalid request: Tests that get requests for timer references with a
// invalid node-for-replicas parameter gets rejected
TYPED_TEST(TestHandler, InvalidTimerGetInvalidRequestNode)
{
  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.5:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.5:9999;cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 400, _));
  TestFixture::_task->run();
}

// Invalid request: Tests that get requests for timer references with a
// invalid cluster-view-id parameter gets rejected
TYPED_TEST(TestHandler, InvalidTimerGetInvalidClusterID)
{
  TestFixture::controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=old-cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=old-cluster-view-id;time-from=10000");
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 400, _));
  TestFixture::_task->run();
}

// Test that a request that doesn't have any site information is GR replicated,
// and the sites are populated as expected
TYPED_TEST(TestHandler, TimerNoSites)
{
  // Extend how many remote sites there are for this test
  std::vector<std::string> old_remote_site_names;
  __globals->get_remote_site_names(old_remote_site_names);

  std::vector<std::string> remote_site_names;

  remote_site_names.push_back("remote_site_1_name");
  remote_site_names.push_back("remote_site_2_name");
  remote_site_names.push_back("remote_site_3_name");

  __globals->lock();
  __globals->set_remote_site_names(remote_site_names);
  __globals->unlock();

  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  TestFixture::controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}", "");
  EXPECT_CALL(*TestFixture::_replicator, replicate(_));
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_));
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  TestFixture::_task->run();

  // Check that the timer is plausible. The local site should be first in the
  // list, and all the other sites should be present
  EXPECT_EQ(added_timer->sites.size(), 4);
  EXPECT_EQ(added_timer->sites[0], "local_site_name");
  std::vector<std::string> expected_remote_site_names;
  expected_remote_site_names.push_back("local_site_name");
  expected_remote_site_names.push_back("remote_site_1_name");
  expected_remote_site_names.push_back("remote_site_2_name");
  expected_remote_site_names.push_back("remote_site_3_name");
  EXPECT_THAT(expected_remote_site_names, UnorderedElementsAreArray(added_timer->sites));
  delete added_timer; added_timer = NULL;
}

// Tests that a timer with site and replica information isn't replicated further
TYPED_TEST(TestHandler, TimerWithSitesAndReplicas)
{
  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  TestFixture::controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.3:9999\"], \"sites\":[\"remote_site_1_name\", \"remote_site_2_name\"] }}", "");
  EXPECT_CALL(*TestFixture::_replicator, replicate(_)).Times(0);
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_)).Times(0);
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  TestFixture::_task->run();

  delete added_timer; added_timer = NULL;
}

// Tests that a timer with site information but no replica information is
// only replicated within the site
TYPED_TEST(TestHandler, TimerWithSitesNoReplicas)
{
  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  TestFixture::controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {\"sites\":[\"remote_site_1_name\", \"remote_site_2_name\"] }}", "");
  EXPECT_CALL(*TestFixture::_replicator, replicate(_)).Times(1);
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_)).Times(0);
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  TestFixture::_task->run();

  delete added_timer; added_timer = NULL;
}

// Tests that a timer with replica information but no site information isn't
// replicated. This situation should only occur in the reregistration period
// after upgrade to Chronos with GR support
TYPED_TEST(TestHandler, TimerWithReplicasNoSites)
{
  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  TestFixture::controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {\"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.3:9999\"]}}", "");
  EXPECT_CALL(*TestFixture::_replicator, replicate(_)).Times(0);
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_)).Times(0);
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  TestFixture::_task->run();

  delete added_timer; added_timer = NULL;
}

// Tests that the replication factor is maintained even when there aren't
// enough replicas - for a new timer
TYPED_TEST(TestHandler, ReplicationFactorGreaterThanReplicasNew)
{
  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  TestFixture::controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replication-factor\": 5 }}", "");
  EXPECT_CALL(*TestFixture::_replicator, replicate(_));
  if (TestFixture::_gr_replicator != NULL)
  {
    EXPECT_CALL(*TestFixture::_gr_replicator, replicate(_));
  }
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  TestFixture::_task->run();

  // Check the Timer ID has 5 for the replication factor
  std::string exp_rsp = "/timers/.*-5";
  EXPECT_THAT(std::string(evhtp_header_find(req.req()->headers_out, "Location")), MatchesRegex(exp_rsp));

  // Check that the timer has 5 for the replication factor
  EXPECT_EQ(added_timer->_replication_factor, 5);
  EXPECT_NE(added_timer->replicas.size(), 5);
  delete added_timer; added_timer = NULL;
}

// Tests that the replication factor is maintained even when there aren't
// enough replicas - for an already replicated timer
TYPED_TEST(TestHandler, ReplicationFactorGreaterThanReplicasReplicated)
{
  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  TestFixture::controller_request("/timers/1231231231231231-5", htp_method_PUT, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [\"10.0.0.1:9999\"] }}", "");
  EXPECT_CALL(*TestFixture::_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*TestFixture::_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  TestFixture::_task->run();

  // Check the Timer ID still has 5 for the replication factor
  EXPECT_EQ(std::string(evhtp_header_find(req.req()->headers_out, "Location")), "/timers/1231231231231231-5");

  // Check that the timer has 5 for the replication factor
  EXPECT_EQ(added_timer->_replication_factor, 5);
  EXPECT_EQ(added_timer->replicas.size(), 1);
  delete added_timer; added_timer = NULL;
}
