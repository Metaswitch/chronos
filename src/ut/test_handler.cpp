/**
 * @file test_handler.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2015  Metaswitch Networks Ltd
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

class TestHandler : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();

    cwtest_completely_control_time();

    _replicator = new MockReplicator();
    _gr_replicator = new MockGRReplicator();
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

// Tests a valid request to delete an existing timer
TEST_F(TestHandler, ValidJSONDeleteTimerWithoutReplicas)
{
  Timer* added_timer;

  controller_request("/timers/1234123412341234-2", htp_method_DELETE, "", "");

  // It's a valid timer so we expect it to be replicated in/cross-site, added
  // to this node, and have a 200 response
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_gr_replicator, replicate(_));
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();

  delete added_timer; added_timer = NULL;
}

// Tests a valid request to delete an existing timer
TEST_F(TestHandler, ValidJSONDeleteTimerWithReplicas)
{
  Timer* added_timer;

  controller_request("/timers/12341234123412341234123412341234", htp_method_DELETE, "", "");
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_gr_replicator, replicate(_));
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();

  delete added_timer; added_timer = NULL;
}

// Tests a valid request to create a new timer that is on the local node
TEST_F(TestHandler, ValidJSONCreateTimerOnNode)
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

  controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}", "");

  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_gr_replicator, replicate(_));
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  _task->run();

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
TEST_F(TestHandler, ValidJSONCreateTimerNotOnNode)
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

  controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}", "");
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_gr_replicator, replicate(_));
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  _task->run();

  // Check that the timer is a tombstone
  EXPECT_TRUE(added_timer->is_tombstone());
  delete added_timer; added_timer = NULL;
}

// Tests that a delete request for timer references always returns 202
TEST_F(TestHandler, ValidTimerReference)
{
  controller_request("/timers/references", htp_method_DELETE, "{\"IDs\": []}", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 202, _));
  _task->run();
}

// Tests that get requests for timer references with a
// lead to the store being queried, using the range header if set.
TEST_F(TestHandler, ValidTimerGetCurrentNodeNoRangeHeader)
{
  controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*_th, get_timers_for_node("10.0.0.1:9999", 0, "cluster-view-id", _, _)).WillOnce(Return(200));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();
}

// Tests that get requests for timer references with a
// lead to the store being queried, using the range header if set.
TEST_F(TestHandler, ValidTimerGetCurrentNodeRangeHeader)
{
  controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=10000");
  _req->add_header_to_incoming_req("Range", "100");
  EXPECT_CALL(*_th, get_timers_for_node("10.0.0.1:9999", 100, _, _, _)).WillOnce(Return(206));
  EXPECT_CALL(*_httpstack, send_reply(_, 206, _));
  _task->run();
}

// Tests that get requests for timer resync with a time-from parameter
// lead to the store being queried with the correct time-from value
TEST_F(TestHandler, ValidTimerValidTimeFromParameter)
{
  // Get the current time (time is controlled in this test so we know it won't
  // move on unless we tell it).
  uint32_t current_time = Utils::get_time();

  controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=12345", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=12345");
  EXPECT_CALL(*_th, get_timers_for_node("10.0.0.1:9999", _, _, current_time + 12345, _)).WillOnce(Return(200));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();
}

// Tests that get requests for timer references with no time-from parameter
// lead to the store being queried with time-from value of the current time
TEST_F(TestHandler, ValidTimerGetNoTimeFromParameter)
{
  // Get the current time (time is controlled in this test so we know it won't
  // move on unless we tell it).
  uint32_t current_time = Utils::get_time();

  controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id");
  EXPECT_CALL(*_th, get_timers_for_node("10.0.0.1:9999", _, _, current_time, _)).WillOnce(Return(200));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();
}

// Tests that get requests for timer references with an invalid time-from
// parameter leads to the store being queried with the time-from value being
// the current value
TEST_F(TestHandler, ValidTimerGetInvalidTimeFromParameter)
{
  // Get the current time (time is controlled in this test so we know it won't
  // move on unless we tell it).
  uint32_t current_time = Utils::get_time();

  controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=notanumber", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=cluster-view-id;time-from=notanumber");
  EXPECT_CALL(*_th, get_timers_for_node("10.0.0.1:9999", _, _, current_time, _)).WillOnce(Return(206));
  EXPECT_CALL(*_httpstack, send_reply(_, 206, _));
  _task->run();
}

// Tests that get requests for timer references for a leaving node
// are correctly processed
TEST_F(TestHandler, ValidTimerGetLeavingNode)
{
  // Set leaving addresses in globals so that we look there as well.
  std::vector<std::string> leaving_cluster_addresses;
  leaving_cluster_addresses.push_back("10.0.0.4:9999");

  __globals->lock();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  __globals->unlock();

  controller_request("/timers?node-for-replicas=10.0.0.4:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.4:9999;cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*_th, get_timers_for_node("10.0.0.4:9999", _, _, _, _)).WillOnce(Return(200));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();
}

// Tests that get requests for timer references for a joining node
// are correctly processed
TEST_F(TestHandler, ValidTimerGetJoiningNode)
{
  // Set joining addresses in globals so that we look there as well.
  std::vector<std::string> joining_cluster_addresses;
  joining_cluster_addresses.push_back("10.0.0.4:9999");

  __globals->lock();
  __globals->set_cluster_joining_addresses(joining_cluster_addresses);
  __globals->unlock();

  controller_request("/timers?node-for-replicas=10.0.0.4:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.4:9999;cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*_th, get_timers_for_node("10.0.0.4:9999", _, _, _, _)).WillOnce(Return(200));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();
}

// Invalid request: Tests the case where we attempt to create a new timer,
// but we can't create the timer from the request
TEST_F(TestHandler, InvalidNoTimerNoBody)
{
  controller_request("/timers/", htp_method_POST, "", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that requests to create a timer but the method is
// wrong get rejected.
TEST_F(TestHandler, InvalidMethodNoTimer)
{
  controller_request("/timers/", htp_method_PUT, "", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 405, _));
  _task->run();
}

// Invalid request: Tests that requests to create a timer but the method is
// wrong get rejected.
TEST_F(TestHandler, InvalidMethodWithTimerWithoutReplicas)
{
  controller_request("/timers/1234123412341234-1", htp_method_POST, "", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 405, _));
  _task->run();
}

// Invalid request: Tests that requests to create a timer but the method is
// wrong get rejected.
TEST_F(TestHandler, InvalidMethodWithTimerWithReplicas)
{
  controller_request("/timers/12341234123412341234123412341234", htp_method_POST, "", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 405, _));
  _task->run();
}

// Invalid request: Tests that requests to modify a timer but timer ID is invalid
// get rejected.
TEST_F(TestHandler, InvalidTimer)
{
  controller_request("/timers/1234", htp_method_PUT, "", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 404, _));
  _task->run();
}

// Invalid request: Tests that requests for timer references that aren't deletes
// get rejected.
TEST_F(TestHandler, InvalidMethodTimerReferences)
{
  controller_request("/timers/references", htp_method_PUT, "", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 405, _));
  _task->run();
}

// Invalid request: Tests that get requests for timer references with a
// missing node-for-replicas parameter gets rejected
TEST_F(TestHandler, InvalidTimerGetMissingRequestNode)
{
  controller_request("/timers?cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that get requests for timer references with a
// missing cluster-view-id parameter gets rejected
TEST_F(TestHandler, InvalidTimerGetMissingClusterID)
{
  controller_request("/timers?node-for-replicas=10.0.0.1:9999;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;time-from=10000");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that get requests for timer references with a
// invalid node-for-replicas parameter gets rejected
TEST_F(TestHandler, InvalidTimerGetInvalidRequestNode)
{
  controller_request("/timers?node-for-replicas=10.0.0.5:9999;cluster-view-id=cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.5:9999;cluster-view-id=cluster-view-id;time-from=10000");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that get requests for timer references with a
// invalid cluster-view-id parameter gets rejected
TEST_F(TestHandler, InvalidTimerGetInvalidClusterID)
{
  controller_request("/timers?node-for-replicas=10.0.0.1:9999;cluster-view-id=old-cluster-view-id;time-from=10000", htp_method_GET, "", "node-for-replicas=10.0.0.1:9999;cluster-view-id=old-cluster-view-id;time-from=10000");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Test that a request that doesn't have any site information is GR replicated,
// and the sites are populated as expected
TEST_F(TestHandler, TimerNoSites)
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

  controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}", "");
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_gr_replicator, replicate(_));
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  _task->run();

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
TEST_F(TestHandler, TimerWithSitesAndReplicas)
{
  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.3:9999\"], \"sites\":[\"remote_site_1_name\", \"remote_site_2_name\"] }}", "");
  EXPECT_CALL(*_replicator, replicate(_)).Times(0);
  EXPECT_CALL(*_gr_replicator, replicate(_)).Times(0);
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  _task->run();

  delete added_timer; added_timer = NULL;
}

// Tests that a timer with site information but no replica information is
// only replicated within the site
TEST_F(TestHandler, TimerWithSitesNoReplicas)
{
  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {\"sites\":[\"remote_site_1_name\", \"remote_site_2_name\"] }}", "");
  EXPECT_CALL(*_replicator, replicate(_)).Times(1);
  EXPECT_CALL(*_gr_replicator, replicate(_)).Times(0);
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  _task->run();

  delete added_timer; added_timer = NULL;
}

// Tests that a timer with replica information but no site information isn't
// replicated. This situation should only occur in the reregistration period
// after upgrade to Chronos with GR support
TEST_F(TestHandler, TimerWithReplicasNoSites)
{
  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {\"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.3:9999\"]}}", "");
  EXPECT_CALL(*_replicator, replicate(_)).Times(0);
  EXPECT_CALL(*_gr_replicator, replicate(_)).Times(0);
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  _task->run();

  delete added_timer; added_timer = NULL;
}

// Tests that the replication factor is maintained even when there aren't
// enough replicas - for a new timer
TEST_F(TestHandler, ReplicationFactorGreaterThanReplicasNew)
{
  // This test is only valid when the TimerIDFormat is without replicas
  Globals::TimerIDFormat timer_id_format = Globals::TimerIDFormat::WITHOUT_REPLICAS;
  __globals->lock();
  __globals->set_timer_id_format(timer_id_format);
  __globals->unlock();

  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replication-factor\": 5 }}", "");
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_gr_replicator, replicate(_));
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  _task->run();

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
TEST_F(TestHandler, ReplicationFactorGreaterThanReplicasReplicated)
{
  // This test is only valid when the TimerIDFormat is without replicas
  Globals::TimerIDFormat timer_id_format = Globals::TimerIDFormat::WITHOUT_REPLICAS;
  __globals->lock();
  __globals->set_timer_id_format(timer_id_format);
  __globals->unlock();

  Timer* added_timer;
  HttpStack::Request req(NULL, NULL);

  controller_request("/timers/1231231231231231-5", htp_method_PUT, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [\"10.0.0.1:9999\"] }}", "");
  EXPECT_CALL(*_th, add_timer(_,_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _)).WillOnce(SaveArg<0>(&req));
  _task->run();

  // Check the Timer ID still has 5 for the replication factor
  EXPECT_EQ(std::string(evhtp_header_find(req.req()->headers_out, "Location")), "/timers/1231231231231231-5");

  // Check that the timer has 5 for the replication factor
  EXPECT_EQ(added_timer->_replication_factor, 5);
  EXPECT_EQ(added_timer->replicas.size(), 1);
  delete added_timer; added_timer = NULL;
}
