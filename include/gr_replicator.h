/**
 * @file gr_replicator.h
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
  GRReplicationRequest(ChronosGRConnection* connection,
                       std::string url,
                       std::string body) :
    _connection(connection),
    _url(url),
    _body(body)
  {
  }

  ChronosGRConnection* _connection;
  std::string _url;
  std::string _body;
};

/// @class GRReplicator
///
/// Responsible for creating replication requests to send between sites, and
/// queuing these requests.
class GRReplicator
{
public:
  GRReplicator(HttpResolver* http_resolver,
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
