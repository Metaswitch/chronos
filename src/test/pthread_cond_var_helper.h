/**
 * @file pthread_cond_var_helper.h
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

#ifndef PTHREAD_COND_VAR_HELPER_H__
#define PTHREAD_COND_VAR_HELPER_H__

#include <pthread.h>

enum STATE { WAIT, TIMED_WAIT, SIGNALED, TIMEDOUT };

class MockPThreadCondVar
{
public:
  MockPThreadCondVar(pthread_mutex_t *mutex);
  ~MockPThreadCondVar();

  // Mirror of CondVar
  int wait();
  int timedwait(struct timespec*);
  int signal();

  // Test script functions
  bool check_signaled();
  void block_till_waiting();
  void block_till_signaled();
  void check_timeout(const struct timespec&);
  void lock();
  void unlock();
  void signal_wake();
  void signal_timeout();

private:

  STATE _state;
  struct timespec _timeout;
  bool _signaled;
  pthread_mutex_t* _mutex;
  pthread_cond_t _cond;
};

#endif
