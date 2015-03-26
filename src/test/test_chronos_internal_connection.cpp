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
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = CURLE_OK;
  std::string response;
  HTTPCode status = _chronos->send_get("10.42.42.42:80", "10.0.0.1", "SCALE", 0, response);
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerNoResults)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timers\":[]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerOneTimer)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2\", \"10.0.0.3\"], \"Timer\": {\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1\", \"10.0.0.3\" ] }}}]}";
  EXPECT_CALL(*_th, add_timer(_));
  EXPECT_CALL(*_replicator, replicate_timer_to_node(_, "10.0.0.3"));  // Update
  EXPECT_CALL(*_replicator, replicate_timer_to_node(_, "10.0.0.2")); // Tombstone
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerOneTimerWithTombstoneAndLeaving)
{
  // Set leaving addresses in globals so that we look there as well.
  std::vector<std::string> leaving_cluster_addresses;
  leaving_cluster_addresses.push_back("10.0.0.4");

  __globals->lock();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  __globals->unlock();

  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2\", \"10.0.0.4\"], \"Timer\": {\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}, \"reliability\": { \"replicas\": [ \"10.0.0.1\", \"10.0.0.3\" ] }}}]}";
  fakecurl_responses["http://10.42.42.42:80/timers/references"] = CURLE_OK;
  EXPECT_CALL(*_th, add_timer(_));
  EXPECT_CALL(*_replicator, replicate_timer_to_node(_, "10.0.0.2")); // Tombstone
  EXPECT_CALL(*_replicator, replicate_timer_to_node(_, "10.0.0.3")); // Update
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 200);

  leaving_cluster_addresses.clear();
  __globals->lock();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  __globals->unlock();
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultsInvalidJSON)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timers\":]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultsNoTimers)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timer\":[]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 400);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidEntryNoTimerObject)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timers\":[\"Timer\"]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidEntryNoReplicas)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4}]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultNoTimer)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2\"]}]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 200);
}

TEST_F(TestChronosInternalConnection, SendTriggerInvalidResultInvalidTimer)
{
  fakecurl_responses["http://10.42.42.42:80/timers?requesting-node=10.0.0.1;sync-mode=SCALE"] = "{\"Timers\":[{\"TimerID\":4, \"OldReplicas\":[\"10.0.0.2\"], \"Timer\": {}}]}";
  HTTPCode status = _chronos->trigger_move_for_one_server("10.0.0.1");
  EXPECT_EQ(status, 200);
}
