/**
 * @file chronos_pd_definitions.h  Defines instances of PDLog for Chronos.
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
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

#ifndef _CHRONOS_PD_DEFINITIONS_H__
#define _CHRONOS_PD_DEFINITIONS_H__

#include <string>
#include "pdlog.h"

// Defines instances of PDLog for the chronos module

// The fields for each PDLog instance contains:
//   Identity - Identifies the log id to be used in the syslog id field.
//   Severity - One of Emergency, Alert, Critical, Error, Warning, Notice, 
//              and Info. Only LOG_ERROR or LOG_NOTICE are used.  
//   Message  - Formatted description of the condition.
//   Cause    - The cause of the condition.
//   Effect   - The effect the condition.
//   Action   - A list of one or more actions to take to resolve the condition 
//              if it is an error.
static const PDLog1<const char*> CL_CHRONOS_CRASHED
(
  PDLogBase::CL_CHRONOS_ID + 1,
  LOG_ERR,
  "Fatal - Chronos has exited or crashed with signal %s.",
  "Chronos has encountered a fatal software error or has been terminated.",
  "The application will exit and restart until the problem is fixed. If "
    "Chronos processes are running correctly on other servers in the cluster, "
    "they will provide service, although statistics may be incorrect.",
  "Ensure that Chronos has been installed correctly and that it "
  "has valid configuration."
);

static const PDLog CL_CHRONOS_STARTED
(
  PDLogBase::CL_CHRONOS_ID + 2,
  LOG_NOTICE,
  "Chronos started.",
  "The Chronos application has started.",
  "Normal.",
  "None."
);

static const PDLog CL_CHRONOS_HTTP_SERVICE_AVAILABLE
(
  PDLogBase::CL_CHRONOS_ID + 5,
  LOG_NOTICE,
  "Chronos HTTP service is now available.",
  "Chronos can now accept HTTP connections.",
  "Normal.",
  "None."
);

static const PDLog CL_CHRONOS_ENDED
(
  PDLogBase::CL_CHRONOS_ID + 6,
  LOG_ERR,
  "Fatal - Termination signal received - terminating.",
  "Chronos has been terminated by monit or has exited.",
  "Chronos timer service is not longer available. If Chronos processes are "
    "running correctly on other servers in the cluster, they will provide "
    "service, although statistics may be incorrect.",
  "(1). This occurs normally when Chronos is stopped. "
  "(2). If Chronos failed to respond then monit can restart Chronos."
);

static const PDLog1<const char*> CL_CHRONOS_NO_SYSTEM_TIME
(
  PDLogBase::CL_CHRONOS_ID + 7,
  LOG_ERR,
  "Fatal - Failed to get system time - timer service cannot run: %s.",
  "The Chronos time service cannot get the system time.",
  "The application will exit and restart until the problem is fixed. If "
    "Chronos processes are running correctly on other servers in the cluster, "
    "they will provide service, although statistics may be incorrect.",
  "(1). Make sure that NTP is running and the system time and date is set. "
  "(2). Check the NTP status and configuration."
);

static const PDLog2<const char*, int> CL_CHRONOS_HTTP_INTERFACE_FAIL
(
  PDLogBase::CL_CHRONOS_ID + 8,
  LOG_ERR,
  "Fatal - Failed to initialize HttpStack stack in function %s with error %d.",
  "The HTTP interfaces could not be initialized.",
  "The application will exit and restart until the problem is fixed. If "
    "Chronos processes are running correctly on other servers in the cluster, "
    "they will provide service, although statistics may be incorrect.",
  "(1). Check the /etc/clearwater/config for correctness. "
  "(2). Check the network status and configuration. "
);

static const PDLog2<const char*, int> CL_CHRONOS_HTTP_INTERFACE_STOP_FAIL
(
  PDLogBase::CL_CHRONOS_ID + 9,
  LOG_ERR,
  "The HTTP interfaces encountered an error when stopping the HTTP stack "
  "in %s with error %d.",
  "When Chronos was exiting it encountered an error when shutting "
  "down the HTTP stack.",
  "Not critical as Chronos is exiting anyway.",
  "No action required."
);

const static PDLog CL_CHRONOS_START_RESYNC
(
  PDLog::CL_CHRONOS_ID + 10,
  LOG_INFO,
  "Chronos has started a resync operation",
  "Chronos has detected an on-going cluster resize or Chronos process start "
  " and is proactively resynchronising timers between cluster members.",
  "Timers are being resynced across the Chronos cluster. Statistics may "
    "be temporarily incorrect.",
  "Wait until the current resync operation has completed before continuing "
    "with any cluster resize."
);

const static PDLog CL_CHRONOS_COMPLETE_RESYNC
(
  PDLog::CL_CHRONOS_ID + 11,
  LOG_INFO,
  "Chronos has completed a resync operation",
  "Chronos has synchronised all available data to the local node.",
  "The operation may be completed once all other Chronos instances have "
    "completed their resync operations.",
  "Once all other Chronos instances have completed their resync operations "
    "you may continue any cluster resize"
);

const static PDLog1<const char*> CL_CHRONOS_RESYNC_ERROR
(
  PDLog::CL_CHRONOS_ID + 12,
  LOG_ERR,
  "Chronos has failed to synchronise some data with the Chronos node at %s.",
  "Chronos was unable to fully synchronize with another Chronos.",
  "Not all timers have been resynchronised, completing any scaling action now "
    "may result in loss of timers or loss of redundancy",
  "Check the status of the Chronos cluster and ensure network connectivity "
    "is possible between all nodes."
);

const static PDLog2<int, int> CL_CHRONOS_CLUSTER_OLD_CFG_READ
(
  PDLog::CL_CHRONOS_ID + 13,
  LOG_NOTICE,
  "The Chronos cluster configuration has been loaded. There are now %d current members and %d leaving nodes.",
  "Chronos has reloaded its cluster configuration file.",
  "If necessary, timers will be resynced across the Chronos cluster.",
  "None."
);

const static PDLog3<int, int, int> CL_CHRONOS_CLUSTER_CFG_READ
(
  PDLog::CL_CHRONOS_ID + 14,
  LOG_NOTICE,
  "The Chronos cluster configuration has been loaded. There are %d joining nodes, %d staying nodes and %d leaving nodes.",
  "Chronos has reloaded its cluster configuration file.",
  "If necessary, timers will be resynced across the Chronos cluster.",
  "None."
);

#endif
