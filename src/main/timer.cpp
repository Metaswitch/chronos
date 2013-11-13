#include "timer.h"

#include <sstream>

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

// Returns the next pop time in ms.
unsigned int Timer::next_pop_time()
{
  // TODO add replication skew
  return start_time + (sequence_number * interval);
}

// Construct a timespec describing the next pop time.
void Timer::next_pop_time(struct timespec& ts)
{
  unsigned int pop_time = next_pop_time();
  ts.tv_sec = pop_time / 1000;
  ts.tv_nsec = (pop_time % 1000) * 1000;
}

// Create the timer's URL from a given hostname.
std::string Timer::url(std::string host)
{
  std::stringstream ss;
  ss << "http://" << host << "/timers/" << id;
  return ss.str();
}

// Render the timer as JSON to be used in an HTTP request body.
std::string Timer::to_json()
{
  std::stringstream ss;
  ss << "{\"timing\":{\"start-at\":\""
     << start_time
     << "\",\"sequence-number\":\""
     << sequence_number
     << "\",\"interval\":\""
     << interval
     << "\",\"repeat-for\":\""
     << repeat_for
     << "\"},\"callback\":{\"http\":{\"uri\":\""
     << callback_url
     << "\",\"opaque\":\""
     << callback_body
     << "\"}},\"reliability\":{\"replicas\":[";
  for (auto it = replicas.begin(); it != replicas.end(); it++)
  {
    ss << "\"" << *it << "\"";
    if (it + 1 != replicas.end())
    {
      ss << ",";
    }
  }
  ss << "]}}";
  return ss.str();
}

TimerID Timer::generate_timer_id()
{
  // TODO snowflake?
  return 1;
}

Timer* Timer::from_json(TimerID id, std::string json)
{
  // TODO Parse the JSON + add validate function to Timer
  Timer* timer = new Timer(id);
  return timer;
}
