/**
 * @file replicator.h
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

#ifndef REPLICATOR_H__
#define REPLICATOR_H__

#include <curl/curl.h>

#include "timer.h"
#include "exception_handler.h"
#include "eventq.h"

#define REPLICATOR_THREAD_COUNT 50

struct ReplicationRequest
{
  std::string url;
  std::string body;
};

// This class is used to replicate timers to the specified replicas, using cURL
// to handle the HTTP construction and sending.
class Replicator
{
public:
  Replicator(ExceptionHandler* exception_handler);
  virtual ~Replicator();

  void worker_thread_entry_point();
  virtual void replicate(Timer*);
  virtual void replicate_timer_to_node(Timer* timer,
                                       std::string node);

  static void* worker_thread_entry_point(void*);

private:
  void replicate_int(const std::string&, const std::string&);
  eventq<ReplicationRequest *> _q;
  pthread_t _worker_threads[REPLICATOR_THREAD_COUNT];
  struct curl_slist* _headers;
  ExceptionHandler* _exception_handler;
};

#endif
