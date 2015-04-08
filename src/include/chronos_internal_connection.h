#ifndef CHRONOSINTERNALCONNECTION_H__
#define CHRONOSINTERNALCONNECTION_H__

#include "httpconnection.h"
#include "timer.h"
#include "timer_handler.h"
#include "replicator.h"
#include "updater.h"
#include "statistic.h"
#include "counter.h"

/// @class ChronosInternalConnection
class ChronosInternalConnection
{
public:
  ChronosInternalConnection(HttpResolver* resolver,
                            TimerHandler* handler, 
                            Replicator* replicator,
                            LastValueCache* lvc,
                            Alarm* alarm);
  virtual ~ChronosInternalConnection();

  // Performs a scale-up/down operation by resynchronisind
  // the timers on this node with all the other Chronos nodes
  virtual void scale_operation();

private:
  HttpConnection* _http;
  TimerHandler* _handler;
  Replicator* _replicator;
  Alarm* _alarm;
  Updater<void, ChronosInternalConnection>* _updater;
  Counter* _timers_processed_stat; 
  Statistic* _nodes_to_query_stat;

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
  virtual HTTPCode send_delete(const std::string server,
                               const std::string body);

  // Sends a get request
  virtual HTTPCode send_get(const std::string server,
                            const std::string requesting_node,
                            const std::string sync_mode,
                            std::string cluster_view_id,
                            int max_timers,
                            std::string& response);

  // Resynchronises with a single Chronos node (used in scale
  // operations). 
  virtual HTTPCode resynchronise_with_single_node(
                            const std::string server_to_sync, 
                            std::vector<std::string> cluster_nodes,
                            std::string localhost);
};

#endif

