#ifndef REPLICATOR_H__
#define REPLICATOR_H__

#include <curl/curl.h>

#include "timer.h"
#include "eventq.h"

#define REPLICATOR_THREAD_COUNT 50

struct ReplicationRequest
{
  std::string url;
  std::string body;
};

// This class is used to replicate timers to the specified replicas, using cURL
// to handle the HTTP construction and sending.
class Replicator
{
public:
  Replicator();
  virtual ~Replicator();

  void worker_thread_entry_point();
  virtual void replicate(Timer*);

  static void* worker_thread_entry_point(void*);

private:
  void replicate_int(const std::string&, const std::string&);
  eventq<ReplicationRequest *> _q;
  pthread_t _worker_threads[REPLICATOR_THREAD_COUNT];
  struct curl_slist* _headers;
};

#endif
