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

class ControllerTask : public HttpStackUtils::Task
{
public:
  struct Config
  {
    Config(Replicator* replicator,
           TimerHandler* handler) :
      _replicator(replicator),
      _handler(handler)
    {}
  
    ~Config()
    {}

    Replicator* _replicator;
    TimerHandler* _handler;
  };

  ControllerTask(HttpStack::Request& req,
                 const Config* cfg,
                 SAS::TrailId trail = 0) :
    HttpStackUtils::Task(req, trail),
    _cfg(cfg),
    _replica_hash(0)
  {};

  ~ControllerTask()
  {}

  void run();
  HTTPCode parse_request();

protected:
  const Config* _cfg;
  TimerID _timer_id;
  uint64_t _replica_hash;
};

#endif
