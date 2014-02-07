## Clustering Chronos

Chronos is a distributed timer service, based on an arbitrary-size cluster of communicating nodes which replicate timers between them. This document describes how to configure Chronos to know about the other Chrons nodes it should replicate timers to.

Chronos' cluster settings are controlled by /etc/chronos/chronos.conf, which looks like this:

    [http]
    bind-address = 0.0.0.0
    bind-port = 7253

    [cluster]
    localhost = 1.2.3.4
    node = 1.2.3.4:7253

Additional nodes are added to the cluster by adding extra "node" lines:

    [http]
    bind-address = 0.0.0.0
    bind-port = 7253

    [cluster]
    localhost = 1.2.3.4
    node = 1.2.3.4:7253
    node = 5.6.7.8:7253

The order and form of "node" entries *must* be consistent across all the Chronos nodes in the cluster.

Chronos will re-read the chronos.conf file when it receives a SIGHUP (e.g. `pkill -HUP chronos` or `service reload chronos`), and start distributing timers around the new servers immediately.

This is relatively free of race conditions - timers can still be created successfully if the cluster is temporarily in an inconsistent state while the chronos.conf files are being updated.

However, timer updates and deletes may not replicate fully if the timer was created on a node which knew about more nodes than the node receiving the PUT/DELETE, so the SIGHUPs should be sent to all nodes in quick succession and (optimally) during a low-usage period.
