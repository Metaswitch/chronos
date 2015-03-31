#ifndef CHRONOSINTERNALCONNECTION_H__
#define CHRONOSINTERNALCONNECTION_H__

#include "httpconnection.h"
#include "timer.h"
#include "timer_handler.h"
#include "replicator.h"

/// @class ChronosInternalConnection
class ChronosInternalConnection
{
public:
  ChronosInternalConnection(HttpResolver* resolver,
                            TimerHandler* handler, 
                            Replicator* replicator);
  virtual ~ChronosInternalConnection();

  virtual HTTPCode trigger_move_for_one_server(const std::string server);

private:
  HttpConnection* _http;
  TimerHandler* _handler;
  Replicator* _replicator;

  std::string create_delete_body(std::map<TimerID, int> delete_map);
  bool get_replica_presence(std::string current_node,
                            std::vector<std::string> replicas);
  bool get_replica_level(int& index,
                         std::string current_node,
                         std::vector<std::string> replicas);
  virtual HTTPCode send_delete(const std::string server,
                               const std::string body);
  virtual HTTPCode send_get(const std::string server,
                            const std::string requesting_node,
                            const std::string sync_mode,
                            std::string cluster_id,
                            int max_timers,
                            std::string& response);
};

#endif

