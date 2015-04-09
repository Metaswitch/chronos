#include "globals.h"
#include "murmur/MurmurHash3.h"
#include "log.h"

#include <fstream>

// Shorten the imported namespace for ease of use.  Notice we don't do this in the
// header file to avoid infecting other compilation units' namespaces.
namespace po = boost::program_options;

// The one and only global object - this must be initialized at start of day and
// terminated before main() returns.
Globals* __globals;

Globals::Globals(std::string config_file) :
  _config_file(config_file)
{
  pthread_rwlock_init(&_lock, NULL);

  // Describe the configuration file format.
  _desc.add_options()
    ("http.bind-address", po::value<std::string>()->default_value("localhost"), "Address to bind the HTTP server to")
    ("http.bind-port", po::value<int>()->default_value(7253), "Port to bind the HTTP server to")
    ("http.default-bind-port", po::value<int>()->default_value(7253), "Port to send requests to other Chronos nodes (if not specified in the cluster settings")
    ("cluster.localhost", po::value<std::string>()->default_value("localhost:7253"), "The address of the local host")
    ("cluster.node", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(1, "localhost:7253"), "HOST"), "The addresses of nodes in the cluster")
    ("cluster.leaving", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(), "HOST"), "The addresses of nodes in the cluster that are leaving")
    ("logging.folder", po::value<std::string>()->default_value("/var/log/chronos"), "Location to output logs to")
    ("logging.level", po::value<int>()->default_value(2), "Logging level: 1(lowest) - 5(highest)")
    ("alarms.enabled", po::value<std::string>()->default_value("false"), "Whether SNMP alarms are enabled")
    ("http.threads", po::value<int>()->default_value(50), "Number of HTTP threads to create")
    ("exceptions.max_ttl", po::value<int>()->default_value(600), "Maximum time before the process exits after hitting an exception")
    ("dns.servers", po::value<std::vector<std::string>>()->multitoken()->default_value(std::vector<std::string>(1, "127.0.0.1"), "HOST"), "The addresses of the DNS servers used by the Chronos process")
    ;

#ifndef UNITTEST
  _updater = new Updater<void, Globals>(this, std::mem_fun(&Globals::update_config));
#endif
}

Globals::~Globals()
{
#ifndef UNITTEST
  delete _updater;
#endif
  pthread_rwlock_destroy(&_lock);
}

void Globals::update_config()
{
  std::ifstream file;
  boost::program_options::variables_map conf_map;

  file.open(_config_file);
  po::store(po::parse_config_file(file, _desc), conf_map);
  po::notify(conf_map);
  file.close();

  lock();

  // Set up logging early so we can log the other settings
  Log::setLogger(new Logger(conf_map["logging.folder"].as<std::string>(), "chronos"));
  Log::setLoggingLevel(conf_map["logging.level"].as<int>());

  std::string bind_address = conf_map["http.bind-address"].as<std::string>();
  set_bind_address(bind_address);
  LOG_STATUS("Bind address: %s", bind_address.c_str());

  int bind_port = conf_map["http.bind-port"].as<int>();
  set_bind_port(bind_port);
  LOG_STATUS("Bind port: %d", bind_port);

  int default_bind_port = conf_map["http.default-bind-port"].as<int>();
  set_default_bind_port(default_bind_port);
  LOG_STATUS("Default bind port: %d", default_bind_port);

  std::string cluster_local_address = conf_map["cluster.localhost"].as<std::string>();
  set_cluster_local_ip(cluster_local_address);
  LOG_STATUS("Cluster local address: %s", cluster_local_address.c_str());

  std::vector<std::string> cluster_addresses = conf_map["cluster.node"].as<std::vector<std::string>>();
  set_cluster_addresses(cluster_addresses);
  
  std::vector<uint32_t> cluster_rendezvous_hashes = generate_hashes(cluster_addresses);
  set_cluster_hashes(cluster_rendezvous_hashes);
 
  std::map<std::string, uint64_t> cluster_bloom_filters;
  uint64_t cluster_view_id = 0;

  LOG_STATUS("Cluster nodes:");
  for (std::vector<std::string>::iterator it = cluster_addresses.begin();
                                          it != cluster_addresses.end();
                                          ++it)
  {
    LOG_STATUS(" - %s", it->c_str());
    uint64_t bloom = generate_bloom_filter(*it);
    cluster_bloom_filters[*it] = bloom;
    cluster_view_id |= bloom;
  }
  set_cluster_bloom_filters(cluster_bloom_filters);

  std::string cluster_view_id_str = std::to_string(cluster_view_id);
  set_cluster_view_id(cluster_view_id_str);
  LOG_STATUS("Cluster view ID: %s", cluster_view_id_str.c_str());

  std::vector<std::string> cluster_leaving_addresses = conf_map["cluster.leaving"].as<std::vector<std::string>>();
  set_cluster_leaving_addresses(cluster_leaving_addresses);

  bool alarms_enabled = (conf_map["alarms.enabled"].as<std::string>().compare("true") == 0);
  set_alarms_enabled(alarms_enabled);
  LOG_STATUS("Alarms enabled: %d", alarms_enabled);

  int threads = conf_map["http.threads"].as<int>();
  set_threads(threads);
  LOG_STATUS("HTTP Threads: %d", threads);

  int ttl = conf_map["exceptions.max_ttl"].as<int>();
  set_max_ttl(ttl);
  LOG_STATUS("Maximum post-exception TTL: %d", ttl);

  std::vector<std::string> dns_servers = conf_map["dns.servers"].as<std::vector<std::string>>();
  set_dns_servers(dns_servers);

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
    while (std::find(ret.begin(), ret.end(), hash) != ret.end()) {
      hash--;
    }
    ret.push_back(hash);
  }

  return ret;
}

