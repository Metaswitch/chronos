#include "replicator.h"
#include "globals.h"

#include <cstring>

Replicator::Replicator()
{
  _curl = curl_easy_init();

  // Tell cURL to perform a POST but to call it a PUT, this allows
  // us to easily pass a JSON body as a string.
  //
  // http://curl.haxx.se/mail/lib-2009-11/0001.html
  curl_easy_setopt(_curl, CURLOPT_POST, 1);
  curl_easy_setopt(_curl, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt(_curl, CURLOPT_VERBOSE, 1);
}

Replicator::~Replicator()
{
  curl_easy_cleanup(_curl);
}

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
    LOG_DEBUG("Replicating to : %s", url.c_str());
    // TODO This should use cURL's multi-mode to parallelize requests.
    std::string body = timer->to_json();

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(_curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_perform(_curl);
    curl_slist_free_all(headers);
  }
}
