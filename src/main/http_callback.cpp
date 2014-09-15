#include "http_callback.h"
#include "log.h"

#include <cstring>


AlarmPair HTTPCallback::_timer_pop_alarms("chronos", "CHRONOS_TIMER_POP_ERROR_CLEAR",
                                                     "CHRONOS_TIMER_POP_ERROR_MAJOR");


HTTPCallback::HTTPCallback(Replicator* replicator) : _q(),
                                                     _running(false),
                                                     _replicator(replicator)
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
      LOG_ERROR("Failed to start callback worker thread: %s", strerror(thread_rc));
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
      // Check if the next pop occurs before the repeat-for interval and,
      // if not, convert to a tombstone to indicate the timer is dead.
      if ((timer->sequence_number + 1) * timer->interval > timer->repeat_for)
      {
        timer->become_tombstone();
      }
      _replicator->replicate(timer);
      _handler->add_timer(timer);
      timer = NULL; // We relinquish control of the timer when we give
                    // it to the store.
      _timer_pop_alarms.clear();
    }
    else
    {
      if (curl_rc == CURLE_HTTP_RETURNED_ERROR)
      {
        long http_rc = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_rc);
        LOG_WARNING("Got HTTP error %d from %s", http_rc, timer->callback_url.c_str());
      }

      LOG_WARNING("Failed to process callback for %lu: URL %s, curl error was: %s", timer->id,
                  timer->callback_url.c_str(),
                  curl_easy_strerror(curl_rc));

      if (timer->is_last_replica())
      {
        _timer_pop_alarms.set();
      }

      delete timer;
    }

    // Tidy up request-speciifc objects
    curl_slist_free_all(headers);
    headers = NULL;
  }

  // Tidy up thread-specific objects
  curl_easy_cleanup(curl);

  return;
}
