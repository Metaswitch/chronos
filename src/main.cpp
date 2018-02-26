/**
 * @file main.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
#include "communicationmonitor.h"
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
#include "gr_replicator.h"
#include "snmp_infinite_timer_count_table.h"
#include "snmp_infinite_scalar_table.h"
#include "snmp_continuous_increment_table.h"
#include "snmp_counter_table.h"
#include "snmp_scalar.h"
#include "snmp_agent.h"
#include "updater.h"

#include <iostream>
#include <cassert>

#include "time.h"

struct options
{
  std::string local_config_file;
  std::string cluster_config_file;
  std::string shared_config_file;
  std::string dns_config_file;
  std::string pidfile;
  bool daemon;
};

// Enum for option types not assigned short-forms
enum OptionTypes
{
  LOCAL_CONFIG_FILE = 128, // start after the ASCII set ends to avoid conflicts
  CLUSTER_CONFIG_FILE,
  SHARED_CONFIG_FILE,
  DNS_CONFIG_FILE,
  PIDFILE,
  DAEMON,
  HELP
};

const static struct option long_opt[] =
{
  {"local-config-file", required_argument, NULL, LOCAL_CONFIG_FILE},
  {"cluster-config-file", required_argument, NULL, CLUSTER_CONFIG_FILE},
  {"shared-config-file", required_argument, NULL, SHARED_CONFIG_FILE},
  {"dns-config-file", required_argument, NULL, DNS_CONFIG_FILE},
  {"pidfile", required_argument, NULL, PIDFILE},
  {"daemon", no_argument, NULL, DAEMON},
  {"help", no_argument, NULL, HELP},
  {NULL, 0, NULL, 0},
};

void usage(void)
{
  puts("Options:\n"
       "\n"
       " --local-config-file <filename>   Specify the per node configuration file\n"
       " --cluster-config-file <filename> Specify the cluster configuration file\n"
       " --shared-config-file <filename>  Specify the site wide configuration file\n"
       " --dns-config-file <filename>     Specify the dns config file\n"
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
    case LOCAL_CONFIG_FILE:
      options.local_config_file = std::string(optarg);
      break;

    case CLUSTER_CONFIG_FILE:
      options.cluster_config_file = std::string(optarg);
      break;

    case SHARED_CONFIG_FILE:
      options.shared_config_file = std::string(optarg);
      break;

    case DNS_CONFIG_FILE:
      options.dns_config_file = std::string(optarg);
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

  // Log the signal, along with a simple backtrace.
  TRC_BACKTRACE("Signal %d caught", sig);

  // Check if there's a stored jmp_buf on the thread and handle if there is
  exception_handler->handle_exception();

  //
  // If we get here it means we didn't handle the exception so we need to exit.
  //

  CL_CHRONOS_CRASHED.log(strsignal(sig));

  // Log a full backtrace to make debugging easier.
  TRC_BACKTRACE_ADV();

  // Ensure the log files are complete - the core file created by abort() below
  // will trigger the log files to be copied to the diags bundle
  TRC_COMMIT();

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
  options.local_config_file = "/etc/chronos/chronos.conf";
  options.cluster_config_file = "/etc/chronos/chronos_cluster.conf";
  options.shared_config_file = "/etc/chronos/chronos_shared.conf";
  options.dns_config_file = "/etc/clearwater/dns.json";
  options.pidfile = "";
  options.daemon = false;

  if (init_options(argc, argv, options) != 0)
  {
    return 1;
  }

  // Copy the program name to a string so that we can be sure of its lifespan -
  // the memory passed to openlog must be valid for the duration of the program.
  //
  // Note that we don't save syslog_identity here, and so we're technically leaking
  // this object. However, its effectively part of static initialisation of
  // the process - it'll be freed on process exit - so it's not leaked in practice.
  std::string* syslog_identity = new std::string("chronos");

  // Open a connection to syslog. This is used for ENT logs.
  openlog(syslog_identity->c_str(), LOG_PID, LOG_LOCAL7);


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
  srand(time(NULL));

  // Initialize the global configuration. Creating the __globals object
  // updates the global configuration. It also creates an updater thread,
  // so this mustn't be created until after the process has daemonised.
  __globals = new Globals(options.local_config_file,
                          options.cluster_config_file,
                          options.shared_config_file);

  // Redirect stderr to chronos_err.log. This is done here and not in the call
  // to daemonize because we need to have __globals to know the logging folder.
  std::string logging_folder;
  __globals->get_logging_folder(logging_folder);
  std::string err = logging_folder + "/chronos_err.log";
  if (freopen(err.c_str(), "a", stderr) == NULL)
  {
    TRC_ERROR("Failed to redirect stderr");
    exit(0);
  }

  AlarmManager* alarm_manager = NULL;
  Alarm* resync_operation_alarm = NULL;
  CommunicationMonitor* remote_chronos_comm_monitor = NULL;
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
  alarm_manager = new AlarmManager();
  resync_operation_alarm = new Alarm(alarm_manager,
                                     "chronos",
                                     AlarmDef::CHRONOS_RESYNC_IN_PROGRESS,
                                     AlarmDef::MINOR);

  remote_chronos_comm_monitor = new CommunicationMonitor(new Alarm(alarm_manager,
                                                                   "chronos",
                                                                   AlarmDef::CHRONOS_REMOTE_CHRONOS_COMM_ERROR,
                                                                   AlarmDef::MAJOR),
                                                         "chronos",
                                                         "remote chronos");

  // Explicitly clear resynchronization alarm in case we died while the alarm
  // was still active, to ensure that the alarm is not then stuck in a set
  // state.
  resync_operation_alarm->clear();

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

  // We're going to need an HttpResolver both for our HTTP callbacks and for
  // our internal connections.  Create one.
  std::vector<std::string> dns_servers;
  __globals->get_dns_servers(dns_servers);
  int dns_timeout;
  __globals->get_dns_timeout(dns_timeout);
  int dns_port;
  __globals->get_dns_port(dns_port);
  DnsCachedResolver* dns_resolver = new DnsCachedResolver(dns_servers,
                                                          dns_timeout,
                                                          options.dns_config_file,
                                                          dns_port);


  // Create an Updater that listens for SIGUSR2 and, in response, reloads the
  // static DNS records
  Updater<void, DnsCachedResolver>* dns_updater =
    new Updater<void, DnsCachedResolver>(dns_resolver,
                                         std::mem_fun(&DnsCachedResolver::reload_static_records),
                                         &_sigusr2_handler,
                                         true);

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

  // Create the timer store, handlers, replicators...
  int gr_threads;
  bool replicate_timers_across_sites;
  __globals->get_gr_threads(gr_threads);
  __globals->get_replicate_timers_across_sites(replicate_timers_across_sites);

  TimerStore* store = new TimerStore(hc);
  Replicator* local_rep = new Replicator(http_resolver,
                                         exception_handler);

  // If the config option to replicate timers to other sites is set to false,
  // then set the GRReplicator to NULL, as it will never be needed.
  GRReplicator* gr_rep;
  if (replicate_timers_across_sites)
  {
    gr_rep = new GRReplicator(http_resolver,
                              exception_handler,
                              gr_threads,
                              remote_chronos_comm_monitor);
  }
  else
  {
    gr_rep = NULL;
  }

  HTTPCallback* callback = new HTTPCallback(http_resolver,
                                            exception_handler);
  TimerHandler* handler = new TimerHandler(store,
                                           callback,
                                           local_rep,
                                           gr_rep,
                                           all_timers_table,
                                           total_timers_table,
                                           scalar_timers_table);
  callback->start(handler);

  int target_latency;
  int max_tokens;
  int initial_token_rate;
  int min_token_rate;
  int max_token_rate;
  __globals->get_target_latency(target_latency);
  __globals->get_max_tokens(max_tokens);
  __globals->get_initial_token_rate(initial_token_rate);
  __globals->get_min_token_rate(min_token_rate);
  __globals->get_max_token_rate(max_token_rate);

  LoadMonitor* load_monitor = new LoadMonitor(target_latency,
                                              max_tokens,
                                              initial_token_rate,
                                              min_token_rate,
                                              max_token_rate);


  // Set up the HTTPStack and handlers
  int bind_port;
  int http_threads;
  __globals->get_bind_port(bind_port);
  __globals->get_threads(http_threads);

  HttpStack* http_stack = new HttpStack(http_threads,
                                        exception_handler,
                                        NULL,
                                        load_monitor,
                                        NULL);
  HttpStackUtils::PingHandler ping_handler;
  ControllerTask::Config controller_config(local_rep, gr_rep, handler);
  HttpStackUtils::SpawningHandler<ControllerTask, ControllerTask::Config> controller_handler(&controller_config,
                                                                                             &HttpStack::NULL_SAS_LOGGER);

  try
  {
    http_stack->initialize();
    http_stack->bind_tcp_socket(bind_address, bind_port);
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

  // Create a Chronos internal connection class for resynchronization operations.
  // We do this after creating the HTTPStack as it triggers a resync operation.
  ChronosInternalConnection* chronos_internal_connection =
            new ChronosInternalConnection(http_resolver,
                                          handler,
                                          local_rep,
                                          resync_operation_alarm,
                                          remaining_nodes_scalar,
                                          timers_processed_table,
                                          invalid_timers_processed_table);

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

  delete load_monitor; load_monitor = NULL;
  delete chronos_internal_connection; chronos_internal_connection = NULL;
  delete handler; handler = NULL;
  // Callback is deleted by the handler
  delete gr_rep; gr_rep = NULL;
  delete local_rep; local_rep = NULL;
  delete store; store = NULL;
  delete http_resolver; http_resolver = NULL;
  delete dns_updater; dns_updater = NULL;
  delete dns_resolver; dns_resolver = NULL;

  delete scalar_timers_table; scalar_timers_table = NULL;
  delete total_timers_table; total_timers_table = NULL;
  delete all_timers_table; all_timers_table = NULL;
  delete invalid_timers_processed_table; invalid_timers_processed_table = NULL;
  delete timers_processed_table; timers_processed_table = NULL;
  delete remaining_nodes_scalar; remaining_nodes_scalar = NULL;

  delete exception_handler; exception_handler = NULL;
  hc->stop_thread();
  delete hc; hc = NULL;

  // Delete Chronos's alarm object
  delete resync_operation_alarm;
  delete remote_chronos_comm_monitor;
  delete alarm_manager;
  delete http_stack; http_stack = NULL;

  sem_destroy(&term_sem);

  // After this point nothing will use __globals so it's safe to delete
  // it here.
  CL_CHRONOS_ENDED.log();
  delete __globals; __globals = NULL;
  curl_global_cleanup();

  return 0;
}
