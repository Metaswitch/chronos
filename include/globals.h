/**
 * @file globals.h
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef GLOBALS_H__
#define GLOBALS_H__

#include <pthread.h>
#include <string>
#include <map>
#include <vector>
#include <boost/program_options.hpp>
#include "updater.h"

// Defines a global variable and it's associated get and set
// functions.  Note that, although get functions are protected
// by the lock automatically, set functions are not, allowing
// updates to be applied atomically (using the lock() and
// unlock() functions around the various set operations)..
#define GLOBAL(NAME, ...)                     \
  public:                                      \
    void get_##NAME(__VA_ARGS__& val) \
    { \
      pthread_rwlock_rdlock(&_lock); \
      val = _##NAME; \
      pthread_rwlock_unlock(&_lock); \
    } \
    void set_##NAME(const __VA_ARGS__ & val) \
    { \
      _##NAME = val; \
    } \
  private: \
    __VA_ARGS__ _##NAME

class Globals
{
public:
  Globals(std::string config_file,
          std::string cluster_config_file,
          std::string gr_config_file);
  ~Globals();
  Globals(const Globals& copy) = delete;

  enum struct TimerIDFormat
  {
    WITH_REPLICAS, WITHOUT_REPLICAS
  };

  // Per node configuration
  GLOBAL(bind_address, std::string);
  GLOBAL(bind_port, int);
  GLOBAL(threads, int);
  GLOBAL(max_ttl, int);
  GLOBAL(dns_servers, std::vector<std::string>);
  GLOBAL(target_latency, int);
  GLOBAL(max_tokens, int);
  GLOBAL(initial_token_rate, int);
  GLOBAL(min_token_rate, int);
  GLOBAL(timer_id_format, TimerIDFormat);

  // Clustering configuration
  GLOBAL(cluster_local_ip, std::string);
  GLOBAL(cluster_joining_addresses, std::vector<std::string>);
  GLOBAL(cluster_staying_addresses, std::vector<std::string>);
  GLOBAL(cluster_leaving_addresses, std::vector<std::string>);
  GLOBAL(new_cluster_hashes, std::vector<uint32_t>);
  GLOBAL(old_cluster_hashes, std::vector<uint32_t>);
  GLOBAL(cluster_bloom_filters, std::map<std::string, uint64_t>);
  GLOBAL(cluster_view_id, std::string);

  GLOBAL(instance_id, uint32_t);
  GLOBAL(deployment_id, uint32_t);

  // Geographic Redundancy configuration
  GLOBAL(local_site_name, std::string);
  GLOBAL(remote_sites, std::map<std::string, std::string>);
  GLOBAL(remote_site_names, std::vector<std::string>);
  GLOBAL(remote_site_dns_records, std::vector<std::string>);

public:
  void update_config();
  void lock() { pthread_rwlock_wrlock(&_lock); }
  void unlock() { pthread_rwlock_unlock(&_lock); }
  TimerIDFormat default_id_format() { return TimerIDFormat::WITHOUT_REPLICAS; }

private:
  uint64_t generate_bloom_filter(std::string);
  std::vector<uint32_t> generate_hashes(std::vector<std::string>);

  std::string _config_file;
  std::string _cluster_config_file;
  std::string _gr_config_file;
  pthread_rwlock_t _lock; 
  Updater<void, Globals>* _updater;
  boost::program_options::options_description _desc;

  std::map<TimerIDFormat, std::string> _timer_id_format_parser =
      {{TimerIDFormat::WITH_REPLICAS, "with_replicas"},
       {TimerIDFormat::WITHOUT_REPLICAS, "without_replicas"}};
};

extern Globals* __globals;

#endif
