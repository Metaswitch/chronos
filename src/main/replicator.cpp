#include "replicator.h"
#include "globals.h"

#include <cstring>
#include <pthread.h>

Replicator::Replicator() : _q(), _headers(NULL)
{
  int thread_rc = pthread_create(&_worker_thread,
                                 NULL,
                                 Replicator::worker_thread_entry_point,
                                 (void*)this);
  if (thread_rc != 0)
  {
    LOG_ERROR("Failed to start replicator thread: %s", strerror(thread_rc));
  }

  // Set up a content type header descriptor to use for our requests.
  _headers = curl_slist_append(_headers, "Content-Type: application/json");
}

Replicator::~Replicator()
{
  _q.terminate();
  pthread_join(_worker_thread, NULL);
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

  for (auto it = timer->replicas.begin(); it != timer->replicas.end(); it++)
  {
    if (*it == localhost)
    {
      continue;
    }

    std::string url = timer->url(*it);
    std::string body = timer->to_json();

    CURL* curl = create_curl_handle(url, body);
    _q.push(curl);
  }

  for (auto it = timer->extra_replicas.begin(); it != timer->extra_replicas.end(); it++)
  {
    if (*it == localhost)
    {
      continue;
    }

    std::string url = timer->url(*it);
    std::string body = timer->to_json();

    CURL* curl = create_curl_handle(url, body);
    _q.push(curl);
  }
}

// The replication worker thread.  This loops, receiving cURL handles off a queue
// and managing them in parallel.
void Replicator::run()
{
  CURL* new_handle = NULL;
  CURLM* multi_handle = curl_multi_init();
  int active_handles = 0;

  while(_q.pop(new_handle, 10))
  {
    if (new_handle != NULL)
    {
      LOG_DEBUG("Sending replication message");
      curl_multi_add_handle(multi_handle, new_handle);

      // Since we added a handle to the multi handle, expect there to
      // be an extra handle in the count.
      active_handles++;
      new_handle = NULL;
    }

    // Check for progress on any of our replication messages.  Compare
    // active_handles on either side of this call to see if some messages
    // are done.
    int old_active_handles = active_handles;
    curl_multi_perform(multi_handle, &active_handles);

    if (old_active_handles != active_handles)
    {
      int outstanding_messages = 0;
      for(CURLMsg* msg = curl_multi_info_read(multi_handle, &outstanding_messages);
          msg != NULL;
          msg = curl_multi_info_read(multi_handle, &outstanding_messages))
      {
        // Found a completed request, log and cleanup.
        if (msg->data.result != CURLE_OK)
        {
          LOG_ERROR("Replication failed: %s", curl_easy_strerror(msg->data.result));
        }
        else
        {
          LOG_DEBUG("Replication successful");
        }

        // We're about to invalidate the data `msg` points to so remember the
        // important bit and NULL `msg` now.
        CURL* old_handle = msg->easy_handle;
        msg = NULL;

        std::string const * body_copy = NULL;
        curl_easy_getinfo(old_handle, CURLINFO_PRIVATE, body_copy);
        delete body_copy;

        curl_multi_remove_handle(multi_handle, old_handle);
        curl_easy_cleanup(old_handle);
      }
    }
  }

  // Received terminate signal, shut down.
  curl_multi_cleanup(multi_handle);
  pthread_exit(NULL);
}

/*****************************************************************************/
/* Private functions.                                                        */
/*****************************************************************************/

CURL* Replicator::create_curl_handle(const std::string& url,
                                     const std::string& body)
{
  CURL* curl = curl_easy_init();
  std::string const * body_copy = new std::string(body.c_str());

  // Tell cURL to perform a POST but to call it a PUT, this allows
  // us to easily pass a JSON body as a string.
  //
  // http://curl.haxx.se/mail/lib-2009-11/0001.html
  curl_easy_setopt(curl, CURLOPT_POST, 1);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

  // Set up the content type (as POSTFIELDS doesn't)
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, _headers);

  // The customized bits of this request.
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_copy->data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.length());

  // Save off the body. This is needed as otherwise the body string can be
  // destroyed before the curl message has been sent, meaning the message
  // is invalid.
  curl_easy_setopt(curl, CURLOPT_PRIVATE, body_copy);

  return curl;
}
