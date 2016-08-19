# Geographic Redundancy in Chronos

Geographic redundancy (GR) is supported in Chronos by using essentially the same methods as we ensure in-site redundancy. These are described [here](https://github.com/Metaswitch/chronos/blob/dev/doc/technical.md).

At a high level, when a timer is added (or first modified after a deployment becomes GR) it creates a site ordering list. This list simply takes the available sites, makes the local site the primary site, and then orders the rest of the sites randomly after it. Chronos knows what sites are available as these are configured in the [GR config](https://github.com/Metaswitch/chronos/blob/dev/doc/configuration.md).

The Chronos process then replicates the timer both within site and cross site. Each Chronos process that the timer is replicated to uses both the site's position in the site ordering list and the Chronos node's position in the replica list to decide what offset to apply to the pop time of the timer. For example, in a 3 site deployment with two replicas per site you can get a timer with the following replicas and pop delays.

* Site 1
** Replica 1 - No delay
** Replica 2 - 2 sec delay
* Site 3
** Replica 1 - 4 sec delay
** Replica 2 - 6 sec delay
* Site 2
** Replica 1 - 8 sec delay
** Replica 2 - 10 sec delay

When a timer pops in a Chronos process, Chronos handles the timer as normal (e.g. handling the callback, replicating the timer/tombstone to all other replicas of the timer).
 
This solution provides the desired data resiliency. A timers can be delayed by `((number of sites - 1) * number of replicas * 2) + 2 + site latencies)` seconds in the case of a multi-site failure.

There's no active timer resynchronisation on site failure - when a site recovers it won't have any Chronos timers until the timers are modified by a client or they pop (which triggers a replication cross-site).

## Error conditions

### One site fails

All existing timers where the failed site is the primary site are delayed by `(number of replicas * 2)` seconds. There is no impact on timers where the failed site isn't the primary site.

All new timers include the failed site when they calculate the site priority list for the timer, but the failed site should never be the primary site (as the primary site is always the site that creates the timer). Therefore, new timers won't have delayed pop times.

### All but one site fails

All existing timers are delayed by a maximum of `(number of site - 1) * number of replicas * 2) + 2 + site latencies)`. Timers where the sole remaining site is higher in the site priority list will be delayed by less time.

All new timers include the failed sites when they calculate the site priority list for the timer, but sole working site should always be the primary site (as the primary site is always the site that creates the timer). Therefore, new timers won't have delayed pop times.

### Net split between site

When the communication between sites goes down, rather than a site going down, then timer replication requests won't be sent successfully between sites.

Existing timers will double pop (e.g. if `site1` is the primary site it will pop at 0 secs, then `site2` will pop at 4 secs). All users of Chronos should be safe to this type of double pop.

## Adding or removing sites

The available sites can be changed by modifying the Chronos GR config - see [here](https://github.com/Metaswitch/chronos/blob/dev/doc/configuration.md) for more details.

### Site removal

The next time a timer is modified by the client, or the timer pops, its site list is edited to remove the site. This reduces the delay for any sites that where after the removed site in the site list.

The removed site is never included on any new timers.

### Site addition

The next time a timer is modified by the client, or the timer pops, its site list is edited to add the site. The new site is always added at the end of the site list for existing timers.

New timers treat the new site the same as any other site (so it can be added at any point in the site list).
