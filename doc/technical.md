## Chronos Technical Introduction

In Chronos we're aiming to build a mechanism to trigger some work to be done on a recurring interval in a way that is both redundant and efficient. The solution to this must satisfy the following properties:

1. Cannot contain a single point of failure (must be redundant).

     * Implies that the client must not be limited to talking to a single access point (in case that fails)
     * Implies that more than one process (or, better, server node) be responsible for handling a given timer.

1. Must support dynamic addition or removal of capacity with no loss of service.
2. Must support a clustered client (for example, a client instance might set a timer, then die, but the timer should still be able to be popped on another instance of the client cluster).
3. If asked for a timer to pop in n seconds, must pop it within at most 2\*n seconds (the closer to n the better).

A solution should also:

1. Where possible, only notify the client cluster once for a given timer pop (though it would be acceptable, in error cases, to generate multiple notifications, so long as its possible to tell at the client that this has occurred).

## The solution

Our solution, Chronos, is implemented as a service that runs on a cluster of nodes that communicate with each other to handle deciding ownership and replicating timers.

Each instance of Chronos is divided into two parts:

 * a non-redundant timer wheel component that receives timers, stores them in memory in 'soonest-to-pop' order and calls the timer's callback when a timer is scheduled to pop. The core of this component the timer wheel is a fairly standard piece of software.
 * a replicating proxy component that sits above the timer heap and handles requests from the client to create/update/delete timers by determining the (ordered) list of replicas for a given timer and forwarding the message on to those replicas. When a timer create request is received, the proxying component picks a unique ID for the timer and passes it to the client to be used later to update/delete the timer.

### Architecture

Chronos is made up of 6 major components:

 * HTTP Stack - Responsible for parsing and validating incoming requests.
 * Controller – Handles the logic for the proxying service.
 * Replication Client – A simple HTTP client that sends replication messages.
 * Timer Handler – Handles the worker threads that pop the timers.
 * Timer Wheel – The local timer wheel.
 * HTTP Callback Client – A simple HTTP client that calls back to the client.

![Architecture Diagram](doc/architecture.png?raw=true)

### Implementation Notes

#### Preventing duplicated pops from replicas

Timers are configured by the client to pop at a given time, but, to prevent duplicated pops, the replication message includes the ordered list of replicas for the timer and each replica configures their instance of the timer to pop from their local store after a small delay. The delay is skewed more for each extra replica (the primary replica pops after 0 skew, the first backup 2 seconds later, the next backup 2 seconds after that and so on).

After a timer pops and the callback is successfully performed, the replica that handled the timer notifies all the other replicas that they should skip that instance of the timer as it's already been handled. This means that, assuming the callback takes under 2 seconds the client will never see duplicate timer pops unless there's a net-split.

**Examples:**

At time 0, a client requests a timer to pop in 20 seconds with 3 replicas, and to recur 6 times.

 * Replica A sets a timer to pop at time 20
 * Replica B sets a timer to pop at time 22
 * Replica C sets a timer to pop at time 24

At time 20, replica A's timer pops. Replica A triggers the callback successfully and replicates this to B and C.

 * Replica A resets his timer to pop at 40
 * Replica B resets his timer to pop at 42
 * Replica C resets his timer to pop at 44

At this point, replica A fails catastrophically. At time 40 nothing happens, since replica A is dead.

At time 42, replica B's timer pops, he handles the callback and notifies the other replicas. He can't reach replica A but he can inform replica C.

 * Replica A is dead
 * Replica B resets his timer to pop at 62
 * Replica C resets his timer to pop at 64

At this point, a net-split occurs between B and C so, although they're both running, they cannot communicate at all.

At time 62, the timer on replica B pops, B handles the callback and attempts to notify the other replicas. He can't reach either of them.

 * Replica A is dead
 * Replica B resets his timer to pop at 82
 * Replica C's timer will pop at 64

At time 64, the timer on replica C pops, C handles the callback (Note this is a duplicate of the timer B just popped). C again attempts to update the timers on the other nodes and fails.

 * Replica A is dead
 * Replica B's timer will pop at 82
 * Replica C resets his timer to pop at 84

Now, the net-split heals and B and C can see each other.

At time 82 the timer on B pops, he handles the callback and replicates this to C.

 * Replica A is dead.
 * Replica B resets his timer to pop at 102
 * Replica C resets his timer to pop at 104

Now, node A recovers, but has lost all the database entries for the timer.

At time 102 the timer on B pops, B handles the callback and replicates this to A and C.

 * Replica A sets a new timer to pop at 120
 * Replica B resets his timer to pop at 122
 * Replica C resets his timer to pop at 124

Finally, at time 120, A pops the callback, determines this is the final pop that this timer is allowed to pop (given the repeat-for attribute). He deletes the timer from B and C.

 * Replica A has no local timer set.
 * Replica B has no local timer set.
 * Replica C has no local timer set.

#### Making updates and deletes reliable

Since networks are inherently unreliable and often impart latency in communications between nodes, there are some window conditions where updates and deletes might be unreliable. We use standard mechanisms (tombstones/change timestamps) to resolve these issues.

#### Elastic Scaling

If we didn't need to support elastic scaling of the Chronos cluster, what we've described above would be all that is necessary:

 * The proxy component can pick replicas for a timer in a deterministic way such that any other proxy component could make the same decision given the timer ID in order to update or delete a timer.
 * The replicas have a record of which the other replicas are for the timer they're handling (they have this information so they can calculate their skew, see above).

With elastic scaling, the second of these bullets is unchanged, but the first becomes harder since the deterministic process would need to know what the cluster looked like when the timer was first created so it could repeat the algorithm to determine the correct replica list.

Chronos's solution involves encoding the list of replicas into the ID returned to the user when they create/update a timer. The user then uses this ID when they want to update/delete the timer later.

The solution uses a bloom filter to store the list of replicas and appends that to the unique ID for the timer. When a request from a client arrives, the proxy component checks the bloom filter against the current list of members in the cluster and replicates the timer to all that match.

Normally this would be inefficient since checking a bloom filter to see if a given value is in it requires hashing the value multiple times, suggesting that checking every member from the cluster would be computationally expensive. Fortunately, we can calculate the bits in the bloom filter that would have to be set if each member was a replica ahead of time (since the cluster membership list rarely changes) and check for presence in the bloom filter with (example C/C++ code):

(bloom\_filter & member\_bits) == member\_bits

This is super cheap in comparison to calculating hashes.

As an extra benefit of this algorithm we are able to use a fixed sized field to represent the collection of IPv4/IPv6 addresses for the replicas.

#### Rebalancing timers

To rebalance timers during scaling up (to take advantage of the new nodes) and to restore redundancy during scaling down (so the timer that were homed on the removed nodes return to having the correct number of replicas), the bloom filter for the timer is generated from the timer's unique ID on each update operation from the client and compared to the one provided in the request. If the filters do not match, Chronos replicates the update/delete operation to all the nodes that match either filter so that the old replicas can delete their record of the timer and the new ones can create a record.

**Example:**

Initially, the Chronos cluster contains 3 nodes and a timer is created with a replication factor of 3 so one replica is placed on each node. The client is given the ID+bloom filter identifier for the timer for future use.

 * Node A sees a create request
     * Node A creates a record in its local store.

 * Node B sees a create request
     * Node B creates a record in its local store.

 * Node C sees a create request
     * Node B creates a record in its local store.

Before the timer pops, a fourth node D is added to the cluster and the timer is updated by the client (who passes the ID+Bloom filter as part of the update request). The receiving Chronos node re-hashes the ID and decides that the new set of replica nodes should be B, C and D. It also sees that node A matches the bloom filter passed by the client so he sends update messages to A, B, C and D.

 * Node A sees an update request
     * Node A is not included as a replica so it deletes it's local record

 * Node B sees an update request
     * Node B updates its local store.

 * Node C sees an update request
     * Node C updates its local store.

 * Node D sees an update request
     * Node D does not recognize this timer so it creates a record for it

In this way, the timers will automatically spread themselves out over the larger cluster.

### Real World Example

As a complete example, Ralf configures the timer to recur at the rate specified by the CDF for sending INTERIM messages. It asks for this timer to recur for the session refresh interval (+ some extra for contingency). This means that Ralf can handle the INTERIM timers at the correct rate and, if the call is lost without Ralf seeing the BYE message flow past, the INTERIM messages will eventually (after one session refresh interval) stop being generated. The CDF will detect this and treat it as the call terminating (as spec-ed by IMS).
