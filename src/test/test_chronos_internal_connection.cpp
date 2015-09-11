/**
 * @file test_chronos_internal_connection.cpp
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

#include "gtest/gtest.h"

#include "fakehttpresolver.hpp"
#include "chronos_internal_connection.h"
#include "base.h"
#include "fakecurl.hpp"
#include "mock_replicator.h"
#include "mock_timer_handler.h"
#include "globals.h"
#include "fakesnmp.hpp"

using namespace std;
using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;

static SNMP::U32Scalar _fake_scalar("","");
static SNMP::CounterTable* _fake_counter_table;

MATCHER(IsTombstone, "is a tombstone")
{
  return arg->is_tombstone();
}

MATCHER(IsNotTombstone, "is not a tombstone")
{
  return !(arg->is_tombstone());
}

/// Fixture for ChronosInternalConnectionTest.
class TestChronosInternalConnection : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();

    _fake_counter_table = SNMP::CounterTable::create("","");
    _resolver = new FakeHttpResolver("10.42.42.42");
    _replicator = new MockReplicator();
    _th = new MockTimerHandler();
    _chronos = new ChronosInternalConnection(_resolver,
                                             _th,
                                             _replicator,
                                             NULL,
                                             &_fake_scalar,
                                             _fake_counter_table,
                                             _fake_counter_table);
    __globals->get_cluster_addresses(_cluster_addresses);
    __globals->get_cluster_local_ip(_local_ip);

    fakecurl_responses.clear();
  }

  void TearDown()
  {
    delete _chronos;
    delete _th;
    delete _replicator;
    delete _resolver;
    delete _fake_counter_table;

    Base::TearDown();
  }

  FakeHttpResolver* _resolver;
  MockReplicator* _replicator;
  MockTimerHandler* _th;
  ChronosInternalConnection* _chronos;
  std::vector<std::string> _cluster_addresses;
  std::string _local_ip;
};

TEST_F(TestChronosInternalConnection, SendDelete)
{
  fakecurl_responses["http://10.42.42.42:80/timers/references"] = CURLE_OK;
  HTTPCode status = _chronos->send_delete("10.42.42.42:80", "{}");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendGet)
{
  fakecurl_responses["http://10.42.42.42:80/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = CURLE_OK;
  std::string response;
  HTTPCode status = _chronos->send_get("10.42.42.42:80", "10.0.0.1:9999", "SCALE", "cluster-view-id", 0, response);
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerNoResults)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":[]}";
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerOneTimer)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\", \"10.0.0.3:9999\"], \"Timer\": {\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.3:9999\" ] }}}]}";
  fakecurl_responses["http://10.42.42.42:9999/timers/references"] = HTTP_ACCEPTED;
  Timer* added_timer;

  EXPECT_CALL(*_th, add_timer_to_store(_)).WillOnce(SaveArg<0>(&added_timer));

  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsNotTombstone(), "10.0.0.3:9999")); // Update
  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsTombstone(), "10.0.0.2:9999")); // Tombstone
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 200);

  delete added_timer; added_timer = NULL;
}

TEST_F(TestChronosInternalConnection, SendTriggerOneTimerWithTombstoneAndLeaving)
{
  // Set leaving addresses in globals so that we look there as well.
  std::vector<std::string> leaving_cluster_addresses;
  leaving_cluster_addresses.push_back("10.0.0.4:9999");

  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  _cluster_addresses.push_back("10.0.0.4:9999");

  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\", \"10.0.0.4:9999\"], \"Timer\": {\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.3:9999\" ] }}}]}";
  fakecurl_responses["http://10.42.42.42:9999/timers/references"] = HTTP_ACCEPTED;
  Timer* added_timer;

  EXPECT_CALL(*_th, add_timer_to_store(_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsTombstone(), "10.0.0.2:9999")); // Tombstone
  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsTombstone(), "10.0.0.4:9999")); // Tombstone
  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsNotTombstone(), "10.0.0.3:9999")); // Update
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 200);

  _cluster_addresses.pop_back();
  leaving_cluster_addresses.clear();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);

  delete added_timer; added_timer = NULL;
}

TEST_F(TestChronosInternalConnection, ResynchronizeWithTimers)
{
  std::vector<std::string> leaving_cluster_addresses;
  leaving_cluster_addresses.push_back("10.0.0.4:9999");

  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  _cluster_addresses.push_back("10.0.0.4:9999");

  // Timers from 10.0.0.2/10.0.0.3/10.0.0.4 - One timer that's having its replica list reordered.
  // This isn't a valid response (as it should be different for .2/.3/.4), but it's sufficient
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.1:9999\", \"10.0.0.2:9999\", \"10.0.0.3:9999\"], \"Timer\": {\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.3:9999\", \"10.0.0.1:9999\", \"10.0.0.2:9999\" ] }}}]}";

  // Delete response
  fakecurl_responses["http://10.42.42.42:9999/timers/references"] = HTTP_SERVER_UNAVAILABLE;

  // There should be no calls to add a timer, as the node has moved higher up
  // the replica list
  EXPECT_CALL(*_th, add_timer_to_store(_)).Times(0);
  // There are no calls to replicate to 10.0.0.3 as it is lower in the replica list
  EXPECT_CALL(*_replicator, replicate_timer_to_node(_, "10.0.0.3:9999")).Times(0);
  // There are three calls to replicate to 10.0.0.2 as it is lower/equal in the old/new replica lists
  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsNotTombstone(), "10.0.0.2:9999")).Times(3);
  _chronos->resynchronize();

  _cluster_addresses.pop_back();
  leaving_cluster_addresses.clear();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
}

TEST_F(TestChronosInternalConnection, ResynchronizeWithInvalidGetResponse)
{
  // Response has invalid JSON
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":}";

  // There should be no calls to add/replicate a timer
  EXPECT_CALL(*_th, add_timer_to_store(_)).Times(0);
  EXPECT_CALL(*_replicator, replicate_timer_to_node(_, _)).Times(0);
  _chronos->resynchronize();
}

TEST_F(TestChronosInternalConnection, ResynchronizeWithGetRequestFailed)
{
  // GET request fails
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = HTTP_BAD_REQUEST;

  // There should be no calls to add/replicate a timer
  EXPECT_CALL(*_th, add_timer_to_store(_)).Times(0);
  EXPECT_CALL(*_replicator, replicate_timer_to_node(_, _)).Times(0);
  _chronos->resynchronize();
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultsInvalidJSON)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":]}";
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultsNoTimers)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timer\":[]}";
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidEntryNoTimerObject)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":[\"Timer\"]}";
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidEntryNoReplicas)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":[{\"TimerID\":4}]}";
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultNoTimer)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\"]}]}";
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultInvalidTimers)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?node-for-replicas=10.0.0.1:9999;sync-mode=SCALE;cluster-view-id=cluster-view-id"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\"], \"Timer\": {}}, {\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\"], \"Timer\": {\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": {}}}]}";
  HTTPCode status = _chronos->resynchronise_with_single_node("10.0.0.1:9999", _cluster_addresses, _local_ip);
  EXPECT_EQ(status, 400);
}
