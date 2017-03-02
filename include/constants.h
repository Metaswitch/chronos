/**
 * @file constants.h
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2015  Metaswitch Networks Ltd
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

#ifndef CONSTANTS_H__
#define CONSTANTS_H__

// Maximum number of responses
static const int MAX_TIMERS_IN_RESPONSE = 100;

// JSON values
static const char* const JSON_TIMERS = "Timers";
static const char* const JSON_TIMER = "Timer";
static const char* const JSON_IDS = "IDs";
static const char* const JSON_ID = "ID";
static const char* const JSON_TIMER_ID = "TimerID";
static const char* const JSON_REPLICA_INDEX = "ReplicaIndex";
static const char* const JSON_OLD_REPLICAS = "OldReplicas";

// Parameters
static const char* const PARAM_NODE_FOR_REPLICAS = "node-for-replicas";
static const char* const PARAM_TIME_FROM = "time-from";
static const char* const PARAM_CLUSTER_VIEW_ID = "cluster-view-id";

// Header values
static const char* const HEADER_RANGE = "Range";
static const char* const HEADER_CONTENT_RANGE = "Content-Range";


#endif
