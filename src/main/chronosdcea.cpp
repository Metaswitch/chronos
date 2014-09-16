/**
 * @file chronosdcea.cpp - Craft Description, Cause, Effect, and Action
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
#include <string>
#include "chronosdcea.h"
// Chronos syslog identities
/**********************************************************
/ log_id
/ severity
/ Description: (formatted)
/ Cause: 
/ Effect:
/ Action:
**********************************************************/
// Chronos syslog identities
PDLog1<const char*> CL_CHRONOS_CRASHED
{
  CL_CHRONOS_ID + 1,
  PDLOG_ERR,
  "Fatal - Chronos has exited or crashed with signal %s",
  "",
  "",
  ""
};
PDLog CL_CHRONOS_STARTED
{
  CL_CHRONOS_ID + 2,
  PDLOG_NOTICE,
  "Chronos started",
  "The Chronos application has started.",
  "Normal",
  "None"
};
PDLog CL_CHRONOS_REACTOR_FAIL
{
  CL_CHRONOS_ID + 3,
  PDLOG_ERR,
  "Fatal - Couldn't create the event reactor service",
  "The event handler for Chronos could not be initialized.",
  "The Chronos application will exit.",
  "Report this issue."
};
PDLog CL_CHRONOS_FAIL_CREATE_HTTP_SERVICE
{
  CL_CHRONOS_ID + 4,
    PDLOG_ERR,
    "Fatal - Could not create an http service",
    "The HTTP service could not be started",
    "The Chronos application will exit.",
    ""
};
PDLog CL_CHRONOS_HTTP_SERVICE_AVAILABLE
{
  CL_CHRONOS_ID + 5,
  PDLOG_NOTICE,
  "Chronos http service is now available",
  "",
  "",
  ""
};
PDLog CL_CHRONOS_ENDED
{
  CL_CHRONOS_ID + 6,
  PDLOG_ERR,
  "Fatal - Termination signal received - terminating",
  "",
  "",
  ""
};
PDLog1<const char*> CL_CHRONOS_NO_SYSTEM_TIME
{
  CL_CHRONOS_ID + 7,
  PDLOG_ERR,
  "Fatal - Failed to get system time - timer service cannot run: %s",
  "The Chronos time service cannot get the system time",
  "",
  ""
};
