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

5. Where possible, only notify the client cluster once for a given timer pop (though it would be
acceptable, in error cases, to generate multiple notifications, so long as its possible to tell at
the client that this has occurred).

If we didn't need to support elastic scaling of the Chronos cluster, what we've described above
would be all that is necessary:

 * The proxy component can pick replicas for a timer in a deterministic way such that any other
proxy component could make the same decision given the timer ID in order to update or delete a
timer.
 * The replicas have a record of which the other replicas are for the timer they're handling (they
have this information so they can calculate their skew, see above).

With elastic scaling, the second of these bullets is unchanged, but the first becomes harder since
the deterministic process would need to know what the cluster looked like when the timer was first
created so it could repeat the algorithm to determine the correct replica list.

Chronos's solution involves encoding the list of replicas into the ID returned to the user when they
create/update a timer. The user then uses this ID when they want to update/delete the timer later. 
ID creation and processing is described in more detail [here](design/hashing.md).

## Rebalancing timers

When a Chronos cluster scales up/down then any existing timers will no longer be on the right 
replicas (given the new configuration). For example, when a new node is added it can immediately 
start accepting new timers, but it won't have any of the load of the existing timers on it. 

We therefore want to rebalance the timers, so that new Chronos nodes can take over some of the load, 
and leaving Chronos nodes can be safely removed from the cluster. 

After the Chronos cluster configuration changes, new, updated and popped timers are rebalanced to 
the correct Chronos nodes, given the new configuration. This process is described [below](scaling.md#rebalancing-timers---passive-method).

Once all the existing timers have been modified in some way (e.g. they've popped or been updated by a
client) then the timers will be correctly balanced across the new Chronos cluster. There is also an 
active process to rebalance Chronos timers (so you don't need to wait), this is described [below](scaling.md#rebalancing-timers---active-method)
as well. 

### Rebalancing timers - passive method

To rebalance timers during scaling up (to take advantage of the new nodes) and to restore redundancy
during scaling down (so the timer that were homed on the removed nodes return to having the correct
number of replicas), the bloom filter for the timer is generated from the timer's unique ID on each
update operation from the client and compared to the one provided in the request. If the filters do
not match, Chronos replicates the update/delete operation to all the nodes that match either filter
so that the old replicas can delete their record of the timer and the new ones can create a record.

#### Worked example

Initially, the Chronos cluster contains 3 nodes and a timer is created with a replication factor of
3 so one replica is placed on each node. The client is given the ID+bloom filter identifier for the
timer for future use.

 * Node A sees a create request
     * Node A creates a record in its local store.

 * Node B sees a create request
     * Node B creates a record in its local store.

 * Node C sees a create request
     * Node B creates a record in its local store.

Before the timer pops, a fourth node D is added to the cluster and the timer is updated by the
client (who passes the ID+Bloom filter as part of the update request). The receiving Chronos node
re-hashes the ID and decides that the new set of replica nodes should be B, C and D. It also sees
that node A matches the bloom filter passed by the client so he sends update messages to A, B, C and
D.

 * Node A sees an update request
     * Node A is not included as a replica so it deletes its local record

 * Node B sees an update request
     * Node B updates its local store.

 * Node C sees an update request
     * Node C updates its local store.

 * Node D sees an update request
     * Node D does not recognize this timer so it creates a record for it

In this way, the timers will automatically spread themselves out over the larger cluster.

### Rebalancing timers - active method

The active resynchronization process has three steps. The first and third step involve changing 
the Chronos configuration file (and reloading Chronos), and the second step actually covers
the rebalancing process:

#### Step 1

Add the new scaling configuration to the chronos configuration file (see [here](clustering.md) for more details) and reload Chronos (`service chronos reload`). This triggers Chronos to update its cluster information. 

Any timers that pop on/are updated on/are added to a node that has the new configuration during this step will have the correct replicas (given the new configuration). Timers that pop/update/are added to nodes that have the old configuration are moved to the correct replicas in Step 2. 

#### Step 2

To trigger the scaling process, run `service chronos <scale-down/scale-up>` on all Chronos nodes that will remain in the cluster (i.e. not on any nodes that are being scaled down). 

#### Step 3

Finally, remove any scaling configuration from the chronos configuration file and reload the Chronos configuration again (`service chronos reload`). 

#### Step 2 - what does this actually cover?

Running `service chronos resync` sends a SIGUSR1 to the Chronos process, which triggers it to contact the other nodes in the cluster and run the resynchronization. The resynchronization process is described below, and more detail about the GET/DELETE requests is available [here](api.md).

* Each node sends a GET request to every other Chronos node.
* The receiving node calculates which of its timers the requesting node will be interested in after the scale operation down is complete (excluding any timers that have already been updated), and responds with enough information that the requesting node can add the timer to their store.
* The requesting node then takes the timers that the leaving node has told it about and then for each timer it may:
    * Add it to its timer wheel - This is only allowed if the requesting node has moved down/stayed at the same level in the replica list (where the lowest replica is the primary) for the timer
    * Replicate the timer - The timer is replicated to any node that is higher in the new replica list than the requesting node, so long as the old replica position of the node to replicate to is equal/higher than the requesting nodes position (where not being involved previously counts as high).
    * Send tombstones for the timer - Tombstone requests for the timer are sent to any node on the old replica list, so long as the replica is no longer involved in the timer, and the position of the node to send a tombstone to in the old replica list is the same/higher than the position of the requesting node in the new replica list.

* Finally, the requesting node sends a DELETE request to all leaving nodes. This contains the IDs of the (old) timers that it has processed.
* Each leaving node finds each old timer, and, if it still exists marks which replicas the new timer has been copied to (which is the requesting node, and any nodes higher in the replica list). If all replicas know about the old timer, then a leaving node deletes the old timer.

If there are many timers, then the GET responses are batched into groups of 100 timers, and the response to the GET indicates that there are more responses. The requesting nodes should continue to send GETs until they have received all the timers.

This process means that at each point in the scale down, each timer has a single node acting as the primary replica for the primary in its replica list (with small windows of time between the GET response, and any replication requests/DELETE requests being processed).

An even more detailed look what happens to timers during the resynchronization process is [here](design/resynchronization.md)

#### Worked example

Take a Chronos cluster that has 3 nodes (A,B,C), and a replication factor of 2. In this example, node C is leaving, there is a Timer 1 that lives on (C,A) (where C is the primary and A is the 1st backup), and it's moving to (B,A).

* A makes a GET to B. B loops through its timers, but doesn't find any that A will be a replica for.
* A then makes a GET to C. C loops through its timers, and finds that A will be a replica for Timer 1. It responds to A with the information A needs to add the new timer.
* When A receives the response, it updates its timer (which it is allowed to do as it as the new 1st backup can modify the old 1st backup). It doesn't send a replication request to B, or a tombstone to C, and the new 1st backup can't modify the old primary or the new primary
* A then sends a DELETE to C, containing the ID of Timer 1.
* When C receives the DELETE, it looks in its timer wheel for Timer 1. C will calculate that the first backup for Timer 1 has been informed about the timer, and mark the timer as such.

At this point, the timer is on (C,A), with the timer on A having up-to-date information. The timer's redundancy has been maintained, and there is only a single replicate at each replica level.

* B then makes a GET to C. C loops through its timers, and finds that B will be a replica for Timer 1. It responds to B with the information B needs to add the new timer.
* When B receives the response, it adds the timer to its timer wheel. It then sends a replication request to A, and a tombstone request to C
* B then sends a DELETE to C, containing the ID of Timer 1.
* When C receives the DELETE, it looks in its timer wheel for Timer 1. C will find the timer, find that it is a tombstone, and do no further processing on the timer. If the DELETE from B is processed before the tombstone from B, C will calculate that all new replicas have been informed about the Timer 1, and tombstone the timer itself.

At this point, the timer is on (B,A), with the timers on A and B having up-to-date information. The timer has been migrated, and its redundancy has been maintained.
