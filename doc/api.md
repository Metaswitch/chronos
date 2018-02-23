# Timer Service API

This document covers the external interface exposed by the Chronos network timer service.

## Introduction

The timer service runs like a timer heap: clients request a timer to pop after a certain interval, or to pop regularly at a given rate over a given interval, and the timer service notifies the client (through a client-supplied callback) when the timer pops.

## Public API Definition

### Adding and modifying timers

The timer service exposes a single HTTP API for the setting and clearing of timers:

    POST /timers
    PUT /timers/<timer-id>
    DELETE /timers/<timer-id>

To set a timer, simply `POST` the definition of the timer to the above URI.  To update an existing timer, `PUT` to the specific timer URI.  To delete a timer, `DELETE` the timer ID.

The timer service is designed to be distributed across multiple nodes in a cluster. In this case a client may set or clear any timer on any node of the cluster, helpful for handling node failures.

#### Request (POST/PUT)

The body of the request should be a JSON block in the following format:

    {
      "timing": {
        "interval": <secs>,
        "repeat-for": <secs>
      },
      "callback": {
        "http": {
          "uri": <callback-uri>,
          "opaque": <opaque-data>
        }
      },
      "reliability": {
        "replication-factor": <n>
      },
      "statistics": {
        "tag-info": [
          {"type": <"TAG 1">, "count": <n>},
          {"type": <"TAG 2">, "count": <n>}
        ]
      }
    }

##### Timing

The `"timing"` section of the request defines how often the timer should pop (in seconds), and how long to recur the timer for (also in seconds).

The `"interval"` attribute is mandatory and the request will be rejected unless it is filled in.

The `"repeat-for"` attribute may be specified to configure the timer service to repeatedly pop the timer every interval for the given length of time.  It is not possible to specify a recurring timer that never ends, to prevent the case where the trigger to cancel the timer is lost due to an outage leading to a leaked timer.  Once the `"repeat-for"` interval has passed, the timer is deleted from the service automatically.  If a timer would pop exactly at the end of the repeat-for interval, it will be scheduled (e.g. if the `"interval"` and `"repeat-for"` are equal, the timer will pop once).

If no `"repeat-for"` attribute is specified, the timer only fires once, after `"interval"` seconds.

_Note that, if `"repeat-for"` is strictly lower than the interval, the timer will never fire.  This use case is indicative of a logical error on the part of the client._

##### Callback

When the timer pops, the client will be notified though the callback mechanism specified here.  Currently only `"http"` is supported as a callback mechanism and specifying any other callback mechanism will result in your request being rejected.

The `"http"` callback takes two attributes, a URL to query and a block of textual opaque data to include in the callback request as a body. The callback request will be built simply as:

    POST <uri> HTTP/1.1
    Host: <uri host part>
    Content-Length: <len>
    X-Sequence-Number: <n>

    <opaque data>

Where the `X-Sequence-Number` is a monotonically incrementing sequence number for timer pops within one configured sequence of pops and can be used to detect duplicate pops during net-split recovery or as a way of enumerating the pops for other means (since the opaque data is identical for each pop).

To specify binary data as the opaque data, we recommend encoding it in Base64 on the request and decoding it on the response.

The HTTP callback must complete within 2 seconds of the request being sent by the timer service.  This is crucial to how the redundancy mechanism works in the timer service.  If the callback cannot complete in 2 seconds, it should report success/failure asynchronously to ensure that consistency is upheld.

##### Reliability

The reliability attribute is an optional parameter that may be used to specify how many replicas of the timer to create to handle outages of nodes in the cluster.

If your `replication-factor` is `n` then unless `n` or more nodes are unavailable simultaneously, all configured timers will still continue to pop, thus a higher replication factor increases the reliability of the timer service.  Configuring a replication factor greater than the current number of nodes in the cluster will be treated as specifying exactly the number of nodes configured.

The disadvantage of larger replication factors are:

* Higher capacity requirements on the timer nodes, causing worse scalability of each node.
* Larger number of duplicated pops in the case of a net-split (you may see up to `n` duplicates each pop).
* Longer delays between the requested interval and the time of the actual pop (up to `2 sec * (n - 1)` in some failure cases).

The default value for the replication factor (if unspecified) is `2`. The replication factor cannot be changed once the timer has been created.

##### Statistics

The `"statistics"` object is an optional parameter that may be used to provide tags holding information on what a timer represents. These tags are then used to generate stateful statistics in Chronos. The `"count"` value is also optional, defaulting to 1, but if present it must be a positive integer.

Further information on stateful statistics, and how to query them can be found [here.](https://clearwater.readthedocs.io/en/stable/Clearwater_Stateful_Statistics/index.html)
Information on the statistics structures that enable these statistics can be found [here.](statistics_structures.md)

#### Request (DELETE)

No body need be provided and will be ignored if it is.  Repeated deletion of a timer ID is as idempotent as possible. IDs may be reused extremely rarely (if more than 4096 requests are made to the same node within 1 millisecond, or if requests are made over a period of 147 years) so careless deletes should be avoided if possible to minimize the chance of deleting a timer created by some other client.

#### Response (POST/PUT)

If the timer creation/update was successful, the response to the POST/PUT request will be a standard HTTP `200 OK` with a `Location` header containing the timer ID that may be used to refresh or cancel the configured timer.

_Note: Performing a PUT to a non-existent timer will create a timer with the specified `timer-id`, allowing the client to chose the `timer-id` to use.  This is not an advised usage, but may happen accidentally when attempting to refresh a timer that has just passed its `repeat-for` interval._

If the body of the POST/PUT was invalid (see above) the service will return a `400 Bad Request` with the error in the `Reason` header.  If the timer service suffers a major internal catastrophe it will return a `503 Server Error` and may give a `Reason` if it can.

#### Response (DELETE)

Always a `201 OK` assuming the request was valid.  A `400 Bad Request` otherwise.

### Timer Accuracy

The timer pop is guaranteed to occur **after** the interval specified, but is not guaranteed to occur exactly on the interval (due to scheduling considerations, implementation details of the redundancy model and network latency).

During a net-split, the timer may pop multiple times, once on each partition.  Once the net-split is healed, the next timer pop will resolve the multi-headed-ness and from then on the timer will only pop once per interval.

## Internal API

Chronos nodes in a cluster communicate through an internal API which is documented here for developers.  This API is designed to be extensible to allow the API to change from release to release without breaking backwards compatibility.

### Mainline Uses

The only request type sent over the API in mainline cases is a PUT which is used for three purposes:

 * Telling the receiving node that it has been chosen as a replica for the given timer.
 * Telling the receiving node that the given timer has been popped by an earlier replica.
 * Telling the receiving node that the given timer has been deleted and should not be popped.

#### Replicating a Timer Creation

As with the public API, the URL shall be `/timers/<timer-id>` and the body shall be a JSON block:

    {
      "timing": {
        "interval": <secs>,
        "repeat-for": <secs>
        "start-time-delta": <ms>
        "sequence-number": <int>
      },
      "callback": {
        "http": {
          "uri": <callback-uri>,
          "opaque": <opaque-data>
        }
      },
      "reliability": {
        "cluster-view-id": <cluster-view-id>,
        "replicas": [<replica-1>, ...],
        "sites": [<site-1>, ...]
      }
    }

Each of the attributes that overlap with the public API are used in the same way, the new attributes are used as followed:

 * `timing/start-time-delta` - The delta to apply to the current time to compensate for time that the timer was programmed on the sending node.
 * `timing/sequence-number` - The instance of the timer that is next to pop (always 0 for one-shot timers)
 * `reliability/cluster-view-id` - The ID of the current cluster topology (used for error-checking and to spot inconsistencies in the cluster)
 * `reliability/replicas` - The ordered list of the replicas for the timer, the receiving node can use this to work out when it should pop the timer. This uses the IP addresses defined in `/etc/chronos/chronos_cluster.conf`.
 * `reliability/sites` - The ordered list of the sites for the timer, the receiving node can use this to work out when it should pop the timer. This uses the site names defined in `/etc/chronos/chronos_shared.conf`.

#### Replicating a Timer Pop

When one Chronos instance pops a timer it informs all other replicas that it did by sending a PUT message (same JSON body as above) with the appropriate `start-time-delta` and `sequence-number` set.  The receiving nodes should prepare to pop the timer at the end of the next interval.

#### Replicating a Timer Deletion

When a timer is deleted by a client or after the final timer pop, the handling node sends a PUT to the replicas for that timer specifying an empty `callback` section (indicating that this is a tombstone record) and with an appropriate `start-time-delta` and `sequence-number`.  A tombstone should be stored for one more `interval` of time to prevent out-of-date replication requests from re-creating deleted timers.

### Resynchronization requests

The timer service supports two types of request to allow Chronos nodes to resynchronize timers.

Resynchronizing timers between nodes is carried out by each Chronos node sending a series of GETs and DELETEs to all the other Chronos nodes in the Chronos cluster. The resynchronization process is described in more detail [here](doc/scaling.md).

#### Request (GET)

    GET /timers

This URL requests information about timers that are on the receiving node that the requesting node will be a replica for under new cluster configuration.

It takes two mandatory parameters

* `node-for-replicas=<address>` - The address of the node to check for replica status (typically the requesting node). This must match a node in the Chronos cluster
* `cluster-view-id=<cluster-view-id>` - The requesting node's view of the current cluster configuration.

There is one optional parameter

* `time-from=<time-from>` - The receiving node should only send information about timers that are due to pop after this time (absolute time, in microseconds since the epoch).

The request should also include a `Range` header, holding how many timers should be returned in one response, e.g.:

    Range: 100

#### Response (GET)

If the GET is missing any of the mandatory parameters or any of the mandatory parameters are invalid then the request is rejected with the following result codes:

* Any of the mandatory parameters are missing - `400 Bad Request`
* The requesting-node address doesn't correspond to a Chronos node in the cluster - `404 Not Found`
* The cluster-view-id supplied by the requesting node doesn't match the receiving node's view of the cluster ID - `400 Bad Request`

The response to a valid GET request is a `200 OK` or a `206 Partial Content`, with a JSON body containing timer information. A `206` is sent instead of a `200` if there are more timers that could be sent, but aren't because of the maximum timer limit. The 206 response includes a `Content-Range` header that shows how many timers were included (e.g. `Content-Range: 100`)

The JSON body in the response has the format:

    {"timers": [{"TimerID": id,
                 "OldReplicas": ["replica-1", ...],
                 "Timer": {"timing": {"start-time": <start time>,
                                      "sequence-number": <int>
                                      "interval": <secs>,
                                      "repeat-for": <secs>
                                     },
                           "callback": {"http": {"uri": <callback-uri>,
                                                 "opaque": <opaque-data>
                                                }
                                       },
                           "reliability": {"cluster-view-id": <cluster-view-id>,
                                           "replicas": ["replica-1", ...]
                                          }
                          }
                },
                ...
               ]
    }

This JSON body contains enough information for the requesting node to add the timer to their timer wheel, and to optionally replicate the timer to other nodes. The `Timer` object contains the information to recreate the timer on the node, the `TimerID` holds the timer's ID, and the `OldReplicas` list holds where the replicas for the timer were under the old cluster configuration. The `start time` for the timer is in ms since the epoch (modulo `UINT_MAX`).

#### Request (DELETE)

    DELETE /timers/references

This URL requests that the receiving node delete their references to a set of timers.

The DELETE body consists of the IDs of all the timers the node has just processed, paired with the replica number of the node for each timer. It has the format:

    {"IDs": [{"ID": id1, "ReplicaIndex": replica-index},
             {"ID": id2, "ReplicaIndex": replica-index}
             ...
            ]
    }

The `ReplicaIndex` is the index of the timer in the replica list (where 0 represents the primary). The `ID` is the timer ID.

#### Response (DELETE)

The response is a `202 Accepted` if the JSON body is valid, and a `400 Bad Request` otherwise.
