Project Clearwater is backed by Metaswitch Networks.  We have discontinued active support for this project as of 1st December 2019.  The mailing list archive is available in GitHub.  All of the documentation and source code remains available for the community in GitHub.  Metaswitch’s Clearwater Core product, built on Project Clearwater, remains an active and successful commercial offering.  Please contact clearwater@metaswitch.com for more information. Note – this email is for commercial contacts with Metaswitch.  We are no longer offering support for Project Clearwater via this contact.

# Chronos

Chronos is a distributed, redundant, reliable timer service.  It is designed to be generic to allow it to be used as part of any service infrastructure.

Chronos is designed to scale out horizontally to handle large loads on the system and also supports elastic, lossless scaling up and down of the cluster to handle extra load on the service.  See [here](doc/technical.md) for a more detailed discussion of how Chronos works and [here](doc/scaling.md) for a more detailed discussion on how Chronos resynchronizes its timers during scaling. 

The HTTP API is described [here](doc/api.md), and the procedure for clustering a group of Chronos nodes together is described [here](doc/clustering.md).
