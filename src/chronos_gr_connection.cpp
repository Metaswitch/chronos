/**
 * @file chronos_gr_connection.cpp.
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
