/**
 * @file handlers.h
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
    _cfg(cfg)
  {};

  ~ControllerTask()
  {}

  void run();
  HTTPCode parse_request();
  void add_or_update_timer(TimerID timer_id, int replica_hash);
  void handle_get();
  void handle_delete();
  bool node_is_in_cluster(std::string requesting_node);

protected:
  const Config* _cfg;
};

#endif
