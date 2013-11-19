#include "timer.h"
#include "timer_store.h"
#include "timer_handler.h"
#include "replicator.h"
#include "callback.h"
#include "http_callback.h"
#include "controller.h"

#include <iostream>
#include <cassert>

#include "time.h"

int main(int argc, char** argv)
{
  // Initialize cURL before creating threads
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Create components
  TimerStore *store = new TimerStore();
  store = new TimerStore();
  Replicator* controller_rep = new Replicator();
  Replicator* handler_rep = new Replicator();
  HTTPCallback* callback = new HTTPCallback();
  TimerHandler* handler = new TimerHandler(store, handler_rep, callback);
  Controller* controller = new Controller(controller_rep, handler);

  // Create an event reactor.
  struct event_base* base = event_base_new();
  if (!base) {
    fprintf(stderr, "Couldn't create an event_base: exiting\n");
    return 1;
  }

  // Create an HTTP server instance.
  struct evhttp* http = evhttp_new(base);
  if (!http) {
    fprintf(stderr, "couldn't create evhttp. Exiting.\n");
    return 1;
  }

  // Register a callback for the "/ping" path.
  evhttp_set_cb(http, "/ping", Controller::controller_ping_cb, NULL);

  // Register a callback for the "/timers" path, we have to do this with the
  // generic callback as libevent doesn't support regex paths.
  evhttp_set_gencb(http, Controller::controller_cb, controller);

  // Bind to the correct port
  evhttp_bind_socket(http, "0.0.0.0", 7253);

  // Start the reactor, this blocks the current thread
  event_base_dispatch(base);

  curl_global_cleanup();

  return 0;
}
