/**
 * @file test_callback.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2016  Metaswitch Networks Ltd
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
    _callback = new HTTPCallback(_resolver);
    _callback->start(_th);

    fakecurl_responses.clear();
    fakecurl_requests.clear();
  }

  void TearDown()
  {
    _callback->stop();
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
      fakecurl_requests.find("http://10.42.42.42:80/callback1");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://10.42.42.42:80/callback1");
  }

  EXPECT_LT(count, 10) << "No request was sent that matched the expected timer";

  // Look at the body sent on the request. Check that it doesn't have any
  // replica information, and that it makes a valid timer
  Request& request = fakecurl_requests["http://10.42.42.42:80/callback1"];
  rapidjson::Document doc;
  EXPECT_EQ(request._body, "stuff stuff stuff");

  delete timer1; timer1 = NULL;
}

// Test failed timer callback
TEST_F(TestHTTPCallback, Failure)
{
  fakecurl_responses["http://10.42.42.42:80/callback2"] = CURLE_REMOTE_FILE_NOT_FOUND;
  Timer* timer2 = default_timer(2);
  EXPECT_CALL(*_th, return_timer(timer2));
  EXPECT_CALL(*_th, handle_failed_callback(2));
  _callback->perform(timer2);

  // The timer's been sent when fakecurl records the request. Sleep until then.
  std::map<std::string, Request>::iterator it =
      fakecurl_requests.find("http://10.42.42.42:80/callback2");
  int count = 0;
  while (it == fakecurl_requests.end() && count < 10)
  {
    // Don't wait for more than 10 seconds
    count++;
    sleep(1);
    it = fakecurl_requests.find("http://10.42.42.42:80/callback2");
  }

  EXPECT_LT(count, 10) << "No request was sent that matched the expected timer";

  delete timer2; timer2 = NULL;
}
