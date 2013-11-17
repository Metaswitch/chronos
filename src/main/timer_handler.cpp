#include <time.h>

#include "timer_handler.h"

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
                           _nearest_new_timer(0)
{
  pthread_mutex_init(&_mutex, NULL);
  pthread_cond_init(&_cond_var, NULL);

  int rc = pthread_create(&_handler_thread,
                          NULL,
                          &timer_handler_entry_func,
                          (void*)this);
  if (rc < 0)
  {
    // LOG_FATAL("Failed to start timer handling thread: %s", strerror(errno)"
    exit(2);
  }
}

TimerHandler::~TimerHandler()
{
  if (_handler_thread)
  {
    pthread_mutex_lock(&_mutex);
    _terminate = true;
    pthread_cond_signal(&_cond_var);
    pthread_mutex_unlock(&_mutex);
    pthread_join(_handler_thread, NULL);
  }

  pthread_cond_destroy(&_cond_var);
  pthread_mutex_destroy(&_mutex);

  delete _replicator;
  delete _callback;
}

void TimerHandler::add_timer(Timer* timer)
{
  pthread_mutex_lock(&_mutex);
  _store->add_timer(timer);
  signal_new_timer(timer->next_pop_time());
  pthread_mutex_unlock(&_mutex);
}

void TimerHandler::run() {
  std::unordered_set<Timer*> next_timers;
  std::unordered_set<Timer*>::iterator sample_timer;
  struct timespec current_time;

  _store->get_next_timers(next_timers);

  pthread_mutex_lock(&_mutex);

  while (!_terminate)
  {
    if (next_timers.empty())
    {
      pthread_cond_wait(&_cond_var, &_mutex);
      _store->get_next_timers(next_timers);
    }
    else
    {
      sample_timer = next_timers.begin();
      Timer* timer = *sample_timer;

      int rc = clock_gettime(CLOCK_REALTIME, &current_time);
      if (rc < 0)
      {
        // Failed to get the current time.  According to `man 3 clock_gettime` this
        // cannot occur.  If it does, it's fatal.
        // LOG_FATAL("Failed to get system time: %s", strerror(errno));
        exit(2);
      }

      if (timer->next_pop_time() <= (current_time.tv_sec * 1000) + (current_time.tv_nsec / 1000))
      {
        pop(next_timers);
        _store->get_next_timers(next_timers);
      }
      else
      {
        while ((_nearest_new_timer >= timer->next_pop_time()) && (rc != ETIMEDOUT))
        {
          struct timespec next_pop;
          timer->next_pop_time(next_pop);
          int rc = pthread_cond_timedwait(&_cond_var, &_mutex, &next_pop);
          if (rc < 0 && rc != ETIMEDOUT)
          {
            // LOG_FATAL("Failed to wait for condition variable: %s", strerror(errno));
            exit(2);
          }
        }

        if (_nearest_new_timer < timer->next_pop_time())
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

// Used to pop a set of timers, this function takes ownership of the timers and
// thus empties the passed in set.
void TimerHandler::pop(std::unordered_set<Timer*>& timers)
{
  for (auto it = timers.begin(); it != timers.end(); it++)
  {
    pop(*it);
  }
  timers.empty();
}

// Pop a specific timer, if required pass the timer on to the replication layer to
// reset the timer for another pop, otherwise destroy the timer record.
void TimerHandler::pop(Timer* timer)
{
  bool success = _callback->perform(timer->callback_url,
                                    timer->callback_body,
                                    timer->sequence_number);
  if (success)
  {
    timer->sequence_number++;
    if (timer->sequence_number * timer->interval <= timer->repeat_for)
    {
      _replicator->replicate(timer);
    }
    else
    {
      delete timer;
    }
  }

void TimerHandler::signal_new_timer(unsigned int pop_time)
{
  if (_nearest_new_timer < pop_time)
  {
    _nearest_new_timer = pop_time;
    pthread_cond_signal(&_cond_var);
  }
}

