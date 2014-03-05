#ifndef COND_VAR_H__
#define COND_VAR_H__

#include <pthread.h>

// This class wraps a condition varable and mutex together and offers a C++ interface.
//
// Apart from the more C++-like interface benefit, this also allows us to mock out
// condition variables in the UT framework (see MockPThreadCondVar for the mock 
// implementation).
class CondVar
{
public:
  CondVar(pthread_mutex_t* mutex) : _mutex(mutex)
  {
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&_cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
  }
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
