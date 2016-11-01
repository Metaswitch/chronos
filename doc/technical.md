## Chronos Technical Introduction

With Chronos we've aimed to build a cloud-ready timer service in a way that is both redundant and
efficient. The solution to this must satisfy the following properties:

1. Cannot contain a single point of failure (must be redundant).

     * Implies that the client must not be limited to talking to a single access point (in case that fails)
     * Implies that more than one process (or, better, server node) be responsible for handling a given timer.

2. Must support dynamic addition or removal of capacity with no loss of service.
3. Must support a clustered client (for example, a client instance might set a timer, then die, but
the timer should still be able to be popped on another instance of the client cluster).
4. If asked for a timer to pop in `n` seconds, must pop it "soon" after `n` seconds have passed,
certainly within at most `2*n` seconds (the closer to `n` the better).

A solution should also:

1. Where possible, only notify the client cluster once for a given timer pop (though it would be
acceptable, in error cases, to generate multiple notifications, so long as its possible to tell at
the client that this has occurred).

## The solution

Chronos is implemented as a service that runs on a cluster of nodes that communicate with each other
to handle deciding ownership and replicating timers.

Each instance of Chronos is divided into two parts.

 * A non-redundant timer wheel component - Receives timers, stores them in memory in
'soonest-to-pop' order and calls the timer's callback when a timer is scheduled to pop. The core
of this component the timer wheel is a fairly standard piece of software.
 * A replicating proxy component - Sits above the timer heap and handles requests from the client to
create/update/delete timers by determining the (ordered) list of replicas for a given timer and
forwarding the message on to those replicas. When a timer create request is received, the
proxying component picks a unique ID for the timer and passes it to the client to be used later
to update/delete the timer.  Also responsible for receiving replication messages from other
instances in the cluster and passing them down to the timer wheel.

### Architecture

Chronos is made up of 8 major components:

 * HTTP Stack - Responsible for parsing and validating incoming requests.
 * Controller - Handles the logic for the proxying service.
 * Replication Client - A simple HTTP client that sends replication messages.
 * Timer Handler - Handles the worker threads that pop the timers.
 * Timer Wheel - The local timer wheel.
 * HTTP Callback Client - A simple HTTP client that calls back to the client.
 * Chronos connection - Responsible for resynchronizing timers between Chronos nodes 
 * GR replicator - Responsible for replicating timers between sites

![Architecture Diagram](architecture.png?raw=true)

### Implementation Notes

Without going into a full specification and design, the following sections describe the more
interesting problems we ran into during the design of Chronos and how they are each resolved in the
Chronos's implementation.

#### Preventing duplicated pops from replicas

Timers are configured by the client to pop at a given time, and the client should be able to assume
(as far as possible) that the timer will pop once at that time.  If the timer pops, say, twice per
configured pop time, the client has to detect and throw away each duplicate.  Thus we wanted to
minimize the times we accidentally pop a timer twice.

To achieve this, the replication message includes the ordered list of replicas for the timer and
each replica configures their instance of the timer to pop from their local store after a small
delay. The delay is skewed more for each extra replica (the primary replica pops after 0 skew, the
first backup 2 seconds later, the next backup 2 seconds after that and so on).

After a timer pops and the callback is successfully performed, the replica that handled the timer
notifies all the other replicas that they should skip that instance of the timer as it's already
been handled. This means that, assuming the callback takes under 2 seconds the client will never see
duplicate timer pops unless there's a net-split.

**Examples:**

At time 0, a client requests a timer to pop in 20 seconds with 3 replicas, and to recur 6 times.

 * Replica A sets a timer to pop at time 20
 * Replica B sets a timer to pop at time 22
 * Replica C sets a timer to pop at time 24

At time 20, replica A's timer pops. Replica A triggers the callback successfully and replicates this
to B and C.

 * Replica A resets his timer to pop at 40
 * Replica B resets his timer to pop at 42
 * Replica C resets his timer to pop at 44

At this point, replica A fails catastrophically. At time 40 nothing happens, since replica A is
dead.

At time 42, replica B's timer pops, he handles the callback and notifies the other replicas. He
can't reach replica A but he can inform replica C.

 * Replica A is dead
 * Replica B resets his timer to pop at 62
 * Replica C resets his timer to pop at 64

At this point, a net-split occurs between B and C so, although they're both running, they cannot
communicate at all.

At time 62, the timer on replica B pops, B handles the callback and attempts to notify the other
replicas. He can't reach either of them.

 * Replica A is dead
 * Replica B resets his timer to pop at 82
 * Replica C's timer will pop at 64

At time 64, the timer on replica C pops, C handles the callback (Note this is a duplicate of the
timer B just popped). C again attempts to update the timers on the other nodes and fails.

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

Finally, at time 120, A pops the callback, determines this is the final pop that this timer is
allowed to pop (given the repeat-for attribute). He deletes the timer from B and C.

 * Replica A has no local timer set.
 * Replica B has no local timer set.
 * Replica C has no local timer set.

#### Making updates and deletes reliable

Since networks are inherently unreliable and often impart latency in communications between nodes,
there are some window conditions where updates and deletes might be unreliable. We use standard
mechanisms (tombstones/change timestamps) to resolve these issues.

### Real World Example

As a complete example, Ralf configures the timer to recur at the rate specified by the CDF for
sending INTERIM messages. It asks for this timer to recur for the session refresh interval (plus
some extra for contingency). This means that Ralf can handle the INTERIM timers at the correct rate
and, if the call is lost without Ralf seeing the BYE message flow past, the INTERIM messages will
eventually (after one session refresh interval) stop being generated. The CDF will detect this and
treat it as the call terminating (as spec-ed by IMS).
