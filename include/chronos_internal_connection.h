/**
 * @file chronos_internal_connection.h
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

#ifndef CHRONOSINTERNALCONNECTION_H__
#define CHRONOSINTERNALCONNECTION_H__

#include "httpclient.h"
#include "timer.h"
#include "timer_handler.h"
#include "replicator.h"
#include "updater.h"
#include "statistic.h"
#include "counter.h"
#include "snmp_counter_table.h"
#include "snmp_scalar.h"

/// @class ChronosInternalConnection
class ChronosInternalConnection
{
public:
  ChronosInternalConnection(HttpResolver* resolver,
                            TimerHandler* handler,
                            Replicator* replicator,
                            Alarm* alarm,
                            SNMP::U32Scalar* _remaining_nodes_scalar = NULL,
                            SNMP::CounterTable* _timers_processed_table = NULL,
                            SNMP::CounterTable* _invalid_timers_processed_table = NULL);
  virtual ~ChronosInternalConnection();

  // Performs a resynchronization operation
  // the timers on this node with all the other Chronos nodes
  virtual void resynchronize();

private:
  HttpClient* _http;
  TimerHandler* _handler;
  Replicator* _replicator;
  Alarm* _alarm;
  SNMP::U32Scalar* _remaining_nodes_scalar;
  SNMP::CounterTable* _timers_processed_table;
  SNMP::CounterTable* _invalid_timers_processed_table;
  Updater<void, ChronosInternalConnection>* _updater;

  // Creates the body to use in a delete request. This is a JSON
  // encoded string of the format:
  //  {"IDs": [{"ID": 123, "ReplicaIndex": 0},
  //           {"ID": 456, "ReplicaIndex": 2},
  //          ...]
  std::string create_delete_body(std::map<TimerID, int> delete_map);

  // Returns whether a node is present in a replica list
  bool get_replica_presence(std::string current_node,
                            std::vector<std::string> replicas);

  // Returns whether a node is present in a replica list, and if it
  // is what index it is
  bool get_replica_level(int& index,
                         std::string current_node,
                         std::vector<std::string> replicas);

  // Sends a delete request
  virtual HTTPCode send_delete(const std::string& server,
                               const std::string& body);

  // Sends a get request
  virtual HTTPCode send_get(const std::string& server,
                            const std::string& requesting_node,
                            std::string cluster_view_id,
                            uint64_t time_from,
                            int max_timers,
                            std::string& response);

  // Resynchronises with a single Chronos node (used in resync operations).
  virtual HTTPCode resynchronise_with_single_node(
                            const std::string& server_to_sync,
                            std::vector<std::string> cluster_nodes,
                            std::string localhost);
};

#endif

