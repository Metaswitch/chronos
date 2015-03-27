#include "gtest/gtest.h"

#include "fakehttpresolver.hpp"
#include "chronos_internal_connection.h"
#include "base.h"
#include "fakecurl.hpp"
#include "mock_replicator.h"
#include "mock_timer_handler.h"
#include "globals.h"

using namespace std;
using ::testing::_;
using ::testing::Return;
using ::testing::SaveArg;

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

    _resolver = new FakeHttpResolver("10.42.42.42");
    _replicator = new MockReplicator();
    _th = new MockTimerHandler();
    _chronos = new ChronosInternalConnection(_resolver, 
                                             _th,
                                             _replicator);

    fakecurl_responses.clear();
  }

  void TearDown()
  {
    delete _chronos;
    delete _th;
    delete _replicator;
    delete _resolver;

    Base::TearDown();
  }

  FakeHttpResolver* _resolver;
  MockReplicator* _replicator;
  MockTimerHandler* _th;
  ChronosInternalConnection* _chronos;
};

TEST_F(TestChronosInternalConnection, SendDelete)
{
  fakecurl_responses["http://10.42.42.42:80/timers/references"] = CURLE_OK;
  HTTPCode status = _chronos->send_delete("10.42.42.42:80", "{}");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendGet)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = CURLE_OK;
  std::string response;
  HTTPCode status = _chronos->send_get("10.42.42.42:80", "10.0.0.1:9999", "SCALE", 0, response);
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerNoResults)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timers\":[]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerOneTimer)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\", \"10.0.0.3:9999\"], \"Timer\": {\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.3:9999\" ] }}}]}";
  Timer* added_timer;

  EXPECT_CALL(*_th, add_timer(_)).WillOnce(SaveArg<0>(&added_timer));

  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsNotTombstone(), "10.0.0.3:9999")); // Update
  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsTombstone(), "10.0.0.2:9999")); // Tombstone
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 200);

  delete added_timer; added_timer = NULL;
}

TEST_F(TestChronosInternalConnection, SendTriggerOneTimerWithTombstoneAndLeaving)
{
  // Set leaving addresses in globals so that we look there as well.
  std::vector<std::string> leaving_cluster_addresses;
  leaving_cluster_addresses.push_back("10.0.0.4:9999");

  __globals->lock();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  __globals->unlock();

  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\", \"10.0.0.4:9999\"], \"Timer\": {\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1:9999\", \"10.0.0.3:9999\" ] }}}]}";
  fakecurl_responses["http://10.42.42.42:9999/timers/references"] = CURLE_OK;
  Timer* added_timer;

  EXPECT_CALL(*_th, add_timer(_)).WillOnce(SaveArg<0>(&added_timer));
  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsTombstone(), "10.0.0.2:9999")); // Tombstone
  EXPECT_CALL(*_replicator, replicate_timer_to_node(IsNotTombstone(), "10.0.0.3:9999")); // Update
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 200);

  leaving_cluster_addresses.clear();
  __globals->lock();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  __globals->unlock();

  delete added_timer; added_timer = NULL;
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultsInvalidJSON)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timers\":]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultsNoTimers)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timer\":[]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidEntryNoTimerObject)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timers\":[\"Timer\"]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidEntryNoReplicas)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4}]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultNoTimer)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\"]}]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultInvalidTimer)
{
  fakecurl_responses["http://10.42.42.42:9999/timers?requesting-node=10.0.0.1:9999;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2:9999\"], \"Timer\": {}}]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1:9999");
  EXPECT_EQ(status, 200);
}
