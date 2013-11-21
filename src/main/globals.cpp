#include "globals.h"

Globals::Globals()
{
  pthread_rwlock_init(&_lock, NULL);
}

Globals::~Globals()
{
  pthread_rwlock_destroy(&_lock);
}

// The one and only global object
Globals __globals;
