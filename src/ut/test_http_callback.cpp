/**
 * @file test_callback.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "globals.h"
#include "http_callback.h"
#include "base.h"
#include "fakecurl.hpp"
#include "fakehttpresolver.hpp"
#include "mock_timer_handler.h"
#include "timer_helper.h"

/// Fixture for HTTPCallbackTest.
class TestHTTPCallback : public Base
{
protected:
  void SetUp()
  {
    Base::SetUp();

    _resolver = new FakeHttpResolver("10.42.42.42");
    _th = new MockTimerHandler();
    _callback = new HTTPCallback(_resolver, NULL);
    _callback->start(_th);

    fakecurl_responses.clear();
    fakecurl_requests.clear();
  }

  void TearDown()
  {
    delete _callback;
    delete _th;
    delete _resolver;

    Base::TearDown();
  }

  FakeHttpResolver* _resolver;
  MockTimerHandler* _th;
  HTTPCallback* _callback;
};

// Test successful timer callback
TEST_F(TestHTTPCallback, Success)
{
  fakecurl_responses["http://10.42.42.42:80/callback1"] = CURLE_OK;
  Timer* timer1 = default_timer(1);
  EXPECT_CALL(*_th, return_timer(timer1));
  EXPECT_CALL(*_th, handle_successful_callback(1));
  _callback->perform(timer1);

  // The timer's been sent when fakecurl records the request. Sleep until then.
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://localhost:80/callback1");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://localhost:80/callback1");
  }

  EXPECT_LT(count, 10) << "No request was sent that matched the expected timer";

  // Check the body on the request is expected.
  Request& request = fakecurl_requests["http://localhost:80/callback1"];
  rapidjson::Document doc;
  EXPECT_EQ(request._body, "stuff stuff stuff");

  delete timer1; timer1 = NULL;
}

// Test failed timer callback
TEST_F(TestHTTPCallback, Failure)
{
  fakecurl_responses["http://10.42.42.42:80/callback1"] = CURLE_REMOTE_FILE_NOT_FOUND;
  Timer* timer1 = default_timer(1);
  EXPECT_CALL(*_th, return_timer(timer1));
  EXPECT_CALL(*_th, handle_failed_callback(1));
  _callback->perform(timer1);

  // The timer's been sent when fakecurl records the request. Sleep until then.
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://localhost:80/callback1");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://localhost:80/callback1");
  }

  EXPECT_LT(count, 10) << "No request was sent that matched the expected timer";

  delete timer1; timer1 = NULL;
}
