# Resynchronization

This document describes the resynchronization process in more detail. There is a high level view of the resynchronization process [here](../scaling.md).

During the resynchronization process, each Chronos node sends a series of GET and DELETE requests to every other Chronos node. The GET requests retrieve information about timers that the requesting node should have a replica for (under the new configuration). The DELETE requests are used to mark which Chronos nodes know about a timer (and so don't need to process it again). 

## Handling a resynchronization GET request

When a node receives a GET request as part of resynchronization (see [here](../api.md#request-get) for the API) it does the following processing:

The node loops through their timer wheel. For each timer it does the following processing

* It compares the `cluster-view-id` on the GET request to the stored `cluster-view-id` in the timer(s). If these are the same, then the nodes stops checking the timer and moves onto the next timer. This allows timers that have been created under the new cluster configuration (or have already been updated) to be quickly passed over).
* The node then calculates the replicas for the timer given the new cluster configuration. If the timer will have a replica on the requesting node under the new configuration, the node pulls out the information for the timer, and adds it to the response.
* The timer information in the response has the new replicas, and the cluster-view-id represents the new cluster configuration. 

## Processing a resynchronization GET response

When the requesting node receives the response to a GET (sent as part of resynchronization) then it loops round the timer information in the response body. 

* For each new timer it checks whether it can add the timer to its timer wheel. It only can if the requesting node has moved down the replica list (where the lowest replica is the primary) or stayed the same - e.g. if the node is moving from 2nd backup to 1st backup, then the node can add the timer, but it can't in the reverse case.
* If the node can add the timer, then it adds the timer without change to its timer wheel. 
* The node then checks where it is in the new replica list, and checks whether it should replicate the timer. It will only replicate the timer to a node where:
    * The new replica position of the node is higher than the requesting nodes position
    * The old replica position of the node is equal/higher than the requesting nodes position (where not being involved previously counts as high).   
    * The node then sends tombstone requests for the old timer to any replica on the old replica list that is equal to its level or higher, so long as the replica isn't a leaving node, and the replica is no longer involved in the timer - e.g. if the requesting node is the 1st backup, it can potentially send tombstones to the old 1st backup, old 2nd backup, etc.. If the node that used to be the old 1st backup is still involved with the timer (say it's moved to be the 2nd backup replica) then the requesting node won't send a tombstone to it.
* The requesting node then sends a DELETE containing the IDs of the old timers that it just dealt with to all leaving nodes. 

## Handling a resynchronization DELETE request

A node that receives a DELETE request as part of resynchronization (see [here](../api.md#request-delete) for the API) request loops through each ID/replica number pairing in the DELETE body, and tries to find the timer with the ID in its timer wheel. 

* If the node finds a single timer with the ID, it uses that timer. If the node finds a list of timers, it uses the informational timer (see [informational timers](doc/design/resynchronization.md#timer-store---informational-timers) below). 
* The node then checks whether the timer has an out of date cluster view ID. If it's up to date then no further processing is done. 
* Otherwise the node marks that the replica number and any replicas higher in the replica list (where the primary is the lowest replica) have seen the timer.
* If all replicas have seen the timer (which is the case when the replica number passed in the DELETE request is 0), and the timer is an informational timer (so isn't in the timer wheel), then the node deletes the timer. 

## Timer store - informational timers

It may be the case that a timer is tombstoned on a node before all the new replicas have learnt about the timer. To solve this, the timer store also stores `informational timers`. These are timers that contain information about an out-of-date timer, and they are solely used during a resynchronization process. These timers are stored under the same ID as an active timer, but they are not part of the timer wheel. 

There can be at most one informational timer stored for a single timer ID.
