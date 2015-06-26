# Chronos

Chronos is a distributed, redundant, reliable timer service.  It is designed to be generic to allow it to be used as part of any service infrastructure.

Chronos is designed to scale out horizontally to handle large loads on the system and also supports elastic, lossless scaling up and down of the cluster to handle extra load on the service.  See [here](doc/technical.md) for a more detailed discussion of how Chronos works and [here](doc/scaling.md) for a more detailed discussion on how Chronos resynchronizes its timers during scaling. 

The HTTP API is described [here](doc/api.md), and the procedure for clustering a group of Chronos nodes together is described [here](doc/clustering.md).
