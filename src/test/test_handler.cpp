#include "handlers.h"
#include "fake_timer_handler.h"
#include "mock_replicator.h"
#include "mockhttpstack.hpp"
#include "base.h"
#include "test_interposer.hpp"
#include "mock_timer_store.h"
#include "timer_handler.h"
#include "globals.h"
#include <gtest/gtest.h>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/
using ::testing::_;
using ::testing::Return;

class TestHandler : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();

    _replicator = new MockReplicator();
    _th = new FakeTimerHandler();
    _store = new MockTimerStore();
    _httpstack = new MockHttpStack();
  }

  void TearDown()
  {
    delete _req;
    delete _cfg;

    delete _httpstack;
    delete _store;
    delete _th;
    delete _replicator;

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

    _cfg = new ControllerTask::Config(_replicator, _th, _store);
    _task = new ControllerTask(*_req, _cfg, 0);
  }

  MockReplicator* _replicator;
  FakeTimerHandler* _th;
  MockHttpStack* _httpstack;
  MockTimerStore* _store;

  MockHttpStack::Request* _req;
  ControllerTask::Config* _cfg;
  ControllerTask* _task;
};

// Tests a valid request to delete an existing timer
TEST_F(TestHandler, ValidJSONDeleteTimer)
{
  controller_request("/timers/12341234123412341234123412341234", htp_method_DELETE, "", "");
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();
}

// Tests a valid request to create a new timer
TEST_F(TestHandler, ValidJSONCreateTimer)
{
  controller_request("/timers", htp_method_POST, "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}", "");
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();
}

// Tests that a delete request for timer references that doesn't have any 
// entries returns a 202 and doesn't try to edit the store
TEST_F(TestHandler, ValidTimerReferenceNoEntries)
{
  controller_request("/timers/references", htp_method_DELETE, "{\"IDs\": []}", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 202, _));
  _task->run();
}

// Tests that a delete request for timer references that doesn't have any
// entries returns a 202 and doesn't try to edit the store
TEST_F(TestHandler, ValidTimerReferenceEntry)
{
  controller_request("/timers/references", htp_method_DELETE, "{\"IDs\": [{\"ID\": 123, \"replica index\": 1}]}", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 202, _));
  EXPECT_CALL(*_store, update_replica_tracker(123, 1));
  _task->run();
}

// Tests that a delete request for timer references that has multiple entries, some of 
// which are valid. Check that the request returns a 202 and only updates
// the store for valid entries
TEST_F(TestHandler, ValidTimerReferenceNoTopLevelMixOfValidInvalidEntries)
{
  controller_request("/timers/references", htp_method_DELETE, "{\"IDs\": [{\"ID\": 123, \"replica index\": 1}, {\"NotID\": 234, \"replica index\": 2}, {\"ID\": 345, \"Notreplica index\": 3}, {\"ID\": 456, \"replica index\": 4}]}", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 202, _));
  EXPECT_CALL(*_store, update_replica_tracker(123, 1));
  EXPECT_CALL(*_store, update_replica_tracker(456, 4));
  _task->run();
}

// Tests that get requests for timer references with a
// lead to the store being queried, using the range header if set. 
TEST_F(TestHandler, ValidTimerGetCurrentNodeNoRangeHeader)
{
  controller_request("/timers?requesting-node=10.0.0.1;sync-mode=SCALE", htp_method_GET, "", "requesting-node=10.0.0.1;sync-mode=SCALE");
  EXPECT_CALL(*_store, get_timers_to_recover("10.0.0.1", _, 0)).WillOnce(Return(200));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();
}

// Tests that get requests for timer references with a
// lead to the store being queried, using the range header if set.
TEST_F(TestHandler, ValidTimerGetCurrentNodeRangeHeader)
{
  controller_request("/timers?requesting-node=10.0.0.1;sync-mode=SCALE", htp_method_GET, "", "requesting-node=10.0.0.1;sync-mode=SCALE");
  _req->add_header_to_incoming_req("Range", "100");
  EXPECT_CALL(*_store, get_timers_to_recover("10.0.0.1", _, 100)).WillOnce(Return(206));
  EXPECT_CALL(*_httpstack, send_reply(_, 206, _));
  _task->run();
}

// Tests that get requests for timer references for a leaving node
// are correctly processed 
TEST_F(TestHandler, ValidTimerGetLeavingNode)
{
  // Set leaving addresses in globals so that we look there as well.
  std::vector<std::string> leaving_cluster_addresses;
  leaving_cluster_addresses.push_back("10.0.0.4");

  __globals->lock();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  __globals->unlock();

  controller_request("/timers?requesting-node=10.0.0.4;sync-mode=SCALE", htp_method_GET, "", "requesting-node=10.0.0.4;sync-mode=SCALE");
  EXPECT_CALL(*_store, get_timers_to_recover("10.0.0.4", _, _)).WillOnce(Return(200));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  _task->run();

  leaving_cluster_addresses.clear();
  __globals->lock();
  __globals->set_cluster_leaving_addresses(leaving_cluster_addresses);
  __globals->unlock();
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
TEST_F(TestHandler, InvalidMethodWithTimer)
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

// Invalid request: Tests that requests for timer references with an empty body
// get rejected
TEST_F(TestHandler, InvalidNoBodyTimerReference)
{
  controller_request("/timers/references", htp_method_DELETE, "", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that requests for timer references with an invalid body
// get rejected
TEST_F(TestHandler, InvalidBodyTimerReferences)
{
  controller_request("/timers/references", htp_method_DELETE, "{\"}", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that requests for timer references with an invalid body
// get rejected
TEST_F(TestHandler, InvalidBodyNoTopLevelEntryTimerReferences)
{
  controller_request("/timers/references", htp_method_DELETE, "{\"NotIDs\": []}", "");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that get requests for timer references with a 
// missing requesting-node parameter gets rejected
TEST_F(TestHandler, InvalidTimerGetMissingRequestNode)
{
  controller_request("/timers?sync-mode=SCALE", htp_method_GET, "", "sync-mode=SCALE");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that get requests for timer references with a
// missing sync-mode parameter gets rejected
TEST_F(TestHandler, InvalidTimerGetMissingSyncMode)
{
  controller_request("/timers?requesting-node=10.0.0.1", htp_method_GET, "", "requesting-node=10.0.0.1");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that get requests for timer references with a
// invalid requesting-node parameter gets rejected
TEST_F(TestHandler, InvalidTimerGetInvalidRequestNode)
{
  controller_request("/timers?requesting-node=10.0.0.5;sync-mode=SCALE", htp_method_GET, "", "requesting-node=10.0.0.5;sync-mode=SCALE");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

// Invalid request: Tests that get requests for timer references with a
// invalid sync-mode parameter gets rejected
TEST_F(TestHandler, InvalidTimerGetInvalidSyncMode)
{
  controller_request("/timers?requesting-node=10.0.0.1;sync-mode=NOTSCALE", htp_method_GET, "", "requesting-node=10.0.0.1;sync-mode=NOTSCALE");
  EXPECT_CALL(*_httpstack, send_reply(_, 400, _));
  _task->run();
}

