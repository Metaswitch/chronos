#include <time.h>
#include <cstring>
#include <iostream>

#include "timer_handler.h"
#include "log.h"

void* TimerHandler::timer_handler_entry_func(void* arg)
{
  ((TimerHandler*)arg)->run();
  return NULL;
}

TimerHandler::TimerHandler(TimerStore* store,
                           Replicator* replicator,
                           Callback* callback) :
                           _store(store),
                           _replicator(replicator),
                           _callback(callback),
                           _terminate(false),
                           _nearest_new_timer(-1)
{
  pthread_mutex_init(&_mutex, NULL);

#ifdef UNITTEST
  _cond = new MockPThreadCondVar(&_mutex);
#else
  _cond = new CondVar(&_mutex);
#endif

  int rc = pthread_create(&_handler_thread,
                          NULL,
                          &timer_handler_entry_func,
                          (void*)this);
  if (rc < 0)
  {
    printf("Failed to start timer handling thread: %s", strerror(errno));
    exit(2);
  }
}

TimerHandler::~TimerHandler()
{
  if (_handler_thread)
  {
    pthread_mutex_lock(&_mutex);
    _terminate = true;
    _cond->signal();
    pthread_mutex_unlock(&_mutex);
    pthread_join(_handler_thread, NULL);
  }

  delete _cond;
  _cond = NULL;

  pthread_mutex_destroy(&_mutex);

  delete _replicator;
  delete _callback;
}

void TimerHandler::add_timer(Timer* timer)
{
  LOG_DEBUG("Adding timer:  %lu", timer->id);
  pthread_mutex_lock(&_mutex);
  signal_new_timer(timer->next_pop_time());
  _store->add_timer(timer);
  pthread_mutex_unlock(&_mutex);
}

// The core function in the timer handler, basic principle is to loop around repeatedly
// retrieving timers from the store, waiting until they need to pop and popping them.
//
// If there are no timers in the store at all, we wait forever for one to be added (or
// until we're terminated).  If we are woken while waiting for one set of timers to
// pop, check the timer store to make sure we're holding the nearest timers.
void TimerHandler::run() {
  std::unordered_set<Timer*> next_timers;
  std::unordered_set<Timer*>::iterator sample_timer;
  struct timespec current_time;

  pthread_mutex_lock(&_mutex);

  _store->get_next_timers(next_timers);

  while (!_terminate)
  {
    if (next_timers.empty())
    {
      // We have no timers, the next added timer will wake us
      _nearest_new_timer = -1;
      _cond->wait();
      _store->get_next_timers(next_timers);
    }
    else
    {
      sample_timer = next_timers.begin();
      Timer* timer = *sample_timer;
      struct timespec next_pop;
      timer->next_pop_time(next_pop);
      _nearest_new_timer = timer->next_pop_time();

      int rc = clock_gettime(CLOCK_MONOTONIC, &current_time);
      if (rc < 0)
      {
        // Failed to get the current time.  According to `man 3 clock_gettime` this
        // cannot occur.  If it does, it's fatal.
        printf("Failed to get system time: %s", strerror(errno));
        exit(2);
      }

      unsigned long long current_timestamp = current_time.tv_sec * 1000;
      current_timestamp += current_time.tv_nsec / 1000000;
      if (timer->next_pop_time() <= current_timestamp)
      {
        pop(next_timers);
        _store->get_next_timers(next_timers);
      }
      else
      {
        rc = 0;
        while ((!_terminate) &&
               (_nearest_new_timer <= timer->next_pop_time()) &&
               (rc != ETIMEDOUT))
        {
          rc = _cond->timedwait(&next_pop);
          if (rc < 0 && rc != ETIMEDOUT)
          {
            printf("Failed to wait for condition variable: %s", strerror(errno));
            exit(2);
          }
        }

        if (_nearest_new_timer > timer->next_pop_time())
        {
          // The timers we're holding are not the next to pop, swap them out.
          _store->add_timers(next_timers);
          _store->get_next_timers(next_timers);
        }
      }
    }
  }

  for (auto it = next_timers.begin(); it != next_timers.end(); it++)
  {
    delete *it;
  }
  next_timers.clear();

  pthread_mutex_unlock(&_mutex);
}

/*****************************************************************************/
/* PRIVATE FUNCTIONS                                                         */
/*****************************************************************************/

// Pop a set of timers, this function takes ownership of the timers and
// thus empties the passed in set.
void TimerHandler::pop(std::unordered_set<Timer*>& timers)
{
  for (auto it = timers.begin(); it != timers.end(); it++)
  {
    pop(*it);
  }
  timers.clear();
}

// Pop a specific timer, if required pass the timer on to the replication layer to
// reset the timer for another pop, otherwise destroy the timer record.
void TimerHandler::pop(Timer* timer)
{
  // Tombstones are reaped when they pop.
  if (timer->is_tombstone())
  {
    delete timer;
    return;
  }

  timer->sequence_number++;
  bool success = _callback->perform(timer->callback_url,
                                    timer->callback_body,
                                    timer->sequence_number);
  if (success)
  {
    // Check if the next pop occurs before the repeat-for interval and,
    // if not, convert to a tombstone to indicate the timer is dead.
    if ((timer->sequence_number + 1) * timer->interval > timer->repeat_for)
    {
      timer->become_tombstone();
    }
    _replicator->replicate(timer);
    _store->add_timer(timer);
    timer = NULL; // We relinquish control of the timer when we give
                  // it to the store.
  }
  else
  {
    LOG_WARNING("Failed to process callback for %lu", timer->id);
    delete timer;
  }
}

void TimerHandler::signal_new_timer(unsigned int pop_time)
{
  if (_nearest_new_timer > pop_time)
  {
    _nearest_new_timer = pop_time;
    _cond->signal();
  }
}

