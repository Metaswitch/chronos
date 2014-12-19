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
//              and Info.  Directly corresponds to the syslog severity types.
//              Only PDLOG_ERROR or PDLOG_NOTICE are used.  
//              See syslog_facade.h for definitions.
//   Message  - Formatted description of the condition.
//   Cause    - The cause of the condition.
//   Effect   - The effect the condition.
//   Action   - A list of one or more actions to take to resolve the condition 
//              if it is an error.
static const PDLog1<const char*> CL_CHRONOS_CRASHED
(
  PDLogBase::CL_CHRONOS_ID + 1,
  PDLOG_ERR,
  "Fatal - Chronos has exited or crashed with signal %s.",
  "Chronos has encountered a fatal software error or has been terminated.",
  "The application will exit and restart until the problem is fixed.",
  "This error can occur if Chronos has been terminated by operator command. "
  "Check your installation and configuration for other types of crashes."
);

static const PDLog CL_CHRONOS_STARTED
(
  PDLogBase::CL_CHRONOS_ID + 2,
  PDLOG_NOTICE,
  "Chronos started.",
  "The Chronos application has started.",
  "Normal.",
  "None."
);

static const PDLog CL_CHRONOS_REACTOR_FAIL
(
  PDLogBase::CL_CHRONOS_ID + 3,
  PDLOG_ERR,
  "Fatal - Chronos encountered a fatal error on startup.",
  "The event handler for Chronos could not be initialized.",
  "The application will exit and restart until the problem is fixed.",
  "Ensure that Chronos has been installed correctly and that it "
  "has a valid configuration."
);

static const PDLog CL_CHRONOS_FAIL_CREATE_HTTP_SERVICE
(
  PDLogBase::CL_CHRONOS_ID + 4,
  PDLOG_ERR,
  "Fatal - Could not create an HTTP service.",
  "The HTTP service could not be started.",
  "The application will exit and restart until the problem is fixed.",
  "(1). Check the Chronos configuration."
  "(2). Check the network status and configuration."
);

static const PDLog CL_CHRONOS_HTTP_SERVICE_AVAILABLE
(
  PDLogBase::CL_CHRONOS_ID + 5,
  PDLOG_NOTICE,
  "Chronos HTTP service is now available.",
  "Chronos can now accept HTTP connections.",
  "Normal.",
  "None."
);

static const PDLog CL_CHRONOS_ENDED
(
  PDLogBase::CL_CHRONOS_ID + 6,
  PDLOG_ERR,
  "Fatal - Termination signal received - terminating.",
  "Chronos has been terminated by monit or has exited.",
  "Chronos timer service is not longer available.",
  "(1). This occurs normally when Chronos is stopped. "
  "(2). If Chronos failed to respond then monit can restart Chronos."
);

static const PDLog1<const char*> CL_CHRONOS_NO_SYSTEM_TIME
(
  PDLogBase::CL_CHRONOS_ID + 7,
  PDLOG_ERR,
  "Fatal - Failed to get system time - timer service cannot run: %s.",
  "The Chronos time service cannot get the system time.",
  "The application will exit and restart until the problem is fixed.",
  "(1). Make sure that ntp is running and the system time and date is set. "
  "(2). Check the ntp status and configuration."
);

#endif
