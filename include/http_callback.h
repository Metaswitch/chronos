/**
 * @file http_callback.h
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef HTTP_CALLBACK_H__
#define HTTP_CALLBACK_H__

#include "callback.h"
#include "eventq.h"
#include "timer_handler.h"
#include "timer.h"
#include "httpresolver.h"
#include "httpconnection.h"

#include <string>
#include <curl/curl.h>

#define HTTPCALLBACK_THREAD_COUNT 50

class HTTPCallback : public Callback
{
public:
  HTTPCallback(HttpResolver* resolver);
  ~HTTPCallback();

  void start(TimerHandler*);
  void stop();

  std::string protocol() { return "http"; };
  void perform(Timer*);

  static void* worker_thread_entry_point(void*);
  void worker_thread_entry_point();

private:
  // Resolver to use to resolve callback URL server FQDNs to IP addresses.
  HttpResolver* _resolver;

  pthread_t _worker_threads[HTTPCALLBACK_THREAD_COUNT];
  eventq<Timer*> _q;

  bool _running;
  TimerHandler* _handler;

  HttpClient _http_client;
};

#endif
