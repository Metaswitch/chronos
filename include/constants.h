/**
 * @file constants.h
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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
