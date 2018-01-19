/**
 * @file http_callback.cpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "http_callback.h"
#include "log.h"

#include <cstring>

HTTPCallback::HTTPCallback(HttpResolver* resolver,
                           ExceptionHandler* exception_handler) :

  _q(),
  _exception_handler(exception_handler),
  _resolver(resolver),
  _running(false),
  _handler(NULL),
  _http_client(false,
               _resolver,
               SASEvent::HttpLogLevel::NONE,
               NULL)
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
      // LCOV_EXCL_START
      TRC_ERROR("Failed to start callback worker thread: %s", strerror(thread_rc));
      // LCOV_EXCL_STOP
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
  HTTPCallback* callback = static_cast<HTTPCallback*>(arg);
  callback->worker_thread_entry_point();
  return NULL;
}

void HTTPCallback::worker_thread_entry_point()
{
  Timer* timer = NULL;
  while (_q.pop(timer))
  {
    CW_TRY
    {
      // Pull out the timer details for use in the CURL request.
      TimerID timer_id = timer->id;
      std::string callback_url = timer->callback_url;
      std::string callback_body = timer->callback_body;

      // Set up the headers.
      std::string seq_no_hdr = "X-Sequence-Number: " + std::to_string(timer->sequence_number);
      std::string content_type_hdr = "Content-Type: application/octet-stream";

      // Return the timer to the store. This avoids the error case where the client
      // attempts to update the timer based on the pop, finds nothing in the store,
      // inserts a new timer rather than updating the timer that popped, and the popped
      // timer then tombstoning and overwriting the newer timer, leading to leaked statistics.
      _handler->return_timer(timer);
      timer = NULL; // We relinquish control of the timer when we give it back to the store.

      // Send the request.
      std::string server;
      std::string scheme;
      std::string path;
      bool valid_url = Utils::parse_http_url(callback_url, scheme, server, path);

      if (valid_url)
      {
        std::unique_ptr<HttpRequest> req(new HttpRequest(server,
                                                         scheme,
                                                         &_http_client,
                                                         HttpClient::RequestType::POST,
                                                         path));
        req->set_req_body(callback_body);
        req->add_req_header(seq_no_hdr);
        req->add_req_header(content_type_hdr);
        HttpResponse resp = req->send();
        HTTPCode http_rc = resp.get_return_code();

        if (http_rc == HTTP_OK)
        {
          // The callback succeeded, so we need to re-find the timer, and replicate it.
          TRC_DEBUG("Callback for timer \"%lu\" was successful", timer_id);
          _handler->handle_successful_callback(timer_id);
        }
        else
        {
          TRC_DEBUG("Failed to process callback for %lu: URL %s, HTTP rc %ld", timer_id,
                    callback_url.c_str(), http_rc);

          // The callback failed, and so we need to remove the timer from the store.
          _handler->handle_failed_callback(timer_id);
        }
      }
      //LCOV_EXCL_START
      else
      {
        TRC_ERROR("Invalid callback url: %s", callback_url.c_str());
        _handler->handle_failed_callback(timer_id);
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
  }

  return;
}
