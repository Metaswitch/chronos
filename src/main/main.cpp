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
#include "exception_handler.h"
#include <getopt.h>
#include "chronos_internal_connection.h"

#include <iostream>
#include <cassert>

#include "time.h"

struct options
{
  std::string config_file;
};

// Enum for option types not assigned short-forms
enum OptionTypes
{
  CONFIG_FILE = 128, // start after the ASCII set ends to avoid conflicts
  HELP
};

const static struct option long_opt[] =
{
  {"config-file", required_argument, NULL, CONFIG_FILE},
  {"help", no_argument, NULL, HELP},
  {NULL, 0, NULL, 0},
};

void usage(void)
{
  puts("Options:\n"
       "\n"
       " --config-file <filename> Specify the configuration file\n"
       " --help Show this help screen\n");
}

int init_options(int argc, char**argv, struct options& options)
{
  int opt;
  int long_opt_ind;
  optind = 0;
  while ((opt = getopt_long(argc, argv, "", long_opt, &long_opt_ind)) != -1)
  {
    switch (opt)
    {
    case CONFIG_FILE:
      LOG_INFO("Configuration file: %s", optarg);
      options.config_file = std::string(optarg);
      break;

    case HELP:
      usage();
      return -1;

    default:
      LOG_ERROR("Unknown option. Run with --help for options.\n");
      return -1;
    }
  }
  return 0;
}

static sem_t term_sem;
ExceptionHandler* exception_handler;

// Signal handler that triggers Chronos termination.
void terminate_handler(int sig)
{
  sem_post(&term_sem);
}

// Signal handler that simply dumps the stack and then crashes out.
void signal_handler(int sig)
{
  // Reset the signal handlers so that another exception will cause a crash.
  signal(SIGABRT, SIG_DFL);
  signal(SIGSEGV, signal_handler);

  // Log the signal, along with a backtrace.
  LOG_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  LOG_COMMIT();

  // Check if there's a stored jmp_buf on the thread and handle if there is
  exception_handler->handle_exception();

  CL_CHRONOS_CRASHED.log(strsignal(sig));
  closelog();

  // Dump a core.
  abort();
}

int main(int argc, char** argv)
{
  Alarm* timer_pop_alarm = NULL;
  Alarm* scale_operation_alarm = NULL;

  // Initialize cURL before creating threads
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, signal_handler);
  signal(SIGSEGV, signal_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);

  struct options options;
  options.config_file = "/etc/chronos/chronos.conf";

  if (init_options(argc, argv, options) != 0)
  {
    return 1;
  }

  // Initialize the global configuration. Creating the __globals object
  // updates the global configuration
  __globals = new Globals(options.config_file);

  boost::filesystem::path p = argv[0];
  // Copy the filename to a string so that we can be sure of its lifespan -
  // the value passed to openlog must be valid for the duration of the program.
  std::string filename = p.filename().c_str();
  openlog(filename.c_str(), PDLOG_PID, PDLOG_LOCAL6);
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
    scale_operation_alarm = new Alarm("chronos", AlarmDef::CHRONOS_SCALE_IN_PROGRESS,
                                                 AlarmDef::MINOR);

    // Start the alarm request agent
    AlarmReqAgent::get_instance().start();
    AlarmState::clear_all("chronos");
  }

  // Now create the Chronos components
  HealthChecker* hc = new HealthChecker();
  pthread_t health_check_thread;
  pthread_create(&health_check_thread,
                 NULL,
                 &HealthChecker::static_main_thread_function,
                 (void*)hc);

  // Create an exception handler. The exception handler doesn't need
  // to quiesce the process before killing it.
  int ttl;
  __globals->get_max_ttl(ttl);
  exception_handler = new ExceptionHandler(ttl,
                                           false,
                                           hc);

  // Create the timer store, handlers, replicators...
  TimerStore *store = new TimerStore(hc);
  Replicator* controller_rep = new Replicator(exception_handler);
  Replicator* handler_rep = new Replicator(exception_handler);
  HTTPCallback* callback = new HTTPCallback(handler_rep, timer_pop_alarm);
  TimerHandler* handler = new TimerHandler(store, callback);
  callback->start(handler);

  // Create a Chronos internal connection class for scaling operations. 
  // This uses HTTPConnection from cpp-common so needs various 
  // resolvers
  std::vector<std::string> dns_servers;
  __globals->get_dns_servers(dns_servers);

  DnsCachedResolver* dns_resolver = new DnsCachedResolver(dns_servers);

  int af = AF_INET;
  struct in6_addr dummy_addr;
  std::string bind_address;
  __globals->get_bind_address(bind_address);

  if (inet_pton(AF_INET6, bind_address.c_str(), &dummy_addr) == 1)
  {
    LOG_DEBUG("Local host is an IPv6 address");
    af = AF_INET6;
  }

  HttpResolver* http_resolver = new HttpResolver(dns_resolver, af);

  std::string stats[] = {"chronos_scale_nodes_to_query",
                         "chronos_scale_timers_processed",
                         "chronos_scale_invalid_timers_processed"};
  LastValueCache* lvc = new LastValueCache(3, stats, "chronos");

  ChronosInternalConnection* chronos_internal_connection =
            new ChronosInternalConnection(http_resolver, 
                                          handler, 
                                          handler_rep, 
                                          lvc,
                                          scale_operation_alarm);

  // Finally, set up the HTTPStack and handlers
  HttpStack* http_stack = HttpStack::get_instance();
  int bind_port;
  int http_threads;
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
    http_stack->configure(bind_address, bind_port, http_threads, exception_handler);
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

  delete chronos_internal_connection; chronos_internal_connection = NULL;
  delete http_resolver; http_resolver = NULL;
  delete dns_resolver; dns_resolver = NULL;
  delete handler; handler = NULL;
  // Callback is deleted by the handler
  delete handler_rep; handler_rep = NULL;
  delete controller_rep; controller_rep = NULL;
  delete store; store = NULL;

  hc->terminate();
  pthread_join(health_check_thread, NULL);
  delete hc; hc = NULL;
  delete exception_handler; exception_handler = NULL;

  if (alarms_enabled)
  {
    // Stop the alarm request agent
    AlarmReqAgent::get_instance().stop();

    // Delete Chronos's alarm objects
    delete timer_pop_alarm;
    delete scale_operation_alarm;
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
