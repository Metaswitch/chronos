/**
 * @file test_gr_replicator.cpp
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
    _conn = new ChronosGRConnection("site1", _resolver);
    _gr = new GRReplicator(std::vector<ChronosGRConnection*>(1, _conn), NULL);

    fakecurl_responses.clear();
  }

  void TearDown()
  {
    delete _gr;
    delete _conn;
    delete _resolver;

    Base::TearDown();
  }

  FakeHttpResolver* _resolver;
  ChronosGRConnection* _conn;
  GRReplicator* _gr;
};

// Test that a timer is replicated successfully
TEST_F(TestGRReplicator, ReplicateTimer)
{
  // Timer should have an ID of 1, and a replication factor of 2. If it doesn't
  // the send_put will fail
  fakecurl_responses["http://10.42.42.42:80/timers/0000000000000001-1"] = CURLE_OK;
  Timer* timer1 = default_timer(1);
  ASSERT_FALSE(timer1->replicas.empty());
  _gr->replicate(timer1);

  // Have to sleep to make sure there's time for the request to have been
  // processed
  sleep(1); 

  // Look at the body sent on the request. Check that it doesn't have any
  // replica information, and that it makes a valid timer
  Request& request = fakecurl_requests["http://10.42.42.42:80/timers/0000000000000001-1"];
  rapidjson::Document doc;
  doc.Parse<0>(request._body.c_str());
  ASSERT_FALSE(doc.HasParseError());
  ASSERT_FALSE(doc["reliability"].HasMember("replicas"));

  std::string error;
  bool replicated;
  bool gr_replicated;

  Timer* timer2 = Timer::from_json(1, 1, 0, request._body, error, replicated, gr_replicated);
  ASSERT_TRUE(timer2);

  delete timer1; timer1 = NULL;
  delete timer2; timer2 = NULL;
}
