/**
 * @file base.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
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

#include "base.h"
#include "globals.h"

#include <gtest/gtest.h>

void Base::SetUp()
{
  // Set up globals to something sensible
  __globals = new Globals("/etc/chronos/chronos.conf",
                          "/etc/chronos/chronos_cluster.conf");
  __globals->lock();
  std::string localhost = "10.0.0.1";
  std::string localhost_port = "10.0.0.1:9999";
  __globals->set_cluster_local_ip(localhost_port);
  __globals->set_bind_address(localhost);
  std::vector<std::string> cluster_addresses;
  cluster_addresses.push_back("10.0.0.1:9999");
  cluster_addresses.push_back("10.0.0.2:9999");
  cluster_addresses.push_back("10.0.0.3:9999");
  __globals->set_cluster_staying_addresses(cluster_addresses);
  std::map<std::string, uint64_t> cluster_bloom_filters;

  cluster_bloom_filters["10.0.0.1:9999"] = 0x00010000010001;
  cluster_bloom_filters["10.0.0.2:9999"] = 0x10001000001000;
  cluster_bloom_filters["10.0.0.3:9999"] = 0x01000100000100;
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

  __globals->unlock();
}

void Base::TearDown()
{
  delete __globals;
  __globals = NULL;
}
