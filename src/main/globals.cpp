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

Globals::Globals()
{
  pthread_rwlock_init(&_lock, NULL);
  
  // Describe the configuration file format.
  _desc.add_options()
    ("http.bind-address", po::value<std::string>()->default_value("0.0.0.0"), "Address to bind the HTTP server to")
    ("http.bind-port", po::value<int>()->default_value(7253), "Port to bind the HTTP server to")
    ("cluster.localhost", po::value<std::string>()->default_value("localhost"), "The address of the local host")
    ("cluster.node", po::value<std::vector<std::string>>()->multitoken(), "The addresses of a node in the cluster")
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
  file.open(CONFIG_FILE);
  _conf_map.clear();
  po::store(po::parse_config_file(file, _desc), _conf_map);
  po::notify(_conf_map);
  file.close();

  lock();
  std::string bind_address = _conf_map["http.bind-address"].as<std::string>();
  set_bind_address(bind_address);
  LOG_INFO("Bind address: %s", bind_address.c_str());

  int bind_port = _conf_map["http.bind-port"].as<int>();
  set_bind_port(bind_port);
  LOG_INFO("Bind port: %d", bind_port);

  std::string cluster_local_address = _conf_map["cluster.localhost"].as<std::string>();
  set_cluster_local_ip(cluster_local_address);
  LOG_INFO("Cluster local address: %s", cluster_local_address.c_str());
  
  std::vector<std::string> cluster_addresses = _conf_map["cluster.node"].as<std::vector<std::string>>();
  set_cluster_addresses(cluster_addresses);
  std::map<std::string, uint64_t> cluster_hashes;
  LOG_INFO("Cluster nodes:");
  for (auto it = cluster_addresses.begin(); it != cluster_addresses.end(); it++)
  {
    LOG_INFO(" - %s", it->c_str());
    cluster_hashes[*it] = generate_hash(*it);
  }
  set_cluster_hashes(cluster_hashes);
  unlock();
}

// Generates the pre-calculated bloom filter for the given string.
//
// Create 3 128-bit hashes, modulo each half down to 0..63 and set those
// bits in the returned value.  In general this will set ~6 bits in the 
// returned hash.
uint64_t Globals::generate_hash(std::string data)
{
  uint64_t hash[2];
  uint64_t rc = 0;
  for (int ii = 0; ii < 3; ii++)
  {
    MurmurHash3_x86_128(data.c_str(), data.length(), ii, (void*)hash);
    for (int jj = 0; jj < 2; jj++)
    {
      int bit = hash[jj] % 64;
      rc |= ((uint64_t)1 << bit);
    }
  }

  return rc;
}
