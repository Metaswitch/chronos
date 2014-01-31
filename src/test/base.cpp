#include "base.h"
#include "globals.h"

#include <gtest/gtest.h>

void Base::SetUp()
{
  // Set up globals to something sensible
  __globals = new Globals();
  __globals->lock();
  std::string localhost = "10.0.0.1";
  __globals->set_cluster_local_ip(localhost);
  __globals->set_bind_address(localhost);
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1");
  cluster_addresses.push_back("10.0.0.2");
  cluster_addresses.push_back("10.0.0.3");
  __globals->set_cluster_addresses(cluster_addresses);
  std::map<std::string, uint64_t> cluster_hashes;
  cluster_hashes["10.0.0.1"] = 0x00010000010001;
  cluster_hashes["10.0.0.2"] = 0x10001000001000;
  cluster_hashes["10.0.0.3"] = 0x01000100000100;
  __globals->set_cluster_hashes(cluster_hashes);
  int bind_port = 9999;
  __globals->set_bind_port(bind_port);
  __globals->unlock();
}

void Base::TearDown()
{
  delete __globals;
  __globals = NULL;
}
