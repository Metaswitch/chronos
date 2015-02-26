#include "chronos_pd_definitions.h"
#include "timer.h"
#include "timer_store.h"
#include "timer_handler.h"
#include "replicator.h"
#include "callback.h"
#include "http_callback.h"
#include "globals.h"
#include "alarm.h"
#include <boost/filesystem.hpp>
#include "httpstack.h"
#include "httpstack_utils.h"
#include "handlers.h"
#include "health_checker.h"

#include <iostream>
#include <cassert>

#include "time.h"

static sem_t term_sem;

// Signal handler that triggers Chronos termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

// Signal handler that simply dumps the stack and then crashes out.
void exception_handler(int sig)
{
  // Reset the signal handlers so that another exception will cause a crash.
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, SIG_DFL);

  // Log the signal, along with a backtrace.
  CL_CHRONOS_CRASHED.log(strsignal(sig));
  closelog();
  LOG_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  LOG_COMMIT();

  // Dump a core.
  abort();
}

int main(int argc, char** argv)
{
  Alarm* timer_pop_alarm = NULL;

  // Initialize cURL before creating threads
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, exception_handler);
  signal(SIGSEGV, exception_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  // Initialize the global configuration.
  __globals = new Globals();
  __globals->update_config();

  boost::filesystem::path p = argv[0];
  openlog(p.filename().c_str(), PDLOG_PID, PDLOG_LOCAL6);
  CL_CHRONOS_STARTED.log();

  // Log the PID, this is useful for debugging if monit restarts chronos.
  LOG_STATUS("Starting with PID %d", getpid());

  bool alarms_enabled;
  __globals->get_alarms_enabled(alarms_enabled);

  if (alarms_enabled)
  {
    // Create Chronos's alarm objects. Note that the alarm identifier strings must match those
    // in the alarm definition JSON file exactly.

    timer_pop_alarm = new Alarm("chronos", AlarmDef::CHRONOS_TIMER_POP_ERROR,
                                            AlarmDef::MAJOR);

    // Start the alarm request agent
    AlarmReqAgent::get_instance().start();
    AlarmState::clear_all("chronos");
  }

  // Create components
  HealthChecker* hc = new HealthChecker();
  TimerStore *store = new TimerStore(hc);
  Replicator* controller_rep = new Replicator();
  Replicator* handler_rep = new Replicator();
  HTTPCallback* callback = new HTTPCallback(handler_rep, timer_pop_alarm);
  TimerHandler* handler = new TimerHandler(store, callback);
  callback->start(handler);

  HttpStack* http_stack = HttpStack::get_instance();
  std::string bind_address;
  int bind_port;
  int http_threads;
  __globals->get_bind_address(bind_address);
  __globals->get_bind_port(bind_port);
  __globals->get_threads(http_threads);

  if (!strcmp(bind_address.c_str(), "0.0.0.0"))
  {
    LOG_ERROR("0.0.0.0 has been deprecated for the bind_address setting. Use the local IP address instead");
  }

  HttpStackUtils::PingHandler ping_handler;
  ControllerTask::Config controller_config(controller_rep, handler);
  HttpStackUtils::SpawningHandler<ControllerTask, ControllerTask::Config> controller_handler(&controller_config);

  try
  {
    http_stack->initialize();
    http_stack->configure(bind_address, bind_port, http_threads, NULL);
    http_stack->register_handler((char*)"^/ping$", &ping_handler);
    http_stack->register_handler((char*)"^/timers", &controller_handler);
    http_stack->start();
    CL_CHRONOS_HTTP_SERVICE_AVAILABLE.log();
  }
  catch (HttpStack::Exception& e)
  {
    CL_CHRONOS_HTTP_INTERFACE_FAIL.log(e._func, e._rc);
    closelog();
    std::cerr << "Caught HttpStack::Exception" << std::endl;
    return 1;
  }

  CL_CHRONOS_HTTP_SERVICE_AVAILABLE.log();

  // Wait here until the quit semaphore is signaled.
  sem_wait(&term_sem);

  try
  {
    http_stack->stop();
    http_stack->wait_stopped();
  }
  catch (HttpStack::Exception& e)
  {
    CL_CHRONOS_HTTP_INTERFACE_STOP_FAIL.log(e._func, e._rc);
    std::cerr << "Caught HttpStack::Exception" << std::endl;
  }

  delete handler; handler = NULL;
  // Callback is deleted by the handler
  delete handler_rep; handler_rep = NULL;
  delete controller_rep; controller_rep = NULL;
  delete store; store = NULL;
  delete hc; hc = NULL;
  
  if (alarms_enabled)
  {
    // Stop the alarm request agent
    AlarmReqAgent::get_instance().stop();

    // Delete Chronos's alarm objects
    delete timer_pop_alarm;
  }

  sem_destroy(&term_sem);

  // After this point nothing will use __globals so it's safe to delete
  // it here.
  CL_CHRONOS_ENDED.log();
  closelog();
  delete __globals; __globals = NULL;
  curl_global_cleanup();

  return 0;
}
