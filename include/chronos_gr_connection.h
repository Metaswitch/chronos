/**
 * @file chronos_gr_connection.h
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef CHRONOS_GR_CONNECTION_H__
#define CHRONOS_GR_CONNECTION_H__

#include "httpconnection.h"

/// @class ChronosGRConnection
///
/// Responsible for sending replication requests between sites. Each connection
/// is responsible for replication to a single remote site.
class ChronosGRConnection
{
public:
  ChronosGRConnection(const std::string& remote_site,
                      HttpResolver* resolver,
                      BaseCommunicationMonitor* comm_monitor = NULL);
  virtual ~ChronosGRConnection();

  // Replicate the timer cross-site.
  virtual void send_put(std::string url,
                        std::string body);

private:
  std::string _site_name;
  HttpConnection* _http;
  BaseCommunicationMonitor* _comm_monitor;
};

#endif
