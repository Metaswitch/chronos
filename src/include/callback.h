#ifndef CALLBACK_H__
#define CALLBACK_H__

#include "timer.h"

#include <string>

// Virtual class for handling timer callbacks.
class Callback
{
public:
  virtual ~Callback() {};

  // Returns the protocol handled by this callback (e.g. http or zmq).
  // This can be compared to the requested callback from the timer
  // to chose a callback hander to manage it.
  virtual std::string protocol() = 0;

  // Perform the callback for the given Timer.  Takes ownership of the Timer object.
  //
  // Returns true if the callback was successful, false otherwise.
  virtual void perform(Timer*) = 0;
};

#endif
