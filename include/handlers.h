/**
 * @file handlers.h
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef HANDLERS_H__
#define HANDLERS_H__

#include "httpstack.h"
#include "httpstack_utils.h"
#include "sas.h"
#include "httpconnection.h"
#include "timer.h"
#include "timer_handler.h"
#include "replicator.h"
#include "gr_replicator.h"
#include "globals.h"

class ControllerTask : public HttpStackUtils::Task
{
public:
  struct Config
  {
    Config(Replicator* replicator,
           GRReplicator* gr_replicator,
           TimerHandler* handler) :
      _replicator(replicator),
      _gr_replicator(gr_replicator),
      _handler(handler)
    {}
  
    ~Config()
    {}

    Replicator* _replicator;
    GRReplicator* _gr_replicator;
    TimerHandler* _handler;
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
  void add_or_update_timer(TimerID timer_id,
                           uint32_t replication_factor,
                           uint64_t replica_hash);
  void handle_get();
  void handle_delete();
  bool node_is_in_cluster(std::string requesting_node);

protected:
  const Config* _cfg;
};

#endif
