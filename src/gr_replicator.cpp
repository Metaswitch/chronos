/**
 * @file gr_replicator.cpp
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

#include "gr_replicator.h"
#include "globals.h"
#include <pthread.h>

GRReplicator::GRReplicator(std::vector<ChronosGRConnection*> connections,
                           ExceptionHandler* exception_handler) :
  _q(),
  _connections(connections),
  _exception_handler(exception_handler)
{
  // Create a pool of replicator threads
  for (int ii = 0; ii < GR_REPLICATOR_THREAD_COUNT; ++ii)
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

  for (int ii = 0; ii < GR_REPLICATOR_THREAD_COUNT; ++ii)
  {
    pthread_join(_worker_threads[ii], NULL);
  }
}

void* GRReplicator::worker_thread_entry_point(void* arg)
{
  GRReplicator* rep = static_cast<GRReplicator*>(arg);
  rep->worker_thread_entry_point();
  return NULL;
}

// Handle the replication of the given timer to its replicas.
void GRReplicator::replicate(Timer* timer)
{
  // Create the JSON body - strip out any replica information
  Timer* timer_copy = new Timer(*timer);
  uint32_t repl_factor = timer_copy->get_replication_factor();
  timer_copy->replicas.clear();
  std::string body = timer_copy->to_json();

  for (ChronosGRConnection* conn : _connections)
  {
    GRReplicationRequest* replication_request = new GRReplicationRequest();
    replication_request->connection = conn;
    replication_request->body = body;
    replication_request->id = std::to_string(timer->id);
    replication_request->replication_factor = std::to_string(repl_factor);
    _q.push(replication_request);
  }

  delete timer_copy; timer_copy = NULL;
}

// The replication worker thread.  This loops, receiving cURL handles off a queue
// and handling them synchronously.  We run a pool of these threads to mitigate
// starvation.
void GRReplicator::worker_thread_entry_point()
{
  GRReplicationRequest* replication_request;

  while(_q.pop(replication_request))
  {
    CW_TRY
    {
      replication_request->connection->send_put(
                                   replication_request->id,
                                   replication_request->body,
                                   replication_request->replication_factor);
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
