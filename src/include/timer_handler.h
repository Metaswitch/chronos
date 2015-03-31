#ifndef TIMER_HANDLER_H__
#define TIMER_HANDLER_H__

#include <pthread.h>

#ifdef UNITTEST
#include "pthread_cond_var_helper.h"
#else
#include "cond_var.h"
#endif

#include "timer_store.h"
#include "callback.h"

class TimerHandler
{
public:
  TimerHandler(TimerStore*, Callback*);
  ~TimerHandler();
  virtual void add_timer(Timer*);
  virtual void update_replica_tracker_for_timer(TimerID id,
                                                int replica_index);
  virtual HTTPCode get_timers_for_node(std::string node,
                                       int max_responses,
                                       std::string cluster_id,
                                       std::string& get_response);
  void run();

  friend class TestTimerHandler;

#ifdef UNITTEST
  TimerHandler() {}
#endif

private:
  void pop(std::unordered_set<Timer*>&);
  void pop(Timer*);
  void signal_new_timer(unsigned int);

  TimerStore* _store;
  Callback* _callback;

  pthread_t _handler_thread;
  volatile bool _terminate;
  volatile unsigned int _nearest_new_timer;
  pthread_mutex_t _mutex;

#ifdef UNITTEST
  MockPThreadCondVar* _cond;
#else
  CondVar* _cond;
#endif

  static void* timer_handler_entry_func(void *);
};

#endif
