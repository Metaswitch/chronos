/**
 * @file gr_replicator.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "gr_replicator.h"
#include "globals.h"
#include <pthread.h>

GRReplicator::GRReplicator(HttpResolver* http_resolver,
                           ExceptionHandler* exception_handler,
                           int gr_threads,
                           BaseCommunicationMonitor* comm_monitor) :
  _q(),
  _exception_handler(exception_handler),
  _gr_threads(gr_threads)
{
  std::vector<std::string> remote_site_dns_records;
  __globals->get_remote_site_dns_records(remote_site_dns_records);

  for (std::string site: remote_site_dns_records)
  {
    ChronosGRConnection* conn = new ChronosGRConnection(site,
                                                        http_resolver,
                                                        comm_monitor);
    _connections.push_back(conn);
  }

  // Create a pool of replicator threads
  _worker_threads.resize(_gr_threads);
  for (int ii = 0; ii < _gr_threads; ++ii)
  {
    pthread_t thread;
    int thread_rc = pthread_create(&thread,
                                   NULL,
                                   GRReplicator::worker_thread_entry_point,
                                   (void*)this);
    if (thread_rc != 0)
    {
      // LCOV_EXCL_START
      TRC_ERROR("Failed to start replicator thread: %s", strerror(thread_rc));
      // LCOV_EXCL_STOP
    }

    _worker_threads[ii] = thread;
  }
}

GRReplicator::~GRReplicator()
{
  _q.terminate();

  for (int ii = 0; ii < _gr_threads; ++ii)
  {
    pthread_join(_worker_threads[ii], NULL);
  }

  for (ChronosGRConnection* conn: _connections)
  {
    delete conn;
  }
}

void* GRReplicator::worker_thread_entry_point(void* arg)
{
  GRReplicator* rep = static_cast<GRReplicator*>(arg);
  rep->worker_thread_entry_point();
  return NULL;
}

// Handle the replication of the timer to other sites
void GRReplicator::replicate(Timer* timer)
{
  // Create the JSON body - strip out any replica information
  Timer timer_copy(*timer);
  std::string url = timer_copy.url();
  timer_copy.replicas.clear();
  std::string body = timer_copy.to_json();

  for (ChronosGRConnection* conn : _connections)
  {
    GRReplicationRequest* replication_request =
                                      new GRReplicationRequest(conn, url, body);
    _q.push(replication_request);
  }
}

void GRReplicator::worker_thread_entry_point()
{
  GRReplicationRequest* replication_request = NULL;

  while(_q.pop(replication_request))
  {
    CW_TRY
    {
      replication_request->_connection->send_put(replication_request->_url,
                                                 replication_request->_body);
    }
    // LCOV_EXCL_START - No exception testing in UT
    CW_EXCEPT(_exception_handler)
    {
      // No recovery behaviour needed
    }
    CW_END
    // LCOV_EXCL_STOP

    // Clean up
    delete replication_request;
  }
}
