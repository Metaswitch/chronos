#include "timer.h"
#include "timer_store.h"
#include "timer_handler.h"
#include "replicator.h"
#include "callback.h"
#include "http_callback.h"
#include "controller.h"
#include "globals.h"

#include <iostream>
#include <cassert>

#include "time.h"

// Signal handler that simply dumps the stack and then crashes out.
void exception_handler(int sig)
{
  // Reset the signal handlers so that another exception will cause a crash.
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);

  // Log the signal, along with a backtrace.
  LOG_BACKTRACE("Signal %d caught", sig);

  // Dump a core.
  abort();
}

int main(int argc, char** argv)
{
  // Initialize cURL before creating threads
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, exception_handler);
  signal(SIGSEGV, exception_handler);

  // Initialize the global configuration.
  __globals = new Globals();
  __globals->update_config();

  // Create components
  TimerStore *store;
  store = new TimerStore();
  Replicator* controller_rep = new Replicator();
  Replicator* handler_rep = new Replicator();
  HTTPCallback* callback = new HTTPCallback();
  TimerHandler* handler = new TimerHandler(store, handler_rep, callback);
  Controller* controller = new Controller(controller_rep, handler);

  // Create an event reactor.
  struct event_base* base = event_base_new();
  if (!base) {
    std::cerr << "Couldn't create an event_base: exiting" << std::endl;
    return 1;
  }

  // Create an HTTP server instance.
  struct evhttp* http = evhttp_new(base);
  if (!http) {
    std::cerr << "Couldn't create evhttp: exiting" << std::endl;
    return 1;
  }

  // Register a callback for the "/ping" path.
  evhttp_set_cb(http, "/ping", Controller::controller_ping_cb, NULL);

  // Register a callback for the "/timers" path, we have to do this with the
  // generic callback as libevent doesn't support regex paths.
  evhttp_set_gencb(http, Controller::controller_cb, controller);

  // Bind to the correct port
  std::string bind_address;
  int bind_port;
  __globals->get_bind_address(bind_address);
  __globals->get_bind_port(bind_port);
  evhttp_bind_socket(http, bind_address.c_str(), bind_port);

  // Start the reactor, this blocks the current thread
  event_base_dispatch(base);

  // Event loop is completed, terminate.
  //
  // After this point nothing will use __globals so it's safe to delete
  // it here.
  delete __globals; __globals = NULL;
  curl_global_cleanup();

  return 0;
}
