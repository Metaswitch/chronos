#include "replicator.h"

#include <cstring>

Replicator::Replicator()
{
  pthread_key_create(&_thread_local_key, cleanup_curl);
}

Replicator::~Replicator()
{
  CURLM* curl = pthread_getspecific(_thread_local_key);
  if (curl)
  {
    pthread_setspecific(_thread_local_key, NULL);
    cleanup_curl(curl);
  }
}

// Handle the replication of the given timer to its replicas.
void Replicator::replicate(Timer* timer)
{
  CURL* curl = get_curl_handle();

  for (auto it = timer->replicas.begin(); it != timer->replicas.end(); it++)
  {
    // TODO This should use cURL's multi-mode to parallelize requests.
    
    // Need to make copy of the body since sending is destructive.
    std::string body = timer->to_json();
    curl_easy_setopt(curl, CURLOPT_URL, (timer->url(*it)).c_str());
    curl_easy_setopt(curl, CURLOPT_READDATA, &body);
    curl_easy_perform(curl);
  }
}

/*****************************************************************************/
/* PRIVATE FUNCTIONS                                                         */
/*****************************************************************************/

CURL* Replicator::get_curl_handle()
{
  CURL* curl = pthread_getspecific(_thread_local_key);
  if (!curl)
  {
    curl = curl_multi_init();
    pthread_setspecific(_thread_local_key, curl);

    // Request body will always be written out from a string.
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, &string_write);

    // All requests are PUTs (since we always know the timer ID and replica list.
    curl_easy_setopt(curl, CURLOPT_PUT, 1);
  }
  return curl;
}

// Simple cleanup routine for thread_local_data.
void Replicator::cleanup_curl(CURL* curl)
{
  curl_multi_cleanup(curl);
}

// cURL utility callback for using a std::stream as the body for a request.
//
// WARNING this callback modifies `stream`.
size_t Replicator::string_write(void* ptr, size_t size, size_t nmemb, void* stream)
{
  std::string* str = (std::string*)stream;

  // Must return 0 when we're done.
  if (str->empty())
  {
    return 0;
  }

  // Otherwise copy in an much as possible and return the length compied.
  strncpy((char *)ptr,
          str->c_str(),
          size * nmemb);
  if (str->length() < size *nmemb)
  {
    return str->length();
  }
  else
  {
    // Didn't copy the whole string, trim the bit we did write off the front.
    *str = str->substr(0, size * nmemb);
    return size * nmemb;
  }
}
