#ifndef HTTP_CALLBACK_H__
#define HTTP_CALLBACK_H__

#include "callback.h"
#include "eventq.h"
#include "timer_handler.h"
#include "replicator.h"
#include "timer.h"
#include "alarm.h"

#include <string>
#include <curl/curl.h>

#define HTTPCALLBACK_THREAD_COUNT 50

class HTTPCallback : public Callback
{
public:
  HTTPCallback(Replicator*,
               AlarmPair* timer_pop_alarms);
  ~HTTPCallback();

  void start(TimerHandler*);
  void stop();

  std::string protocol() { return "http"; };
  void perform(Timer*);

  static void* worker_thread_entry_point(void*);
  void worker_thread_entry_point();

private:
  pthread_t _worker_threads[HTTPCALLBACK_THREAD_COUNT];
  eventq<Timer*> _q;

  bool _running;
  TimerHandler* _handler;
  Replicator* _replicator;

  AlarmPair* _timer_pop_alarms;
};

#endif
