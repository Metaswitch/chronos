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
static const char* const PARAM_REQUESTING_NODE = "requesting-node";
static const char* const PARAM_SYNC_MODE = "sync-mode";
static const char* const PARAM_CLUSTER_ID = "cluster-id";

// Parameter values
static const char* const PARAM_SYNC_MODE_VALUE_SCALE = "SCALE";

// Header values
static const char* const HEADER_RANGE = "Range";
static const char* const HEADER_CONTENT_RANGE = "Content-Range";


#endif
