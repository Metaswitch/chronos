#ifndef CALLBACK_H__
#define CALLBACK_H__

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

  // Perform the callback.
  //
  //  * The first argument is the location of the callback server (e.g. HTTP URL or zmq socket)
  //  * The second argument is the opaque data for the callback (e.g. HTTP body)
  //  * The third argument is the sequence number for the callback.
  //
  //  Returns true if the callback was successful, false otherwise.
  virtual bool perform(std::string, std::string, unsigned int) = 0;
};

#endif
