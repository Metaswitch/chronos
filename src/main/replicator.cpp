#include "replicator.h"
#include "globals.h"

#include <cstring>
#include <pthread.h>

Replicator::Replicator() : _q(), _headers(NULL)
{
  // Create a pool of replicator threads
  for (int ii = 0; ii < REPLICATOR_THREAD_COUNT; ii++)
  {
    pthread_t thread;
    int thread_rc = pthread_create(&thread,
                                   NULL,
                                   Replicator::worker_thread_entry_point,
                                   (void*)this);
    if (thread_rc != 0)
    {
      LOG_ERROR("Failed to start replicator thread: %s", strerror(thread_rc));
    }

    _worker_threads[ii] = thread;
  }

  // Set up a content type header descriptor to use for our requests.
  _headers = curl_slist_append(_headers, "Content-Type: application/json");
}

Replicator::~Replicator()
{
  _q.terminate();
  for (int ii = 0; ii < REPLICATOR_THREAD_COUNT; ii++)
  {
    pthread_join(_worker_threads[ii], NULL);
  }
  curl_slist_free_all(_headers);
}

/*****************************************************************************/
/* Static functions.                                                         */
/*****************************************************************************/

void* Replicator::worker_thread_entry_point(void* arg)
{
  Replicator* rep = (Replicator*)arg;
  rep->run();
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

  std::string body = timer->to_json();

  for (auto it = timer->replicas.begin(); it != timer->replicas.end(); it++)
  {
    if (*it == localhost)
    {
      continue;
    }

    ReplicationRequest* replication_request = new ReplicationRequest();
    replication_request->url = timer->url(*it);
    replication_request->body = body;
    _q.push(replication_request);
  }

  for (auto it = timer->extra_replicas.begin(); it != timer->extra_replicas.end(); it++)
  {
    if (*it == localhost)
    {
      continue;
    }

    std::string url = timer->url(*it);
    std::string body = timer->to_json();

    ReplicationRequest* replication_request = new ReplicationRequest();
    replication_request->url = timer->url(*it);
    replication_request->body = body;
    _q.push(replication_request);
  }
}

// The replication worker thread.  This loops, receiving cURL handles off a queue
// and handling them synchronously.  We run a pool of these threads to mitigate
// starvation.
void Replicator::run()
{
  CURL* curl = curl_easy_init();

  // Tell cURL to perform a POST but to call it a PUT, this allows
  // us to easily pass a JSON body as a string.
  //
  // http://curl.haxx.se/mail/lib-2009-11/0001.html
  curl_easy_setopt(curl, CURLOPT_POST, 1);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

  // Set up the content type (as POSTFIELDS doesn't)
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, _headers);

  ReplicationRequest* replication_request;
  while(_q.pop(replication_request))
  {
    // The customized bits of this request.
    curl_easy_setopt(curl, CURLOPT_URL, replication_request->url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, replication_request->body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, replication_request->body.length());

    // Send the request.
    curl_easy_perform(curl);

    // Clean up
    delete replication_request;
  }

  // Received terminate signal, shut down.
  pthread_exit(NULL);
}
