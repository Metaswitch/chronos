/**
 * @file gr_replicator.h
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2016  Metaswitch Networks Ltd
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

#ifndef GR_REPLICATOR_H__
#define GR_REPLICATOR_H__

#include "timer.h"
#include "chronos_gr_connection.h"
#include "exception_handler.h"
#include "eventq.h"

#define GR_REPLICATOR_THREAD_COUNT 20

struct GRReplicationRequest
{
  ChronosGRConnection* connection;
  std::string url;
  std::string body;
};

/// @class GRReplicator
///
/// Responsible for creating replication requests to send between sites, and
/// queuing these requests.
class GRReplicator
{
public:
  GRReplicator(std::vector<ChronosGRConnection*> connections,
               ExceptionHandler* exception_handler);
  virtual ~GRReplicator();

  void worker_thread_entry_point();
  virtual void replicate(Timer*);
  static void* worker_thread_entry_point(void*);

private:
  eventq<GRReplicationRequest *> _q;
  pthread_t _worker_threads[GR_REPLICATOR_THREAD_COUNT];
  std::vector<ChronosGRConnection*> _connections;
  ExceptionHandler* _exception_handler;
};

#endif
