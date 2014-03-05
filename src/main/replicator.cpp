#include "replicator.h"
#include "globals.h"

#include <cstring>
#include <pthread.h>

Replicator::Replicator() : _q()
{
  int thread_rc = pthread_create(&_worker_thread,
                                 NULL, 
                                 Replicator::worker_thread_entry_point, 
                                 (void*)this);
  if (thread_rc != 0)
  {
    LOG_ERROR("Failed to start replicator thread: %s", strerror(thread_rc));
  }
}

Replicator::~Replicator()
{
  _q.terminate();
  pthread_join(_worker_thread, NULL);
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
}

void Replicator::run()
{
  CURL* new_handle = NULL;
  CURLM* multi_handle = curl_multi_init();
  int active_handles = 0;

  while(_q.pop(new_handle, 10))
  e
    if (new_handle != NULL)
    {
      LOG_DEBUG("Sending replication message");
      curl_multi_add_handle(multi_handle, new_handle);
      new_handle = NULL;
    }
    else
    {
      curl_multi_perform(multi_handle, &active_handles);
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
        CURL* old_handle = msg->easy_handle;

        // We're about to invalidate the data `msg` points to so NULL it now.
        msg = NULL;
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

  // Tell cURL to perform a POST but to call it a PUT, this allows
  // us to easily pass a JSON body as a string.
  //
  // http://curl.haxx.se/mail/lib-2009-11/0001.html
  curl_easy_setopt(curl, CURLOPT_POST, 1);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

  // Set up the content type (as POSTFIELDS doesn't) we'll free these in
  // the cURL thread after the request is completed.
  struct curl_slist* headers = NULL;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

  return curl;
}
