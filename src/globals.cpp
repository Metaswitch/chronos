/**
 * @file globals.cpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "globals.h"
#include "murmur/MurmurHash3.h"
#include "log.h"
#include "chronos_pd_definitions.h"
#include "utils.h"

#include <fstream>
#include <syslog.h>

// Shorten the imported namespace for ease of use.  Notice we don't do this in the
// header file to avoid infecting other compilation units' namespaces.
namespace po = boost::program_options;

// The one and only global object - this must be initialized at start of day and
// terminated before main() returns.
Globals* __globals;

Globals::Globals(std::string local_config_file,
                 std::string cluster_config_file,
                 std::string shared_config_file) :
  _local_config_file(local_config_file),
  _cluster_config_file(cluster_config_file),
  _shared_config_file(shared_config_file)
{
  pthread_rwlock_init(&_lock, NULL);

  // Describe the configuration file format.
  _desc.add_options()
    ("http.bind-address", po::value<std::string>()->default_value("0.0.0.0"), "Address to bind the HTTP server to")
    ("http.bind-port", po::value<int>()->default_value(7253), "Port to bind the HTTP server to")
    ("cluster.localhost", po::value<std::string>()->default_value("127.0.0.1:7253"), "The address of the local host")
    ("cluster.joining", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), "HOST"), "The addresses of nodes in the cluster that are joining")
    ("cluster.node", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), "HOST"), "The addresses of nodes in the cluster that are staying")
    ("cluster.leaving", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), "HOST"), "The addresses of nodes in the cluster that are leaving")
    ("identity.instance_id", po::value<uint32_t>()->default_value(0), "A number between 0 and 127. The combination of instance ID and deployment ID should uniquely identify this node in the cluster, to remove the risk of timer collisions.")
    ("identity.deployment_id", po::value<uint32_t>()->default_value(0), "A number between 0 and 7. The combination of instance ID and deployment ID should uniquely identify this node in the cluster, to remove the risk of timer collisions.")
    ("logging.folder", po::value<std::string>()->default_value("/var/log/chronos"), "Location to output logs to")
    ("logging.level", po::value<int>()->default_value(2), "Logging level: 1(lowest) - 5(highest)")
    ("http.threads", po::value<int>()->default_value(50), "Number of HTTP threads (for incoming requests) to create")
    ("http.gr_threads", po::value<int>()->default_value(50), "Number of HTTP threads (for GR replication) to create")
    ("exceptions.max_ttl", po::value<int>()->default_value(600), "Maximum time before the process exits after hitting an exception")
    ("sites.local_site", po::value<std::string>()->default_value("site1"), "The name of the local site")
    ("sites.remote_site", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), "SITE"), "The name and address of the remote sites in the cluster")
    ("throttling.target_latency", po::value<int>()->default_value(500000), "Target latency (in microseconds) for HTTP responses")
    ("throttling.max_tokens", po::value<int>()->default_value(1000), "Maximum token bucket size for HTTP overload control")
    ("throttling.initial_token_rate", po::value<int>()->default_value(500), "Initial token bucket refill rate for HTTP overload control")
    ("throttling.min_token_rate", po::value<int>()->default_value(10), "Minimum token bucket refill rate for HTTP overload control")
    ("throttling.max_token_rate", po::value<int>()->default_value(0), "Maximum token bucket refill rate for HTTP overload control")
    ("dns.servers", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(1, "127.0.0.1"), "HOST"), "The addresses of the DNS servers used by the Chronos process")
    ("dns.timeout", po::value<int>()->default_value(200), "The amount of time to wait for a DNS response")
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

static void parse_config_file(std::string& config_file,
                              po::variables_map& conf_map,
                              po::options_description _desc)
{
  std::ifstream file;
  file.open(config_file);

  // This is safe even if the config file doesn't exist. It sets up the default
  // values if the file doesn't exist, or if the config options aren't set.
  // We ignore any unrecognised config options.
  po::store(po::parse_config_file(file, _desc, true), conf_map);

  if (file.is_open())
  {
    file.close();
  }
}

void Globals::update_config()
{
  po::variables_map conf_map;

  // Read clustering config from _cluster_config_file, shared config
  // from _shared_config_file and local config from _local_config_file.
  std::string config_files[] = {_cluster_config_file,
                                _local_config_file,
                                _shared_config_file};
  for (std::string& config_file : config_files)
  {
    try
    {
      parse_config_file(config_file, conf_map, _desc);
    }
    // LCOV_EXCL_START
    catch (po::error& e)
    {
      fprintf(stderr, "Error parsing config file: %s \n %s",
              config_file.c_str(),
              e.what());
      exit(1);
    }
    // LCOV_EXCL_STOP

    po::notify(conf_map);
  }

  lock();

  // Set up the per node configuration. Set up logging early so we can
  // log the other settings
  std::string logging_folder = conf_map["logging.folder"].as<std::string>();
  set_logging_folder(logging_folder);

#ifndef UNIT_TEST
  Log::setLogger(new Logger(logging_folder, "chronos"));
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

  int gr_threads = conf_map["http.gr_threads"].as<int>();
  set_gr_threads(gr_threads);
  TRC_STATUS("HTTP GR Threads: %d", gr_threads);

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

  int max_token_rate = conf_map["throttling.max_token_rate"].as<int>();
  set_max_token_rate(max_token_rate);

  std::vector<std::string> dns_servers = conf_map["dns.servers"].as<std::vector<std::string>>();
  set_dns_servers(dns_servers);

  int dns_timeout = conf_map["dns.timeout"].as<int>();
  set_dns_timeout(dns_timeout);

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

  std::vector<std::string> cluster_leaving_addresses = conf_map["cluster.leaving"].as<std::vector<std::string>>();
  set_cluster_leaving_addresses(cluster_leaving_addresses);

  std::vector<std::string> cluster_staying_addresses = conf_map["cluster.node"].as<std::vector<std::string>>();

  // If there are no joining, staying or leaving addresses, add the local node
  // to the staying nodes
  if (cluster_staying_addresses.empty() &&
      cluster_leaving_addresses.empty() &&
      cluster_joining_addresses.empty())
  {
    cluster_staying_addresses.push_back(cluster_local_address);
  }

  set_cluster_staying_addresses(cluster_staying_addresses);

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

  // Store the Geographic Redundancy Sites
  std::string local_site_name = conf_map["sites.local_site"].as<std::string>();
  TRC_STATUS("Local site: %s", local_site_name.c_str());
  set_local_site_name(local_site_name);

  std::vector<std::string> remote_site_list = conf_map["sites.remote_site"].as<std::vector<std::string>>();
  std::map<std::string, std::string> remote_sites;
  std::vector<std::string> remote_site_names;
  std::vector<std::string> remote_site_dns_records;

  for (std::vector<std::string>::iterator it = remote_site_list.begin();
                                          it != remote_site_list.end();
                                          ++it)
  {
    std::vector<std::string> site_details;
    Utils::split_string(*it, '=', site_details, 0);

    if (site_details.size() != 2)
    {
      TRC_ERROR("Ignoring remote site: %s - Site must include name and address separated by =",
                it->c_str());
    }
    else if (site_details[0] == local_site_name)
    {
      TRC_DEBUG("Not adding remote site as it's the same as the local site name (%s)",
                local_site_name.c_str());
    }
    else
    {
      std::string remote_uri = Utils::uri_address(site_details[1], bind_port);
      TRC_STATUS("Configured remote site: %s=%s",
                 site_details[0].c_str(),
                 remote_uri.c_str());
      remote_sites[site_details[0]] = remote_uri;
      remote_site_names.push_back(site_details[0]);
      remote_site_dns_records.push_back(remote_uri);
    }
  }

  set_remote_sites(remote_sites);
  set_remote_site_names(remote_site_names);
  set_remote_site_dns_records(remote_site_dns_records);

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
