/**
 * @file pthread_cond_var_helper.cpp
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

#include "pthread_cond_var_helper.h"

#include <gtest/gtest.h>
#include <cassert>
#include <errno.h>

MockPThreadCondVar::MockPThreadCondVar(pthread_mutex_t* mutex) : _state(SIGNALED),
                                                                 _mutex(mutex)
{
  pthread_condattr_t cond_attr;
  pthread_condattr_init(&cond_attr);
  pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
  pthread_cond_init(&_cond, &cond_attr);
  pthread_condattr_destroy(&cond_attr);
}

MockPThreadCondVar::~MockPThreadCondVar()
{
  pthread_cond_destroy(&_cond);
}

// Simply lock until the test code wakes us up.
int MockPThreadCondVar::wait()
{
  _state = WAIT;
  _timeout.tv_sec = 0;
  _timeout.tv_nsec = 0;
  pthread_cond_signal(&_cond);
  while (_state == WAIT)
  {
    pthread_cond_wait(&_cond, _mutex);
  }
  assert(_state == SIGNALED);
  return 0;
}

int MockPThreadCondVar::timedwait(struct timespec* ts)
{
  int rc;

  _state = TIMED_WAIT;
  _timeout = *ts;
  pthread_cond_signal(&_cond);
  while (_state == TIMED_WAIT)
  {
    pthread_cond_wait(&_cond, _mutex);
  }

  if (_state == TIMEDOUT)
  {
    rc = ETIMEDOUT;
    _state = SIGNALED;
  }
  else if (_state == SIGNALED)
  {
    rc = 0;
  }
  else
  {
    assert(!"Impossible cond var state");
  }
  return rc;
}

int MockPThreadCondVar::signal()
{
  _state = SIGNALED;
  pthread_cond_signal(&_cond);
  return 0;
}

/*****************************************************************************/
/* Test control functions.                                                   */
/*****************************************************************************/

void MockPThreadCondVar::block_till_waiting()
{
  lock();
  while (_state == SIGNALED || _state == TIMEDOUT)
  {
    pthread_cond_wait(&_cond, _mutex);
  }
  unlock();
}

// Consume a signal from the SUT, blocking till it is received.
void MockPThreadCondVar::block_till_signaled()
{
  lock();
  while (_state != SIGNALED)
  {
    pthread_cond_wait(&_cond, _mutex);
  }
  unlock();
}

void MockPThreadCondVar::check_timeout(const struct timespec& ts)
{
  lock();
  EXPECT_EQ(TIMED_WAIT, _state);
  EXPECT_EQ(ts.tv_sec, _timeout.tv_sec);
  EXPECT_EQ(ts.tv_nsec, _timeout.tv_nsec);
  unlock();
}

void MockPThreadCondVar::signal_wake()
{
  lock();
  EXPECT_EQ(TIMED_WAIT, _state);
  _state = SIGNALED;
  pthread_cond_signal(&_cond);
  unlock();
}

void MockPThreadCondVar::signal_timeout()
{
  lock();
  EXPECT_EQ(TIMED_WAIT, _state);
  _state = TIMEDOUT;
  pthread_cond_signal(&_cond);
  unlock();
}

void MockPThreadCondVar::lock() { pthread_mutex_lock(_mutex); }
void MockPThreadCondVar::unlock() { pthread_mutex_unlock(_mutex); }
