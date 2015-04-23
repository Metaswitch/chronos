## Clustering Chronos

Chronos is a distributed timer service, based on an arbitrary-size cluster of communicating nodes which replicate timers between them. This document describes how to configure Chronos to know about the other Chronos nodes it should replicate timers to.

Chronos's cluster settings are controlled by `/etc/chronos/chronos.conf`, which includes:

    [cluster]
    localhost = 1.2.3.4
    node = 1.2.3.4

## Scaling up

Additional nodes are added to the cluster by adding extra `node` lines and sending a SIGHUP (so that Chronos reloads the config):

    [cluster]
    localhost = 1.2.3.4
    node = 1.2.3.4
    node = 2.3.4.5
    node = 3.4.5.6
    node = 4.5.6.7

To rebalance any existing timers across the Chronos cluster, run `service chronos resync` on each Chronos node. You can find more details about scaling up procedures for a Clearwater deployment [here](http://clearwater.readthedocs.org/en/latest/Clearwater_Elastic_Scaling/index.html).

## Scaling down

To remove nodes from a cluster, mark them as leaving in the configuration file, e.g.:

    [cluster]
    localhost = 1.2.3.4
    node = 1.2.3.4
    node = 2.3.4.5
    leaving = 3.4.5.6
    leaving = 4.5.6.7

Nodes marked as leaving will no longer be chosen as replicas for new timers. To rebalance the existing timers across the new Chronos cluster, run `service chronos resync` on each Chronos node. You can find more details about scaling down procedures for a Clearwater deployment [here](http://clearwater.readthedocs.org/en/latest/Clearwater_Elastic_Scaling/index.html).

# Other scaling requirements and notes

Chronos will re-read the chronos.conf file when it receives a SIGHUP (e.g. `pkill -HUP chronos` or `service reload chronos`), and start distributing timers around the new servers immediately.

This is relatively free of race conditions - timers can still be created successfully if the cluster is temporarily in an inconsistent state while the `chronos.conf` files are being updated.

However, timer updates and deletes may not replicate fully if the timer was created on a node which knew about more nodes than the node receiving the `PUT`/`DELETE`, so the SIGHUPs should be sent to all nodes in quick succession and (optimally) during a low-usage period.
