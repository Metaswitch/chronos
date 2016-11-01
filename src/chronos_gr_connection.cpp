/**
 * @file chronos_gr_connection.cpp.
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2016  Metaswitch Networks Ltd
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

#include "log.h"
#include "sasevent.h"
#include "chronos_gr_connection.h"

ChronosGRConnection::ChronosGRConnection(const std::string& remote_site,
                                         HttpResolver* resolver) :
  _site_name(remote_site),
  _http(new HttpConnection(remote_site,
                           false,
                           resolver,
                           SASEvent::HttpLogLevel::NONE,
                           NULL))
{
}

ChronosGRConnection::~ChronosGRConnection()
{
  delete _http; _http = NULL;
}

void ChronosGRConnection::send_put(std::string url,
                                   std::string body)
{
  HTTPCode rc = _http->send_put(url, body, 0);

  if (rc != HTTP_OK)
  {
    // LCOV_EXCL_START - No value in testing this log in UT
    TRC_ERROR("Unable to send replication to a remote site (%s)",
              _site_name.c_str());
    // LCOV_EXCL_STOP
  }
}
