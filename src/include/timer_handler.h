#ifndef TIMER_HANDLER_H__
#define TIMER_HANDLER_H__

#include <pthread.h>

#include "timer_store.h"
#include "replicator.h"
#include "callback.h"

class TimerHandler
{
public:
  TimerHandler(TimerStore*, Replicator*, Callback*);
  ~TimerHandler();
  void signal_new_timer(unsigned int);
  void run();

private:
  void pop(std::unordered_set<Timer*>&);
  void pop(Timer*);

  TimerStore* _store;
  Replicator* _replicator;
  Callback* _callback;

  pthread_t _handler_thread;
  volatile bool _terminate;
  volatile unsigned int _nearest_new_timer;
  pthread_mutex_t _mutex;
  pthread_cond_t _cond_var;

  static void* timer_handler_entry_func(void *);
};

#endif
