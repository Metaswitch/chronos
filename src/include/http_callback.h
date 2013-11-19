#ifndef HTTP_CALLBACK_H__
#define HTTP_CALLBACK_H__

#include "callback.h"

#include <string>
#include <curl/curl.h>

class HTTPCallback : public Callback
{
public:
  HTTPCallback();
  ~HTTPCallback();

  std::string protocol() { return "http"; };
  bool perform(std::string, std::string, unsigned int);

private:
  CURL* _curl;
};

#endif
