/**
 * @file cond_var.h
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

#ifndef COND_VAR_H__
#define COND_VAR_H__

#include <pthread.h>

// This class wraps a condition varable and mutex together and offers a C++ interface.
//
// Apart from the more C++-like interface benefit, this also allows us to mock out
// condition variables in the UT framework (see MockPThreadCondVar for the mock 
// implementation).
class CondVar
{
public:
  CondVar(pthread_mutex_t* mutex) : _mutex(mutex)
  {
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&_cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
  }
  ~CondVar() { pthread_cond_destroy(&_cond); }

  int wait() { return pthread_cond_wait(&_cond, _mutex); }
  int timedwait(struct timespec* ts) { return pthread_cond_timedwait(&_cond, _mutex, ts); }
  int signal() { return pthread_cond_signal(&_cond); }
  int broadcast() { return pthread_cond_broadcast(&_cond); }

private:
  pthread_cond_t _cond;
  pthread_mutex_t* _mutex;
};

#endif
