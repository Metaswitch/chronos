/**
 * @file base.cpp
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "base.h"
#include "globals.h"

#include <gtest/gtest.h>

void Base::SetUp()
{
  // Set up globals to something sensible
  __globals = new Globals("/etc/chronos/chronos.conf",
                          "/etc/chronos/chronos_cluster.conf",
                          "/etc/chronos/chronos_gr.conf");
  __globals->lock();
  std::string localhost = "10.0.0.1";
  std::string localhost_port = "10.0.0.1:9999";
  __globals->set_cluster_local_ip(localhost_port);
  __globals->set_bind_address(localhost);
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  cluster_addresses.push_back("10.0.0.2");
  cluster_addresses.push_back("10.0.0.3");
  __globals->set_cluster_staying_addresses(cluster_addresses);
  std::map<std::string, uint64_t> cluster_bloom_filters;

  cluster_bloom_filters["10.0.0.1:9999"] = 0x00010000010001;
  cluster_bloom_filters["10.0.0.2"] = 0x10001000001000;
  cluster_bloom_filters["10.0.0.3"] = 0x01000100000100;
  __globals->set_cluster_bloom_filters(cluster_bloom_filters);
  std::vector<uint32_t> cluster_rendezvous_hashes = __globals->generate_hashes(cluster_addresses);
  __globals->set_new_cluster_hashes(cluster_rendezvous_hashes);
  __globals->set_old_cluster_hashes(cluster_rendezvous_hashes);

  std::string cluster_view_id = "cluster-view-id";
  __globals->set_cluster_view_id(cluster_view_id);
  int bind_port = 9999;
  __globals->set_bind_port(bind_port);

  Globals::TimerIDFormat timer_id_format = __globals->default_id_format();
  __globals->set_timer_id_format(timer_id_format);

  uint32_t instance_id = 42;
  uint32_t deployment_id = 3;
  __globals->set_instance_id(instance_id);
  __globals->set_deployment_id(deployment_id);

  std::string local_site_name = "local_site_name";
  __globals->set_local_site_name(local_site_name);

  std::map<std::string, std::string> remote_sites;
  remote_sites.insert(std::make_pair("remote_site_1_name", "remote_site_1_dns_record"));
  __globals->set_remote_sites(remote_sites);

  std::vector<std::string> remote_site_names;
  remote_site_names.push_back("remote_site_1_name");
  __globals->set_remote_site_names(remote_site_names);

  std::vector<std::string> remote_site_dns_records;
  remote_site_dns_records.push_back("remote_site_1_dns_record");
  __globals->set_remote_site_dns_records(remote_site_dns_records);

  __globals->unlock();
}

void Base::TearDown()
{
  delete __globals;
  __globals = NULL;
}
