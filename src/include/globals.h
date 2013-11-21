#ifndef GLOBALS_H__
#define GLOBALS_H__

#include <pthread.h>
#include <string>
#include <map>
#include <vector>

#define GLOBAL(NAME, ...)                     \
  public:                                      \
    __VA_ARGS__ get_##NAME() \
    { \
      pthread_rwlock_rdlock(&_lock); \
      __VA_ARGS__ rc = _##NAME; \
      pthread_rwlock_unlock(&_lock); \
      return rc; \
    } \
  private: \
    __VA_ARGS__ _##NAME

class Globals
{
public:
  Globals();
  ~Globals();

  GLOBAL(local_ip, std::string);
  GLOBAL(cluster_hashes, std::map<std::string, uint64_t>);
  GLOBAL(cluster_addresses, std::vector<std::string>);

private:
  pthread_rwlock_t _lock;
};

extern Globals __globals;

#endif
