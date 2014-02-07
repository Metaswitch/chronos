## Clustering Chronos

Chronos is a distributed timer service, based on an arbitrary-size cluster of communicating nodes which replicate timers between them. This document describes how to configure Chronos to know about the other Chrons nodes it should replicate timers to.

Chronos's cluster settings are controlled by /etc/chronos/chronos.conf, which looks like this:

    [http]
    bind-address = 0.0.0.0
    bind-port = 7253

    [cluster]
    localhost = 1.2.3.4
    node = 1.2.3.4:7253

## Scaling up

Additional nodes are added to the cluster by adding extra "node" lines and sending a SIGHUP (so that Chronos reloads the config):

    [http]
    bind-address = 0.0.0.0
    bind-port = 7253

    [cluster]
    localhost = 1.2.3.4
    node = 1.2.3.4:7253
    node = 5.6.7.8:7253

The order and form of "node" entries *must* be consistent across all the Chronos nodes in the cluster.

## Scaling down

Likewise, nodes can be removed from the cluster simply by deleting their "node" lines and sending a SIGHUP on every node.

When this happens, those nodes will no longer be chosen as replicas for new timers. However, they will continue to serve as replicas for existing timers, so there is a risk of timers being lost if the Chronos process is stopped before those timers expire (i.e. the longest 'repeat-for' period that applications using Chronos will set on a timer, multiplied by the total number of times it can do a PUT to reset a timer).

We expect Chronos clusters to be deployed privately in support of specific applications, so these variables will be known. However, we plan to improve the safety of data when scaling down in future releases.

# Other scaling requirements and notes

Chronos will re-read the chronos.conf file when it receives a SIGHUP (e.g. `pkill -HUP chronos` or `service reload chronos`), and start distributing timers around the new servers immediately.

This is relatively free of race conditions - timers can still be created successfully if the cluster is temporarily in an inconsistent state while the chronos.conf files are being updated.

However, timer updates and deletes may not replicate fully if the timer was created on a node which knew about more nodes than the node receiving the PUT/DELETE, so the SIGHUPs should be sent to all nodes in quick succession and (optimally) during a low-usage period.
