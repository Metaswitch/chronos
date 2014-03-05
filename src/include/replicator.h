#ifndef REPLICATOR_H__
#define REPLICATOR_H__

#include <curl/curl.h>

#include "timer.h"
#include "eventq.h"

// This class is used to replicate timers to the specified replicas, using cURL
// to handle the HTTP construction and sending.
class Replicator
{
public:
  Replicator();
  virtual ~Replicator();

  void run();
  virtual void replicate(Timer*);

  static void* worker_thread_entry_point(void*);

private:
  CURL* create_curl_handle(const std::string& url,
                           const std::string& body);

  eventq<CURL*> _q;
  pthread_t _worker_thread;
};

#endif
