#ifndef TIMER_H__
#define TIMER_H__

#include <vector>
#include <string>

typedef unsigned int TimerID;

class Timer
{
public:
  Timer(TimerID,
        unsigned int = 0,
        unsigned int = 0,
        unsigned int = 0,
        unsigned int = 0,
        std::vector<std::string> = std::vector<std::string>(),
        std::string = "",
        std::string = "");
  ~Timer();

  // Returns the next time to pop in ms after epoch
  unsigned int next_pop_time();

  // Returns the next time to pop in ns after epoch in a timespec
  void next_pop_time(struct timespec&);

  // Construct the URL for this timer given a hostname
  std::string url(std::string);

  // Convert this timer to JSON to be sent to replicas
  std::string to_json();

  // Member variables
  TimerID id;
  unsigned int start_time;
  unsigned int interval;
  unsigned int repeat_for;
  unsigned int sequence_number;
  unsigned int replication_factor;
  std::vector<std::string> replicas;
  std::string callback_url;
  std::string callback_body;

  // Class functions
  static TimerID generate_timer_id();
  static Timer* create_tombstone(TimerID);
  static Timer* from_json(TimerID, std::string, std::string&);

  // Class variables
  static uint32_t deployment_id;
  static uint32_t instance_id;
};

#endif
