#ifndef GLOBALS_H__
#define GLOBALS_H__

#include <pthread.h>
#include <string>
#include <map>
#include <vector>

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
  Globals();
  ~Globals();

  GLOBAL(local_ip, std::string);
  GLOBAL(cluster_hashes, std::map<std::string, uint64_t>);
  GLOBAL(cluster_addresses, std::vector<std::string>);

  void lock() { pthread_rwlock_wrlock(&_lock); }
  void unlock() { pthread_rwlock_unlock(&_lock); }

private:
  pthread_rwlock_t _lock;
};

extern Globals __globals;

#endif
