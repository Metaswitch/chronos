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

private:
  CURL* _curl;
};

#endif
