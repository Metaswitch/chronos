#include "timer_helper.h"

Timer* default_timer(TimerID id)
{
  return new Timer(id,
                   1000000,
                   100,
                   100,
                   0,
                   std::vector<std::string>(1, "10.0.0.1"),
                   "localhost:80/callback",
                   "stuff stuff stuff");
}
