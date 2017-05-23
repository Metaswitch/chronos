/**
 * @file chronos_internal_connection.h
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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

  // Creates the path to send a request to
  std::string create_path(const std::string& node_for_replicas_param,
                          std::string cluster_view_id_param,
                          uint32_t time_from_param,
                          bool use_time_from_param);

  // Sends a get request
  virtual HTTPCode send_get(const std::string& server,
                            const std::string& path,
                            int max_timers,
                            std::string& response);

  // Resynchronises with a single Chronos node (used in resync operations).
  virtual HTTPCode resynchronise_with_single_node(
                            const std::string& server_to_sync,
                            std::vector<std::string> cluster_nodes,
                            std::string localhost);
};

#endif

