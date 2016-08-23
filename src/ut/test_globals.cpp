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

using ::testing::UnorderedElementsAreArray;

static const std::string UT_FILE(__FILE__);
const std::string UT_DIR = UT_FILE.substr(0, UT_FILE.rfind("/"));

class TestGlobals : public ::testing::Test
{
};

TEST_F(TestGlobals, ParseGlobalsDefaults)
{
  // Initialize the global configuration. Use default configuration
  Globals* test_global = new Globals("./no_config_file",
                                     "./no_cluster_config_file",
                                     "./no_gr_config_file");

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
  EXPECT_EQ(dns_servers.size(), (unsigned)1);
  EXPECT_EQ(dns_servers[0], "127.0.0.1");

  std::string cluster_local_address;
  test_global->get_cluster_local_ip(cluster_local_address);
  EXPECT_EQ(cluster_local_address, "localhost:7253");

  std::vector<std::string> cluster_addresses;
  test_global->get_cluster_staying_addresses(cluster_addresses);
  EXPECT_EQ(cluster_addresses.size(), (unsigned)1);
  EXPECT_EQ(cluster_addresses[0], "localhost:7253");

  std::vector<std::string> cluster_joining_addresses;
  test_global->get_cluster_joining_addresses(cluster_joining_addresses);
  EXPECT_EQ(cluster_joining_addresses.size(), (unsigned)0);

  std::vector<std::string> cluster_leaving_addresses;
  test_global->get_cluster_leaving_addresses(cluster_leaving_addresses);
  EXPECT_EQ(cluster_leaving_addresses.size(), (unsigned)0);

  std::vector<uint32_t> new_cluster_hashes;
  std::vector<uint32_t> old_cluster_hashes;
  test_global->get_new_cluster_hashes(new_cluster_hashes);
  test_global->get_old_cluster_hashes(old_cluster_hashes);
  EXPECT_EQ(new_cluster_hashes, old_cluster_hashes);

  Globals::TimerIDFormat timer_id_format;
  test_global->get_timer_id_format(timer_id_format);
  EXPECT_EQ(timer_id_format, Globals::TimerIDFormat::WITHOUT_REPLICAS);

  std::string local_site_name;
  test_global->get_local_site_name(local_site_name);
  EXPECT_EQ(local_site_name, "site1");

  std::map<std::string, std::string> remote_sites;
  std::vector<std::string> remote_site_names;
  std::vector<std::string> remote_site_dns_records;
  test_global->get_remote_sites(remote_sites);
  test_global->get_remote_site_names(remote_site_names);
  test_global->get_remote_site_dns_records(remote_site_dns_records);

  EXPECT_EQ(remote_sites.size(), 0);
  EXPECT_EQ(remote_site_names.size(), 0);
  EXPECT_EQ(remote_site_dns_records.size(), 0);

  delete test_global; test_global = NULL;
}

TEST_F(TestGlobals, ParseGlobalsNotDefaults)
{
  // Initialize the global configuration. Use default configuration
  Globals* test_global = new Globals(std::string(UT_DIR).append("/chronos.conf"),
                                     std::string(UT_DIR).append("/chronos_cluster.conf"),
                                     std::string(UT_DIR).append("/chronos_gr.conf"));

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
  EXPECT_EQ(dns_servers.size(), (unsigned)3);
  EXPECT_EQ(dns_servers[0], "1.1.1.1");
  EXPECT_EQ(dns_servers[1], "2.2.2.2");
  EXPECT_EQ(dns_servers[2], "3.3.3.3");

  std::string cluster_local_address;
  test_global->get_cluster_local_ip(cluster_local_address);
  EXPECT_EQ(cluster_local_address, "1.2.3.4");

  std::vector<std::string> cluster_addresses;
  test_global->get_cluster_staying_addresses(cluster_addresses);
  EXPECT_EQ(cluster_addresses.size(), (unsigned)2);
  EXPECT_EQ(cluster_addresses[0], "1.2.3.4");
  EXPECT_EQ(cluster_addresses[1], "1.2.3.5");

  std::vector<std::string> cluster_joining_addresses;
  test_global->get_cluster_joining_addresses(cluster_joining_addresses);
  EXPECT_EQ(cluster_joining_addresses.size(), (unsigned)2);
  EXPECT_EQ(cluster_joining_addresses[0], "3.4.5.6");
  EXPECT_EQ(cluster_joining_addresses[1], "3.4.5.7");

  std::vector<std::string> cluster_leaving_addresses;
  test_global->get_cluster_leaving_addresses(cluster_leaving_addresses);
  EXPECT_EQ(cluster_leaving_addresses.size(), (unsigned)2);
  EXPECT_EQ(cluster_leaving_addresses[0], "2.3.4.5");
  EXPECT_EQ(cluster_leaving_addresses[1], "2.3.4.6");

  std::vector<uint32_t> new_cluster_hashes;
  std::vector<uint32_t> old_cluster_hashes;
  test_global->get_new_cluster_hashes(new_cluster_hashes);
  test_global->get_old_cluster_hashes(old_cluster_hashes);
  EXPECT_NE(new_cluster_hashes, old_cluster_hashes);

  Globals::TimerIDFormat timer_id_format;
  test_global->get_timer_id_format(timer_id_format);
  EXPECT_EQ(timer_id_format, Globals::TimerIDFormat::WITHOUT_REPLICAS);

  std::string local_site_name;
  test_global->get_local_site_name(local_site_name);
  EXPECT_EQ(local_site_name, "mysite");

  std::map<std::string, std::string> remote_sites;
  std::vector<std::string> remote_site_names;
  std::vector<std::string> remote_site_dns_records;
  test_global->get_remote_sites(remote_sites);
  test_global->get_remote_site_names(remote_site_names);
  test_global->get_remote_site_dns_records(remote_site_dns_records);

  // Site C will be stripped as it doesn't have an address, so we only expect
  // to see two entries. Site mysite will be stripped as it's the same as the
  // local site.
  EXPECT_EQ(remote_sites.size(), 2);
  EXPECT_EQ(remote_sites["a"], "foo.com:800");
  EXPECT_EQ(remote_sites["b"], "bar.com");
  EXPECT_EQ(remote_site_names.size(), 2);
  std::vector<std::string> expected_remote_site_names;
  expected_remote_site_names.push_back("a");
  expected_remote_site_names.push_back("b");
  EXPECT_THAT(expected_remote_site_names, UnorderedElementsAreArray(remote_site_names));
  EXPECT_EQ(remote_site_dns_records.size(), 2);
  std::vector<std::string> expected_remote_site_dns_records;
  expected_remote_site_dns_records.push_back("foo.com:800");
  expected_remote_site_dns_records.push_back("bar.com");
  EXPECT_THAT(remote_site_dns_records, UnorderedElementsAreArray(expected_remote_site_dns_records));

  delete test_global; test_global = NULL;
}
