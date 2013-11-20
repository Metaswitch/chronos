#ifndef COND_VAR_H__
#define COND_VAR_H__

#include <pthread.h>

class CondVar
{
public:
  CondVar(pthread_mutex_t* mutex) : _mutex(mutex) { pthread_cond_init(&_cond, NULL); }
  ~CondVar() { pthread_cond_destroy(&_cond); }

  int wait() { return pthread_cond_wait(&_cond, _mutex); }
  int timedwait(struct timespec* ts) { return pthread_cond_timedwait(&_cond, _mutex, ts); }
  int signal() { return pthread_cond_signal(&_cond); }
  int broadcast() { return pthread_cond_broadcast(&_cond); }

private:
  pthread_cond_t _cond;
  pthread_mutex_t* _mutex;
};

#endif
