/**
 * @file test_globals.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
  Globals* test_global = new Globals("./no_local_config_file",
                                     "./no_cluster_config_file",
                                     "./no_shared_config_file");

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

  int gr_threads;
  test_global->get_gr_threads(gr_threads);
  EXPECT_EQ(gr_threads, 50);

  int ttl;
  test_global->get_max_ttl(ttl);
  EXPECT_EQ(ttl, 600);

  std::vector<std::string> dns_servers;
  test_global->get_dns_servers(dns_servers);
  EXPECT_EQ(dns_servers.size(), (unsigned)1);
  EXPECT_EQ(dns_servers[0], "127.0.0.1");

  int dns_timeout;
  test_global->get_dns_timeout(dns_timeout);
  EXPECT_EQ(dns_timeout, 200);

  int dns_port;
  test_global->get_dns_port(dns_port);
  EXPECT_EQ(dns_port, 53);

  std::string cluster_local_address;
  test_global->get_cluster_local_ip(cluster_local_address);
  EXPECT_EQ(cluster_local_address, "127.0.0.1:7253");

  std::vector<std::string> cluster_addresses;
  test_global->get_cluster_staying_addresses(cluster_addresses);
  EXPECT_EQ(cluster_addresses.size(), (unsigned)1);
  EXPECT_EQ(cluster_addresses[0], "127.0.0.1:7253");

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

  bool replicate_timers_across_sites;
  test_global->get_replicate_timers_across_sites(replicate_timers_across_sites);
  EXPECT_EQ(replicate_timers_across_sites, false);

  delete test_global; test_global = NULL;
}

TEST_F(TestGlobals, ParseGlobalsNotDefaults)
{
  // Initialize the global configuration. Use default configuration
  Globals* test_global = new Globals(std::string(UT_DIR).append("/chronos.conf"),
                                     std::string(UT_DIR).append("/chronos_cluster.conf"),
                                     std::string(UT_DIR).append("/chronos_shared.conf"));

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

  int gr_threads;
  test_global->get_gr_threads(gr_threads);
  EXPECT_EQ(gr_threads, 30);

  int ttl;
  test_global->get_max_ttl(ttl);
  EXPECT_EQ(ttl, 500);

  std::vector<std::string> dns_servers;
  test_global->get_dns_servers(dns_servers);
  EXPECT_EQ(dns_servers.size(), (unsigned)3);
  EXPECT_EQ(dns_servers[0], "1.1.1.1");
  EXPECT_EQ(dns_servers[1], "2.2.2.2");
  EXPECT_EQ(dns_servers[2], "3.3.3.3");

  int dns_timeout;
  test_global->get_dns_timeout(dns_timeout);
  EXPECT_EQ(dns_timeout, 500);

  int dns_port;
  test_global->get_dns_port(dns_port);
  EXPECT_EQ(dns_port, 5353);

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
  // local site. Site b with have the bind port added to its URI as there's no
  // existing port.
  EXPECT_EQ(remote_sites.size(), 2);
  EXPECT_EQ(remote_sites["a"], "foo.com:800");
  EXPECT_EQ(remote_sites["b"], "bar.com:7254");
  EXPECT_EQ(remote_site_names.size(), 2);
  std::vector<std::string> expected_remote_site_names;
  expected_remote_site_names.push_back("a");
  expected_remote_site_names.push_back("b");
  EXPECT_THAT(expected_remote_site_names, UnorderedElementsAreArray(remote_site_names));
  EXPECT_EQ(remote_site_dns_records.size(), 2);
  std::vector<std::string> expected_remote_site_dns_records;
  expected_remote_site_dns_records.push_back("foo.com:800");
  expected_remote_site_dns_records.push_back("bar.com:7254");
  EXPECT_THAT(expected_remote_site_dns_records, UnorderedElementsAreArray(remote_site_dns_records));

  bool replicate_timers_across_sites;
  test_global->get_replicate_timers_across_sites(replicate_timers_across_sites);
  EXPECT_EQ(replicate_timers_across_sites, true);

  delete test_global; test_global = NULL;
}

TEST_F(TestGlobals, LocalConfigOverridesSharedConfig)
{
  // Initialize the global configuration.
  Globals* test_global = new Globals(std::string(UT_DIR).append("/chronos_local_override.conf"),
                                     std::string(UT_DIR).append("/chronos_cluster.conf"),
                                     std::string(UT_DIR).append("/chronos_shared.conf"));

  // Read all global entries
  test_global->update_config();

  // Check that the values from the local config files is used in preference
  // to the global files - check one value that's only in the local file, one
  // that's in the shared file, and one that's in both.
  std::string bind_address;
  test_global->get_bind_address(bind_address);
  EXPECT_EQ(bind_address, "1.2.3.4");

  std::string local_site_name;
  test_global->get_local_site_name(local_site_name);
  EXPECT_EQ(local_site_name, "mysite");

  int ttl;
  test_global->get_max_ttl(ttl);
  EXPECT_EQ(ttl, 1000);

  delete test_global; test_global = NULL;
}

TEST_F(TestGlobals, UnknownOptions)
{
  // Initialize the global configuration.
  Globals* test_global = new Globals(std::string(UT_DIR).append("/chronos_unknown_options.conf"),
                                     std::string(UT_DIR).append("/chronos_cluster.conf"),
                                     std::string(UT_DIR).append("/chronos_shared.conf"));

  // Read all global entries
  test_global->update_config();

  // This test has succeeded if we've got to this point and not crashed.
  // We also check that we can read the file that had an unknown option.
  std::string bind_address;
  test_global->get_bind_address(bind_address);
  EXPECT_EQ(bind_address, "1.2.3.4");

  delete test_global; test_global = NULL;
}
