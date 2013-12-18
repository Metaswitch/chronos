#ifndef PTHREAD_COND_VAR_HELPER_H__
#define PTHREAD_COND_VAR_HELPER_H__

#include <pthread.h>

enum STATE { WAIT, TIMED_WAIT, SIGNALED, TIMEDOUT };

class MockPThreadCondVar
{
public:
  MockPThreadCondVar(pthread_mutex_t *mutex);
  ~MockPThreadCondVar();

  // Mirror of CondVar
  int wait();
  int timedwait(struct timespec*);
  int signal();

  // Test script functions
  bool check_signaled();
  void block_till_waiting();
  void block_till_signaled();
  void check_timeout(const struct timespec&);
  void lock();
  void unlock();
  void signal_wake();
  void signal_timeout();

private:

  STATE _state;
  struct timespec _timeout;
  bool _signaled;
  pthread_mutex_t* _mutex;
  pthread_cond_t _cond;
};

#endif
