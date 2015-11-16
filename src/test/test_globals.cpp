/**
 * @file test_globals.cpp
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

#include "globals.h"
#include "test_interposer.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <map>

static const std::string UT_FILE(__FILE__);
const std::string UT_DIR = UT_FILE.substr(0, UT_FILE.rfind("/"));

class TestGlobals : public ::testing::Test
{
};

TEST_F(TestGlobals, ParseGlobalsDefaults)
{
  // Initialize the global configuration. Use default configuration
  Globals* test_global = new Globals("./no_config_file",
                                     "./no_cluster_config_file");

  // Read all global entries
  test_global->update_config();

  // Check that the default values are used
  std::string bind_address;
  test_global->get_bind_address(bind_address);
  EXPECT_EQ(bind_address, "0.0.0.0");

  int bind_port;
  test_global->get_bind_port(bind_port);
  EXPECT_EQ(bind_port, 7253);

  int threads;
  test_global->get_threads(threads);
  EXPECT_EQ(threads, 50);

  int ttl;
  test_global->get_max_ttl(ttl);
  EXPECT_EQ(ttl, 600);

  std::vector<std::string> dns_servers;
  test_global->get_dns_servers(dns_servers);
  EXPECT_EQ(dns_servers.size(), 1);
  EXPECT_EQ(dns_servers[0], "127.0.0.1");

  std::string cluster_local_address;
  test_global->get_cluster_local_ip(cluster_local_address);
  EXPECT_EQ(cluster_local_address, "localhost:7253");

  std::vector<std::string> cluster_addresses;
  test_global->get_cluster_addresses(cluster_addresses);
  EXPECT_EQ(cluster_addresses.size(), 1);
  EXPECT_EQ(cluster_addresses[0], "localhost:7253");

  std::vector<std::string> cluster_leaving_addresses;
  test_global->get_cluster_leaving_addresses(cluster_leaving_addresses);
  EXPECT_EQ(cluster_leaving_addresses.size(), 0);

  delete test_global; test_global = NULL;
}

TEST_F(TestGlobals, ParseGlobalsNotDefaults)
{
  // Initialize the global configuration. Use default configuration
  Globals* test_global = new Globals(std::string(UT_DIR).append("/chronos.conf"),
                                     std::string(UT_DIR).append("/chronos_cluster.conf"));

  // Read all global entries
  test_global->update_config();

  // Check that the values from the config files are used
  std::string bind_address;
  test_global->get_bind_address(bind_address);
  EXPECT_EQ(bind_address, "1.2.3.4");

  int bind_port;
  test_global->get_bind_port(bind_port);
  EXPECT_EQ(bind_port, 7254);

  int threads;
  test_global->get_threads(threads);
  EXPECT_EQ(threads, 40);

  int ttl;
  test_global->get_max_ttl(ttl);
  EXPECT_EQ(ttl, 500);

  std::vector<std::string> dns_servers;
  test_global->get_dns_servers(dns_servers);
  EXPECT_EQ(dns_servers.size(), 3);
  EXPECT_EQ(dns_servers[0], "1.1.1.1");
  EXPECT_EQ(dns_servers[1], "2.2.2.2");
  EXPECT_EQ(dns_servers[2], "3.3.3.3");

  std::string cluster_local_address;
  test_global->get_cluster_local_ip(cluster_local_address);
  EXPECT_EQ(cluster_local_address, "1.2.3.4");

  std::vector<std::string> cluster_addresses;
  test_global->get_cluster_addresses(cluster_addresses);
  EXPECT_EQ(cluster_addresses.size(), 2);
  EXPECT_EQ(cluster_addresses[0], "1.2.3.4");
  EXPECT_EQ(cluster_addresses[1], "1.2.3.5");

  std::vector<std::string> cluster_leaving_addresses;
  test_global->get_cluster_leaving_addresses(cluster_leaving_addresses);
  EXPECT_EQ(cluster_leaving_addresses.size(), 2);
  EXPECT_EQ(cluster_leaving_addresses[0], "2.3.4.5");
  EXPECT_EQ(cluster_leaving_addresses[1], "2.3.4.6");

  delete test_global; test_global = NULL;
}
