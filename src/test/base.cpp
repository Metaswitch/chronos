#include "base.h"
#include "globals.h"

#include <gtest/gtest.h>

void Base::SetUp()
{
  // Set up globals to something sensible
  __globals = new Globals("/etc/chronos/chronos.conf");
  __globals->lock();
  std::string localhost = "10.0.0.1";
  std::string localhost_port = "10.0.0.1:9999";
  __globals->set_cluster_local_ip(localhost_port);
  __globals->set_bind_address(localhost);
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  cluster_addresses.push_back("10.0.0.2:9999");
  cluster_addresses.push_back("10.0.0.3:9999");
  __globals->set_cluster_addresses(cluster_addresses);
  std::map<std::string, uint64_t> cluster_bloom_filters;

  cluster_bloom_filters["10.0.0.1:9999"] = 0x00010000010001;
  cluster_bloom_filters["10.0.0.2:9999"] = 0x10001000001000;
  cluster_bloom_filters["10.0.0.3:9999"] = 0x01000100000100;
  __globals->set_cluster_bloom_filters(cluster_bloom_filters);
  std::vector<uint32_t> cluster_rendezvous_hashes = __globals->generate_hashes(cluster_addresses);
  __globals->set_cluster_hashes(cluster_rendezvous_hashes);

  std::string cluster_view_id = "cluster-view-id";
  __globals->set_cluster_view_id(cluster_view_id);
  int bind_port = 9999;
  __globals->set_bind_port(bind_port);
  __globals->set_default_bind_port(bind_port);
  __globals->unlock();
}

void Base::TearDown()
{
  delete __globals;
  __globals = NULL;
}
