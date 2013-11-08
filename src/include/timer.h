#ifndef TIMER_H__
#define TIMER_H__

#include <vector>
#include <string>

typedef unsigned int TimerID;

class Timer
{
public:
  Timer(TimerID,
        unsigned int,
        unsigned int,
        unsigned int,
        unsigned int,
        std::vector<std::string>,
        std::string,
        std::string);
  ~Timer();

  unsigned int id;
  unsigned int start_time;
  unsigned int interval;
  unsigned int repeat_for;
  unsigned int sequence_number;
  std::vector<std::string> replicas;
  std::string callback_url;
  std::string callback_body;
};

#endif
