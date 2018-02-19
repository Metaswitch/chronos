/**
 * @file chronos_gr_connection.cpp.
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#include "log.h"
#include "sasevent.h"
#include "globals.h"
#include "chronos_gr_connection.h"

ChronosGRConnection::ChronosGRConnection(const std::string& remote_site,
                                         HttpResolver* resolver,
                                         BaseCommunicationMonitor* comm_monitor) :
  _site_name(remote_site),
  _comm_monitor(comm_monitor)
{
  std::string bind_address;
  __globals->get_bind_address(bind_address);
  _http = new HttpConnection(remote_site,
                             false,
                             resolver,
                             nullptr,
                             nullptr,
                             SASEvent::HttpLogLevel::NONE,
                             nullptr,
                             "http",
                             false,
                             true,
                             -1,
                             false,
                             "",
                             bind_address);
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

    if (_comm_monitor)
    {
      _comm_monitor->inform_failure();
    }
    // LCOV_EXCL_STOP
  }
  else
  {
    if (_comm_monitor)
    {
      _comm_monitor->inform_success();
    }
  }
}
