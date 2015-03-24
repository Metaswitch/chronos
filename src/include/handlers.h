#ifndef HANDLERS_H__
#define HANDLERS_H__

#include "httpstack.h"
#include "httpstack_utils.h"
#include "sas.h"
#include "httpconnection.h"
#include "timer.h"
#include "timer_handler.h"
#include "replicator.h"
#include "globals.h"
#include "chronosconnection.h"

class ControllerTask : public HttpStackUtils::Task
{
public:
  struct Config
  {
    Config(Replicator* replicator,
           TimerHandler* handler,
           TimerStore* store) :
      _replicator(replicator),
      _handler(handler),
      _store(store)
    {}
  
    ~Config()
    {}

    Replicator* _replicator;
    TimerHandler* _handler;
    TimerStore* _store;
  };

  ControllerTask(HttpStack::Request& req,
                 const Config* cfg,
                 SAS::TrailId trail = 0) :
    HttpStackUtils::Task(req, trail),
    _cfg(cfg)
  {};

  ~ControllerTask()
  {}

  void run();
  HTTPCode parse_request();
  void add_or_update_timer(int timer_id, int replica_hash);
  void handle_get();
  void handle_delete();
  bool node_is_in_cluster(std::string requesting_node);

protected:
  const Config* _cfg;
};

#endif
