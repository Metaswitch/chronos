/**
 * @file timer_helper.cpp
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

#include "timer_helper.h"

Timer* default_timer(TimerID id)
{
  // Start the timer right now.
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  // Add a single timer to the store
  Timer* timer = new Timer(id, 100000, 100000);
  timer->start_time_mono_ms = (ts.tv_sec * 1000) + (ts.tv_nsec / (1000 * 1000));
  timer->sequence_number = 0;
  timer->replicas = std::vector<std::string>(1, "10.0.0.1:9999");
  timer->sites = std::vector<std::string>(1, "local_site_name");
  timer->sites.push_back("remote_site_1_name");
  timer->tags = std::map<std::string, uint32_t> {{"TAG" + std::to_string(id), 1}};
  timer->callback_url = "localhost:80/callback" + std::to_string(id);
  timer->callback_body = "stuff stuff stuff";
  timer->_replica_tracker = 1;
  timer->_replication_factor = 1;
  timer->cluster_view_id = "cluster-view-id";

  return timer;
}
