/**
 * @file replicator.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
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

#include "replicator.h"
#include "globals.h"

#include <cstring>
#include <pthread.h>

Replicator::Replicator(ExceptionHandler* exception_handler) :
  _q(),
  _headers(NULL),
  _exception_handler(exception_handler)
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
  for (int ii = 0; ii < REPLICATOR_THREAD_COUNT; ++ii)
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
// starvation.
void Replicator::worker_thread_entry_point()
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
    CW_TRY
    {
      // The customized bits of this request.
      curl_easy_setopt(curl, CURLOPT_URL, replication_request->url.c_str());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, replication_request->body.data());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, replication_request->body.length());

      // Send the request.
      CURLcode rc = curl_easy_perform(curl);
      if (rc == CURLE_HTTP_RETURNED_ERROR)
      {
        long http_rc;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_rc);
        LOG_WARNING("Failed to replicate timer to %s, HTTP error was %d %s",
                    replication_request->url.c_str(),
                    http_rc,
                    curl_easy_strerror(rc));
      }
    }
    CW_EXCEPT(_exception_handler)
    {
      // No recovery behaviour needed
    }
    CW_END

    // Clean up
    delete replication_request;
  }

  curl_easy_cleanup(curl);
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
