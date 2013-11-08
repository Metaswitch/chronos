#include "timer.h"

Timer::Timer(TimerID id,
             unsigned int start_time,
             unsigned int interval,
             unsigned int repeat_for,
             unsigned int sequence_number,
             std::vector<std::string> replicas,
             std::string callback_url,
             std::string callback_body) :
  id(id),
  start_time(start_time),
  interval(interval),
  repeat_for(repeat_for),
  sequence_number(sequence_number),
  replicas(replicas),
  callback_url(callback_url),
  callback_body(callback_body)
{
}

Timer::~Timer()
{
}
