/**
 * @file globals.cpp
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

#include "globals.h"
#include "murmur/MurmurHash3.h"
#include "log.h"
#include "chronos_pd_definitions.h"

#include <fstream>
#include <syslog.h>

// Shorten the imported namespace for ease of use.  Notice we don't do this in the
// header file to avoid infecting other compilation units' namespaces.
namespace po = boost::program_options;

// The one and only global object - this must be initialized at start of day and
// terminated before main() returns.
Globals* __globals;

Globals::Globals(std::string config_file,
                 std::string cluster_config_file) :
  _config_file(config_file),
  _cluster_config_file(cluster_config_file)
{
  pthread_rwlock_init(&_lock, NULL);

  // Describe the configuration file format.
  _desc.add_options()
    ("http.bind-address", po::value<std::string>()->default_value("0.0.0.0"), "Address to bind the HTTP server to")
    ("http.bind-port", po::value<int>()->default_value(7253), "Port to bind the HTTP server to")
    ("cluster.localhost", po::value<std::string>()->default_value("localhost:7253"), "The address of the local host")
    ("cluster.joining", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), "HOST"), "The addresses of nodes in the cluster that are joining")
    ("cluster.node", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(1, "localhost:7253"), "HOST"), "The addresses of nodes in the cluster that are staying")
    ("cluster.leaving", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), "HOST"), "The addresses of nodes in the cluster that are leaving")
    ("identity.instance_id", po::value<uint32_t>()->default_value(0), "A number between 0 and 127. The combination of instance ID and deployment ID should uniquely identify this node in the cluster, to remove the risk of timer collisions.")
    ("identity.deployment_id", po::value<uint32_t>()->default_value(0), "A number between 0 and 7. The combination of instance ID and deployment ID should uniquely identify this node in the cluster, to remove the risk of timer collisions.")
    ("logging.folder", po::value<std::string>()->default_value("/var/log/chronos"), "Location to output logs to")
    ("logging.level", po::value<int>()->default_value(2), "Logging level: 1(lowest) - 5(highest)")
    ("http.threads", po::value<int>()->default_value(50), "Number of HTTP threads to create")
    ("exceptions.max_ttl", po::value<int>()->default_value(600), "Maximum time before the process exits after hitting an exception")
    ("throttling.target_latency", po::value<int>()->default_value(500000), "Target latency (in microseconds) for HTTP responses")
    ("throttling.max_tokens", po::value<int>()->default_value(1000), "Maximum token bucket size for HTTP overload control")
    ("throttling.initial_token_rate", po::value<int>()->default_value(500), "Initial token bucket refill rate for HTTP overload control")
    ("throttling.min_token_rate", po::value<int>()->default_value(10), "Minimum token bucket refill rate for HTTP overload control")
    ("dns.servers", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(1, "127.0.0.1"), "HOST"), "The addresses of the DNS servers used by the Chronos process")
    ("timers.id-format", po::value<std::string>()->default_value(_timer_id_format_parser.at(default_id_format())), "The format of the timer IDs")

    // Deprecated option left in for backwards compatibility
    ("alarms.enabled", po::value<std::string>()->default_value("false"), "Whether SNMP alarms are enabled")
    ;

#ifndef UNIT_TEST
  _updater = new Updater<void, Globals>(this, std::mem_fun(&Globals::update_config));
#else
  _updater = NULL;
#endif
}

Globals::~Globals()
{
#ifndef UNIT_TEST
  delete _updater;
#endif
  pthread_rwlock_destroy(&_lock);
}

void Globals::update_config()
{
  std::ifstream file;
  po::variables_map conf_map;

  // Read clustering config from _cluster_config_file and other config from
  // _config_file. Any remaining unset configuration options will be set to
  // their default values defined above when notify is called.
  file.open(_cluster_config_file);
  if (file.is_open())
  {
    po::store(po::parse_config_file(file, _desc), conf_map);
    file.close();
  }

  file.open(_config_file);
  // This is safe even if the config file doesn't exist, and this also sets up
  // the default values if the file doesn't exist, or for any config options
  // that aren't set in the file.
  po::store(po::parse_config_file(file, _desc), conf_map);
  po::notify(conf_map);

  if (file.is_open())
  {
    file.close();
  }

  lock();

  // Copy the program name to a string so that we can be sure of its lifespan -
  // the memory passed to openlog must be valid for the duration of the program.
  //
  // Note that we don't save syslog_identity here, and so we're technically leaking
  // this object. However, its effectively part of static initialisation of
  // the process - it'll be freed on process exit - so it's not leaked in practice.
  std::string* syslog_identity = new std::string("chronos");

  // Open a connection to syslog. This is used for ENT logs.
  openlog(syslog_identity->c_str(), LOG_PID, LOG_LOCAL7);

  // Set up the per node configuration. Set up logging early so we can 
  // log the other settings
#ifndef UNIT_TEST
  Log::setLogger(new Logger(conf_map["logging.folder"].as<std::string>(), "chronos"));
  Log::setLoggingLevel(conf_map["logging.level"].as<int>());
#endif

  std::string bind_address = conf_map["http.bind-address"].as<std::string>();
  set_bind_address(bind_address);
  TRC_STATUS("Bind address: %s", bind_address.c_str());

  int bind_port = conf_map["http.bind-port"].as<int>();
  set_bind_port(bind_port);
  TRC_STATUS("Bind port: %d", bind_port);

  int threads = conf_map["http.threads"].as<int>();
  set_threads(threads);
  TRC_STATUS("HTTP Threads: %d", threads);

  int ttl = conf_map["exceptions.max_ttl"].as<int>();
  set_max_ttl(ttl);
  TRC_STATUS("Maximum post-exception TTL: %d", ttl);

  int target_latency = conf_map["throttling.target_latency"].as<int>();
  set_target_latency(target_latency);

  int max_tokens = conf_map["throttling.max_tokens"].as<int>();
  set_max_tokens(max_tokens);

  int initial_token_rate = conf_map["throttling.initial_token_rate"].as<int>();
  set_initial_token_rate(initial_token_rate);

  int min_token_rate = conf_map["throttling.min_token_rate"].as<int>();
  set_min_token_rate(min_token_rate);

  std::vector<std::string> dns_servers = conf_map["dns.servers"].as<std::vector<std::string>>();
  set_dns_servers(dns_servers);

  std::string timer_id_format_str = conf_map["timers.id-format"].as<std::string>();
  TimerIDFormat timer_id_format = default_id_format();

  for (std::map<TimerIDFormat, std::string>::iterator it = _timer_id_format_parser.begin();
                                                      it != _timer_id_format_parser.end();
                                                      ++it)
  {
    if (it->second == timer_id_format_str)
    {
      timer_id_format = it->first;
      break;
    }
  }

  TRC_STATUS("%s", _timer_id_format_parser.at(timer_id_format).c_str());
  set_timer_id_format(timer_id_format);

  uint32_t instance_id = conf_map["identity.instance_id"].as<uint32_t>();
  uint32_t deployment_id = conf_map["identity.deployment_id"].as<uint32_t>();
  
  set_instance_id(instance_id);
  set_deployment_id(deployment_id);

  TRC_STATUS("Instance ID is %d, deployment ID is %d", instance_id, deployment_id);

  std::string cluster_local_address = conf_map["cluster.localhost"].as<std::string>();
  set_cluster_local_ip(cluster_local_address);
  TRC_STATUS("Cluster local address: %s", cluster_local_address.c_str());

  std::vector<std::string> cluster_joining_addresses = conf_map["cluster.joining"].as<std::vector<std::string>>();
  set_cluster_joining_addresses(cluster_joining_addresses);

  std::vector<std::string> cluster_staying_addresses = conf_map["cluster.node"].as<std::vector<std::string>>();
  set_cluster_staying_addresses(cluster_staying_addresses);

  std::vector<std::string> cluster_leaving_addresses = conf_map["cluster.leaving"].as<std::vector<std::string>>();
  set_cluster_leaving_addresses(cluster_leaving_addresses);

  // Figure out the new cluster by combining the nodes that are staying and the
  // nodes that are joining.
  std::vector<std::string> new_cluster_addresses = cluster_staying_addresses;
  new_cluster_addresses.insert(new_cluster_addresses.end(),
                               cluster_joining_addresses.begin(),
                               cluster_joining_addresses.end());
  std::vector<uint32_t> new_cluster_rendezvous_hashes = generate_hashes(new_cluster_addresses);
  set_new_cluster_hashes(new_cluster_rendezvous_hashes);
  
  // Figure out the old cluster by combining the nodes that are staying and the
  // nodes that are leaving.
  std::vector<std::string> old_cluster_addresses = cluster_staying_addresses;
  old_cluster_addresses.insert(old_cluster_addresses.end(),
                               cluster_leaving_addresses.begin(),
                               cluster_leaving_addresses.end());
  std::vector<uint32_t> old_cluster_rendezvous_hashes = generate_hashes(old_cluster_addresses);
  set_old_cluster_hashes(old_cluster_rendezvous_hashes);
 
  std::map<std::string, uint64_t> cluster_bloom_filters;
  uint64_t cluster_view_id = 0;

  TRC_STATUS("Staying nodes:");
  for (std::vector<std::string>::iterator it = cluster_staying_addresses.begin();
                                          it != cluster_staying_addresses.end();
                                          ++it)
  {
    TRC_STATUS(" - %s", it->c_str());
  }

  TRC_STATUS("Joining nodes:");
  for (std::vector<std::string>::iterator it = cluster_joining_addresses.begin();
                                          it != cluster_joining_addresses.end();
                                          ++it)
  {
    TRC_STATUS(" - %s", it->c_str());
  }

  for (std::vector<std::string>::iterator it = new_cluster_addresses.begin();
                                          it != new_cluster_addresses.end();
                                          ++it)
  {
    uint64_t bloom = generate_bloom_filter(*it);
    cluster_bloom_filters[*it] = bloom;
    cluster_view_id |= bloom;
  }

  set_cluster_bloom_filters(cluster_bloom_filters);

  std::string cluster_view_id_str = std::to_string(cluster_view_id);
  set_cluster_view_id(cluster_view_id_str);
  TRC_STATUS("Cluster view ID: %s", cluster_view_id_str.c_str());

  CL_CHRONOS_CLUSTER_CFG_READ.log(cluster_joining_addresses.size(),
                                  cluster_staying_addresses.size(),
                                  cluster_leaving_addresses.size());

  unlock();
}

// Generates the pre-calculated bloom filter for the given string.
//
// Create 3 128-bit hashes, modulo each half down to 0..63 and set those
// bits in the returned value.  In general this will set ~6 bits in the
// returned hash.
uint64_t Globals::generate_bloom_filter(std::string data)
{
  uint64_t hash[2];
  uint64_t rc = 0;
  for (int ii = 0; ii < 3; ++ii)
  {
    MurmurHash3_x86_128(data.c_str(), data.length(), ii, (void*)hash);
    for (int jj = 0; jj < 2; ++jj)
    {
      int bit = hash[jj] % 64;
      rc |= ((uint64_t)1 << bit);
    }
  }

  return rc;
}

std::vector<uint32_t> Globals::generate_hashes(std::vector<std::string> data)
{
  std::vector<uint32_t> ret;
  for (size_t ii = 0; ii < data.size(); ++ii)
  {
    uint32_t hash;
    MurmurHash3_x86_32(data[ii].c_str(), data[ii].length(), 0, &hash);

    // If we have hash collisions, modify the hash (we decrement it,
    // but any arbitrary modification is valid) until it is unique.
    while (std::find(ret.begin(), ret.end(), hash) != ret.end()) {
      hash--;
    }
    ret.push_back(hash);
  }

  return ret;
}
