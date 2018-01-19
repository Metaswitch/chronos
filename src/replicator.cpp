/**
 * @file replicator.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "replicator.h"
#include "globals.h"

#include <cstring>
#include <pthread.h>

Replicator::Replicator(HttpResolver* resolver,
                       ExceptionHandler* exception_handler) :
  _q(),
  _exception_handler(exception_handler),
  _resolver(resolver),
  _http_client(false,
               _resolver,
               SASEvent::HttpLogLevel::NONE,
               NULL)
{
  // Create a pool of replicator threads
  for (int ii = 0; ii < REPLICATOR_THREAD_COUNT; ++ii)
  {
    pthread_t thread;
    int thread_rc = pthread_create(&thread,
                                   NULL,
                                   Replicator::worker_thread_entry_point,
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

Replicator::~Replicator()
{
  _q.terminate();
  for (int ii = 0; ii < REPLICATOR_THREAD_COUNT; ++ii)
  {
    pthread_join(_worker_threads[ii], NULL);
  }
}

/*****************************************************************************/
/* Static functions.                                                         */
/*****************************************************************************/

void* Replicator::worker_thread_entry_point(void* arg)
{
  Replicator* rep = static_cast<Replicator*>(arg);
  rep->worker_thread_entry_point();
  return NULL;
}

/*****************************************************************************/
/* Public API functions.                                                     */
/*****************************************************************************/

// Handle the replication of the given timer to its replicas.
void Replicator::replicate(Timer* timer)
{
  std::string localhost;
  __globals->get_cluster_local_ip(localhost);

  // Only create the body once (as it's the same for each replica).
  std::string body = timer->to_json();

  for (std::vector<std::string>::iterator it = timer->replicas.begin();
                                          it != timer->replicas.end();
                                          ++it)
  {
    if (*it != localhost)
    {
      replicate_int(body, timer->url(*it));
    }
  }

  for (std::vector<std::string>::iterator it = timer->extra_replicas.begin();
                                          it != timer->extra_replicas.end();
                                          ++it)
  {
    if (*it != localhost)
    {
      replicate_int(body, timer->url(*it));
    }
  }
}

// Handle the replication of the given timer to a single node
void Replicator::replicate_timer_to_node(Timer* timer,
                                         std::string node)
{
  std::string body = timer->to_json();
  replicate_int(body, timer->url(node));
}

// The replication worker thread.  This loops, receiving cURL handles off a queue
// and handling them synchronously.  We run a pool of these threads to mitigate
// starvation
void Replicator::worker_thread_entry_point()
{
  ReplicationRequest* replication_request;
  while(_q.pop(replication_request))
  {
    CW_TRY
    {
      std::string replication_url = replication_request->url.c_str();
      std::string replication_body = replication_request->body.data();

      std::string server;
      std::string scheme;
      std::string path;
      bool valid_url = Utils::parse_http_url(replication_url, scheme, server, path);

      if (valid_url)
      {
        std::unique_ptr<HttpRequest> req(new HttpRequest(server,
                                                        scheme,
                                                        &_http_client,
                                                        HttpClient::RequestType::PUT,
                                                        path));
        req->set_req_body(replication_body);
        HttpResponse resp = req->send();
        HTTPCode http_rc = resp.get_return_code();

        if (http_rc != HTTP_OK)
        {
          TRC_DEBUG("Failed to process replication for %s. HTTP rc %ld",
                    replication_url.c_str(),
                    http_rc);
        }
      }
      //LCOV_EXCL_START
      else
      {
        TRC_DEBUG("Invalid URL for replication: %s", replication_url.c_str());
      }
      // LCOV_EXCL_STOP
    }
    //LCOV_EXCL_START - No exception testing in UT
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

/*****************************************************************************/
/* Private functions.                                                        */
/*****************************************************************************/

void Replicator::replicate_int(const std::string& body, const std::string& url)
{
  ReplicationRequest* replication_request = new ReplicationRequest();
  replication_request->url = url;
  replication_request->body = body;
  _q.push(replication_request);
}
