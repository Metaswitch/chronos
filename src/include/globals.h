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
    void set_##NAME(__VA_ARGS__ & val) \
    { \
      _##NAME = val; \
    } \
  private: \
    __VA_ARGS__ _##NAME

class Globals
{
public:
  Globals(std::string config_file);
  ~Globals();

  GLOBAL(bind_address, std::string);
  GLOBAL(bind_port, int);
  GLOBAL(cluster_local_ip, std::string);
  GLOBAL(cluster_bloom_filters, std::map<std::string, uint64_t>);
  GLOBAL(cluster_addresses, std::vector<std::string>);
  GLOBAL(cluster_hashes, std::vector<uint32_t>);
  GLOBAL(cluster_leaving_addresses, std::vector<std::string>);
  GLOBAL(alarms_enabled, bool);
  GLOBAL(threads, int);
  GLOBAL(max_ttl, int);

public:
  void update_config();
  void lock() { pthread_rwlock_wrlock(&_lock); }
  void unlock() { pthread_rwlock_unlock(&_lock); }

private:
  uint64_t generate_bloom_filter(std::string);
  std::vector<uint32_t> generate_hashes(std::vector<std::string>);

  std::string _config_file;
  pthread_rwlock_t _lock; 
  Updater<void, Globals>* _updater;
  boost::program_options::options_description _desc;
};

extern Globals* __globals;

#endif
