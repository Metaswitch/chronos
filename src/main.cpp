/**
 * @file main.cpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

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
#include "load_monitor.h"
#include "health_checker.h"
#include "exception_handler.h"
#include <getopt.h>
#include "chronos_internal_connection.h"
#include "chronos_alarmdefinition.h"
#include "snmp_infinite_timer_count_table.h"
#include "snmp_infinite_scalar_table.h"
#include "snmp_continuous_increment_table.h"
#include "snmp_counter_table.h"
#include "snmp_scalar.h"
#include "snmp_agent.h"

#include <iostream>
#include <cassert>

#include "time.h"

struct options
{
  std::string config_file;
  std::string cluster_config_file;
  std::string pidfile;
  bool daemon;
};

// Enum for option types not assigned short-forms
enum OptionTypes
{
  CONFIG_FILE = 128, // start after the ASCII set ends to avoid conflicts
  CLUSTER_CONFIG_FILE,
  PIDFILE,
  DAEMON,
  HELP
};

const static struct option long_opt[] =
{
  {"config-file", required_argument, NULL, CONFIG_FILE},
  {"cluster-config-file", required_argument, NULL, CLUSTER_CONFIG_FILE},
  {"pidfile", required_argument, NULL, PIDFILE},
  {"daemon", no_argument, NULL, DAEMON},
  {"help", no_argument, NULL, HELP},
  {NULL, 0, NULL, 0},
};

void usage(void)
{
  puts("Options:\n"
       "\n"
       " --config-file <filename>         Specify the per node configuration file\n"
       " --cluster-config-file <filename> Specify the cluster configuration file\n"
       " --pidfile <filename>             Specify the pidfile\n"
       " --daemon                         Run in the background as a daemon\n"
       " --help                           Show this help screen\n");
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
      options.config_file = std::string(optarg);
      break;

    case CLUSTER_CONFIG_FILE:
      options.cluster_config_file = std::string(optarg);
      break;

    case PIDFILE:
      options.pidfile = std::string(optarg);
      break;

    case DAEMON:
      options.daemon = true;
      break;

    case HELP:
      usage();
      return -1;

    default:
      TRC_ERROR("Unknown option. Run with --help for options.\n");
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
  TRC_BACKTRACE("Signal %d caught", sig);

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  TRC_COMMIT();

  // Check if there's a stored jmp_buf on the thread and handle if there is
  exception_handler->handle_exception();

  CL_CHRONOS_CRASHED.log(strsignal(sig));

  // Dump a core.
  abort();
}

int main(int argc, char** argv)
{
  // Initialize cURL before creating threads
  curl_global_init(CURL_GLOBAL_DEFAULT);

  // Set up our exception signal handler for asserts and segfaults.
  signal(SIGABRT, signal_handler);
  signal(SIGSEGV, signal_handler);

  sem_init(&term_sem, 0, 0);
  signal(SIGTERM, terminate_handler);
  signal(SIGINT, terminate_handler);

  struct options options;
  options.config_file = "/etc/chronos/chronos.conf";
  options.cluster_config_file = "/etc/chronos/chronos_cluster.conf";
  options.pidfile = "";

  if (init_options(argc, argv, options) != 0)
  {
    return 1;
  }

  // Initialise ENT logging before making "Started" log
  PDLogStatic::init(argv[0]);

  CL_CHRONOS_STARTED.log();

  if (options.daemon)
  {
    // Options parsed and validated, time to demonize before writing out our
    // pidfile or spwaning threads.
    int errnum = Utils::daemonize();
    if (errnum != 0)
    {
      TRC_ERROR("Failed to convert to daemon, %d (%s)", errnum, strerror(errnum));
      exit(0);
    }
  }

  // Log the PID, this is useful for debugging if monit restarts chronos.
  TRC_STATUS("Starting with PID %d", getpid());

  if (options.pidfile != "")
  {
    int rc = Utils::lock_and_write_pidfile(options.pidfile);
    if (rc == -1)
    {
      // Failure to acquire pidfile lock
      TRC_ERROR("Could not write pidfile - exiting");
      return 2;
    }
  }

  start_signal_handlers();

  // Initialize the global configuration. Creating the __globals object
  // updates the global configuration. It also creates an updater thread,
  // so this mustn't be created until after the process has daemonised.
  __globals = new Globals(options.config_file,
                          options.cluster_config_file);

  Alarm* scale_operation_alarm = NULL;
  SNMP::U32Scalar* remaining_nodes_scalar = NULL;
  SNMP::CounterTable* timers_processed_table = NULL;
  SNMP::CounterTable* invalid_timers_processed_table = NULL;
  SNMP::ContinuousIncrementTable* all_timers_table = NULL;
  SNMP::InfiniteTimerCountTable* total_timers_table = NULL;
  SNMP::InfiniteScalarTable* scalar_timers_table = NULL;

  // Sets up SNMP statistics
  snmp_setup("chronos");

  all_timers_table = SNMP::ContinuousIncrementTable::create("chronos_all_timers_table",
                                                            ".1.2.826.0.1.1578918.9.10.4");
  total_timers_table = SNMP::InfiniteTimerCountTable::create("chronos_tagged_timers_table",
                                                             ".1.2.826.0.1.1578918.999");
  scalar_timers_table = SNMP::InfiniteScalarTable::create("chronos_scalar_timers_table",
                                                             ".1.2.826.0.1.1578918.998");

  remaining_nodes_scalar = new SNMP::U32Scalar("chronos_remaining_nodes_scalar",
                                               ".1.2.826.0.1.1578918.9.10.1");
  timers_processed_table = SNMP::CounterTable::create("chronos_processed_timers_table",
                                                      ".1.2.826.0.1.1578918.9.10.2");
  invalid_timers_processed_table = SNMP::CounterTable::create("chronos_invalid_timers_processed_table",
                                                              ".1.2.826.0.1.1578918.9.10.3");

  // Must be called after all SNMP tables have been registered
  init_snmp_handler_threads("chronos");

  // Create Chronos's alarm objects. Note that the alarm identifier strings must match those
  // in the alarm definition JSON file exactly.
  scale_operation_alarm = new Alarm("chronos", AlarmDef::CHRONOS_SCALE_IN_PROGRESS,
                                               AlarmDef::MINOR);

  // Start the alarm request agent
  AlarmReqAgent::get_instance().start();
  // Explicitly clear scaling alarm in case we died while the alarm was still active,
  // to ensure that the alarm is not then stuck in a set state.
  scale_operation_alarm->clear();

  // Now create the Chronos components
  HealthChecker* hc = new HealthChecker();
  hc->start_thread();

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
  HTTPCallback* callback = new HTTPCallback();
  TimerHandler* handler = new TimerHandler(store, callback,
                                           handler_rep,
                                           all_timers_table,
                                           total_timers_table,
                                           scalar_timers_table);
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
    TRC_DEBUG("Local host is an IPv6 address");
    af = AF_INET6;
  }

  HttpResolver* http_resolver = new HttpResolver(dns_resolver, af);

  ChronosInternalConnection* chronos_internal_connection =
            new ChronosInternalConnection(http_resolver,
                                          handler,
                                          handler_rep,
                                          scale_operation_alarm,
                                          remaining_nodes_scalar,
                                          timers_processed_table,
                                          invalid_timers_processed_table);


  int target_latency;
  int max_tokens;
  int initial_token_rate;
  int min_token_rate;
  __globals->get_target_latency(target_latency);
  __globals->get_max_tokens(max_tokens);
  __globals->get_initial_token_rate(initial_token_rate);
  __globals->get_min_token_rate(min_token_rate);


  LoadMonitor* load_monitor = new LoadMonitor(target_latency,
                                              max_tokens,
                                              initial_token_rate,
                                              min_token_rate);

  // Finally, set up the HTTPStack and handlers
  HttpStack* http_stack = HttpStack::get_instance();
  int bind_port;
  int http_threads;
  __globals->get_bind_port(bind_port);
  __globals->get_threads(http_threads);

  HttpStackUtils::PingHandler ping_handler;
  ControllerTask::Config controller_config(controller_rep, handler);
  HttpStackUtils::SpawningHandler<ControllerTask, ControllerTask::Config> controller_handler(&controller_config,
                                                                                             &HttpStack::NULL_SAS_LOGGER);

  try
  {
    http_stack->initialize();
    http_stack->configure(bind_address, bind_port, http_threads, exception_handler, NULL, load_monitor, NULL);
    http_stack->register_handler((char*)"^/ping$", &ping_handler);
    http_stack->register_handler((char*)"^/timers", &controller_handler);
    http_stack->start();
    CL_CHRONOS_HTTP_SERVICE_AVAILABLE.log();
  }
  catch (HttpStack::Exception& e)
  {
    CL_CHRONOS_HTTP_INTERFACE_FAIL.log(e._func, e._rc);
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

  delete remaining_nodes_scalar;
  delete timers_processed_table;
  delete invalid_timers_processed_table;
  delete total_timers_table;

  hc->stop_thread();
  delete hc; hc = NULL;
  delete exception_handler; exception_handler = NULL;

  // Stop the alarm request agent
  AlarmReqAgent::get_instance().stop();

  // Delete Chronos's alarm object
  delete scale_operation_alarm;

  sem_destroy(&term_sem);

  // After this point nothing will use __globals so it's safe to delete
  // it here.
  CL_CHRONOS_ENDED.log();
  delete __globals; __globals = NULL;
  curl_global_cleanup();

  return 0;
}
