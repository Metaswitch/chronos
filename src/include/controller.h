#ifndef CONTROLLER_H__
#define CONTROLLER_H__

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <string>

class Controller
{
public:
  Controller();
  ~Controller();

  void handle_request(struct evhttp_request*);

  static void controller_cb(struct evhttp_request*, void*);
  static void controller_ping_cb(struct evhttp_request*, void*);

private:

  void send_error(struct evhttp_request*, int, const char*);
  std::string get_req_body(struct evhttp_request*);
};

#endif
