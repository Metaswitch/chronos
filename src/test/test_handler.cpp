#include "handlers.h"
#include "fake_timer_handler.h"
#include "mock_replicator.h"
#include "mockhttpstack.hpp"
#include "base.h"
#include "test_interposer.hpp"

#include "timer_handler.h"

#include <gtest/gtest.h>

/*****************************************************************************/
/* Test fixture                                                              */
/*****************************************************************************/
using ::testing::_;

class TestHandler : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();
    _replicator = new MockReplicator();
    _th = new FakeTimerHandler();
    _httpstack = new MockHttpStack();
  }

  void TearDown()
  {
    delete _httpstack;
    delete _th;
    delete _replicator;
    Base::TearDown();
  }

  void invalid_controller_request(std::string http_path,
                                  htp_method method,
                                  int expected_rc)
  {
    MockHttpStack::Request req(_httpstack,
                               http_path,
                               "",
                               "",
                               "",
                               method);

    ControllerTask::Config cfg(_replicator, _th);
    ControllerTask* task = new ControllerTask(req, &cfg, 0);
    EXPECT_CALL(*_httpstack, send_reply(_, expected_rc, _));
    task->run();
  }

  MockReplicator* _replicator;
  FakeTimerHandler* _th;
  MockHttpStack* _httpstack;
};

// Tests a valid request to delete an existing timer
TEST_F(TestHandler, ValidJSONDeleteTimer)
{
  MockHttpStack::Request req(_httpstack,
                             "/timers/12341234123412341234123412341234",
                             "",
                             "",
                             "",
                             htp_method_DELETE);

  ControllerTask::Config cfg(_replicator, _th);
  ControllerTask* task = new ControllerTask(req, &cfg, 0);
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  task->run();
}

// Tests a valid request to create a new timer
TEST_F(TestHandler, ValidJSONCreateTimer)
{
  MockHttpStack::Request req(_httpstack,
                             "/timers",
                             "",
                             "",
                             "{\"timing\": { \"interval\": 100, \"repeat-for\": 200 }, \"callback\": { \"http\": { \"uri\": \"localhost\", \"opaque\": \"stuff\" }}}",
                             htp_method_POST);

  ControllerTask::Config cfg(_replicator, _th);
  ControllerTask* task = new ControllerTask(req, &cfg, 0);
  EXPECT_CALL(*_replicator, replicate(_));
  EXPECT_CALL(*_httpstack, send_reply(_, 200, _));
  task->run();
}

// Invalid request: Tests the case where we attempt to create a new timer, 
// but we can't create the timer from the request
TEST_F(TestHandler, InvalidNoTimerNoBody)
{
  invalid_controller_request("/timers/", htp_method_POST, 400);
}

// Invalid request: Tests that requests to create a timer but the method is
// wrong get rejected. 
TEST_F(TestHandler, InvalidMethodNoTimer)
{
  invalid_controller_request("/timers/", htp_method_PUT, 405);
}

// Invalid request: Tests that requests to create a timer but the method is
// wrong get rejected.
TEST_F(TestHandler, InvalidMethodWithTimer)
{
  invalid_controller_request("/timers/12341234123412341234123412341234", htp_method_POST, 405);
}

// Invalid request: Tests that requests to modify a timer but timer ID is invalid
// get rejected.
TEST_F(TestHandler, InvalidTimer)
{
  invalid_controller_request("/timers/1234", htp_method_PUT, 404);
}
