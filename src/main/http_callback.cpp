/**
 * @file http_callback.cpp
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

#include "http_callback.h"
#include "log.h"

#include <cstring>

HTTPCallback::HTTPCallback() :
  _q(),
  _running(false)
{
}

HTTPCallback::~HTTPCallback()
{
  if (_running)
  {
    stop();
  }
}

void HTTPCallback::start(TimerHandler* handler)
{
  _handler = handler;
  _running = true;

  // Create a pool of worker threads
  for (int ii = 0; ii < HTTPCALLBACK_THREAD_COUNT; ++ii)
  {
    pthread_t thread;
    int thread_rc = pthread_create(&thread,
                                   NULL,
                                   HTTPCallback::worker_thread_entry_point,
                                   (void*)this);
    if (thread_rc != 0)
    {
      TRC_ERROR("Failed to start callback worker thread: %s", strerror(thread_rc));
    }

    _worker_threads[ii] = thread;
  }
}

void HTTPCallback::stop()
{
  _q.terminate();
  for (int ii = 0; ii < HTTPCALLBACK_THREAD_COUNT; ++ii)
  {
    pthread_join(_worker_threads[ii], NULL);
  }
  _running = false;
}

void HTTPCallback::perform(Timer* timer)
{
  _q.push(timer);
}

void* HTTPCallback::worker_thread_entry_point(void* arg)
{
  HTTPCallback* callback = (HTTPCallback*)arg;
  callback->worker_thread_entry_point();
  return NULL;
}

void HTTPCallback::worker_thread_entry_point()
{
  CURL* curl = curl_easy_init();
  curl_easy_setopt(curl, CURLOPT_POST, 1);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

  Timer* timer;
  while (_q.pop(timer))
  {
    // Set up the request details.
    curl_easy_setopt(curl, CURLOPT_URL, timer->callback_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, timer->callback_body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, timer->callback_body.length());

    // Include the sequence number header.
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, (std::string("X-Sequence-Number: ") +
                                          std::to_string(timer->sequence_number)).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Send the request
    CURLcode curl_rc = curl_easy_perform(curl);
    if (curl_rc == CURLE_OK)
    {
      _handler->return_timer(timer, true);
      timer = NULL; // We relinquish control of the timer when we give it back to the store.
    }
    else
    {
      if (curl_rc == CURLE_HTTP_RETURNED_ERROR)
      {
        long http_rc = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_rc);
        TRC_WARNING("Got HTTP error %d from %s", http_rc, timer->callback_url.c_str());
      }

      TRC_WARNING("Failed to process callback for %lu: URL %s, curl error was: %s", timer->id,
                  timer->callback_url.c_str(),
                  curl_easy_strerror(curl_rc));

      _handler->return_timer(timer, false);
      timer = NULL; // We relinquish control of the timer when we give it back to the store.
    }

    // Tidy up request-speciifc objects
    curl_slist_free_all(headers);
    headers = NULL;
  }

  // Tidy up thread-specific objects
  curl_easy_cleanup(curl);

  return;
}
