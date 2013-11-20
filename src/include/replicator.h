#ifndef REPLICATOR_H__
#define REPLICATOR_H__

#include <curl/curl.h>

#include "timer.h"

// This class is used to replicate timers to the specified replicas, using cURL
// to handle the HTTP construcion and sending.
class Replicator
{
public:
  Replicator();
  virtual ~Replicator();

  virtual void replicate(Timer*);

  static void cleanup_curl(CURLM* curl);
  static size_t string_write(void*, size_t, size_t, void*);

private:
  CURLM* get_curl_handle();

  pthread_key_t _thread_local_key;
};

#endif
