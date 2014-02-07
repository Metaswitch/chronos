# Timer Service API

This document covers the external interface exposed by the Chronos network timer service.

## Introduction

The timer service runs like a timer heap: clients request a timer to pop after a certain interval, or to pop regularly at a given rate over a given interval, and the timer service notifies the client (through a client-supplied callback) when the timer pops.

## API Definition

The timer service exposes a single HTTP API on port 7253 for the setting and clearing of timers:

    POST /timers
    PUT /timers/<timer-id>
    DELETE /timers/<timer-id>

To set a timer, simply `POST` the definition of the timer to the above URI.  To update an existing timer, `PUT` to the specific timer URI.  To delete a timer, `DELETE` the timer ID.

The timer service is designed to be distributed across multiple nodes in a cluster. In this case a client may set or clear any timer on any node of the cluster, helpful for handling node failures.

_In future, but not in the current version, retrieving information about the configured timers may be possible through one or both of:_

    GET /timers
    GET /timers/<timer-id>

### Request (POST/PUT)

The body of the request should be a JSON block in the following format:

    {
      "timing": {
        "interval": <ms>,
        "repeat-for": <ms>
      },
      "callback": {
        "http": {
          "uri": <callback-uri>,
          "opaque": <opaque-data>
        }
      },
      "reliability": {
        "replication-factor": <n>
      }
    }

#### Timing

The `"timing"` section of the request defines how often the timer should pop (in seconds), and how long to recur the timer for (also in seconds).

The `"interval"` attribute is mandatory and the request will be rejected unless it is filled in.

The `"repeat-for"` attribute may be specified to configure the timer service to repeatedly pop the timer every interval for the given length of time.  It is not possible to specify a recurring timer that never ends, to prevent the case where the trigger to cancel the timer is lost due to an outage leading to a leaked timer.  Once the `"repeat-for"` interval has passed, the timer is deleted from the service automatically.  If a timer would pop exactly at the end of the repeat-for interval, it will be scheduled (e.g. if the `"interval"` and `"repeat-for"` are equal, the timer will pop once).

If no `"repeat-for"` attribute is specified, the timer only fires once, after `"interval"` seconds.

_Note that, if `"repeat-for"` is strictly lower than the interval, the timer will never fire.  This use case is indicative of a logical error on the part of the client._

#### Callback

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

#### Reliability

The reliability attribute is an optional parameter that may be used to specify how many replicas of the timer to create to handle outages of nodes in the cluster.

If your `replication-factor` is `n` then unless `n` or more nodes are unavailable simultaneously, all configured timers will still continue to pop, thus a higher replication factor increases the reliability of the timer service.  Configuring a replication factor greater than the current number of nodes in the cluster will be treated as specifying exactly the number of nodes configured.

The disadvantage of larger replication factors are:

* Higher capacity requirements on the timer nodes, causing worse scalability of each node.
* Larger number of duplicated pops in the case of a net-split (you may see up to `n` duplicates each pop).
* Longer delays between the requested interval and the time of the actual pop (up to `2 sec * (n - 1)` in some failure cases).

The default value for the replication factor (if unspecified) is `2`.

### Request (DELETE)

No body need be provided and will be ignored if it is.  Repeated deletion of a timer ID is as idempotent as possible. IDs may be reused extremely rarely (if more than 4096 requests are made to the same node within 1 millisecond, or if requests are made over a period of 147 years) so careless deletes should be avoided if possible to minimize the chance of deleting a timer created by some other client.

### Response (POST/PUT)

If the timer creation/update was successful, the response to the POST/PUT request will be a standard HTTP `200 OK` with a `Content-Location` header directing the client at the URI that may be used to refresh or cancel the configured timer.

_Note: Performing a PUT to a non-existent timer will create a timer with the specified `timer-id`, allowing the client to chose the `timer-id` to use.  This is not an advised usage, but may happen accidentally when attempting to refresh a timer that has just passed its `repeat-for` interval._

If the body of the POST/PUT was invalid (see above) the service will return a `400 Bad Request` with the error in the `Reason` header.  If the timer service suffers a major internal catastrophe it will return a `503 Server Error` and may give a `Reason` if it can.

### Response (DELETE)

Always a `201 OK` assuming the request was valid.  A `400 Bad Request` otherwise.

## Timer Accuracy

The timer pop is guaranteed to occur **after** the interval specified, but is not guaranteed to occur exactly on the interval (due to scheduling considerations, implementation details of the redundancy model and network latency).

The accuracy of the timer service is only as good as the accuracy of the system clock on the timer nodes themselves.  If the system clock are skewed by a large amount (&gt;&gt; 1 second) then multiple timer nodes may pop for the same sequence number of the same timer.  To prevent this, we recommend running a time-synchronizing service on each of your timer nodes (NTPd/PTPd) to keep their clocks in sync.

During a net-split, the timer may pop multiple times, once on each partition.  Once the net-split is healed, the next timer pop will resolve the multi-headed-ness and from then on the timer will only pop once per interval.
