## Overview ##

Chronos uses hashing to determine which Chronos nodes in a timer should be the primary and backup nodes for a given timer.

The minimum requirements for this are:

- Every Chronos node should consistently come up with the same result when hashing - node A should not decide that a timer lives on nodes B and C, if node B decides it should live on nodes D and E.
- The hashing algorithm should balance timers across the cluster - node A shouldn't end up with 90% of the timers, for example.

Some additional, very desirable properties, are:

- On scale-up or scale-down, the minimum possible number of timers should move. For example, if we go from "A, B, C" to "A, B, C, D", the best way to do this is just to move 25% of timers from A, B and C onto D.
- On scale-up, if the primary replica for a timer changes, it should only change to move on to the new node (not between existing nodes)
- On scale-down, if the primary replica for a timer changes, it should only change to move off a leaving node (not between remaining nodes)
- A node that was the primary replica for a timer should not become a backup replica after a scale-up or scale-down operation.

We ensure this through rendezvous hashing. http://en.wikipedia.org/wiki/Rendezvous_hashing talks about this method at a high level, and its benefits, but to understand how it works in Chronos, the best way is probably a worked example.

## Worked example ##

Assume we start with the cluster A, B, C, and are trying to choose a primary and a backup for a timer with ID 100.

- At start-of, day, we hash "A", "B" and "C". These might hash to the values 1000, 2000 and 3000.
- When considering the timer with ID 100, we hash the value 100 three times:
    - once with the seed 1000 (server A's hash), resulting in e.g. 7777
    - once with the seed 2000 (server B's hash), resulting in e.g. 9999
    - once with the seed 3000 (server C's hash), resulting in e.g. 8888
-  We then reorder the servers by those hashes - A/7777, C/8888, B/9999
-  A would therefore be the primary replica (as the lowest hash), and B the first backup replica (as the highest hash).
-  If we had a replication-factor of 3 rather than 2, C (with the second-highest hash) would be second backup replica.

- We then scale up to the cluster "A, B, C, D". We hash "A", "B", "C" and "D". "A", "B" and "C" hash to the same values as before (1000, 2000 and 3000). "D" might hash to 4000,
- When considering the timer with ID 100, we hash the value 100 four times:
    - once with the seed 1000 (server A's hash), resulting in 7777 as before
    - once with the seed 2000 (server B's hash), resulting in 9999 as before
    - once with the seed 3000 (server C's hash), resulting in 8888 as before
    - once with the seed 4000 (server D's hash), resulting in e.g. 6666
-  We then reorder the servers by those hashes - D/6666, A/7777, C/8888, B/9999
-  D would therefore be the primary replica (as the lowest hash), and B the first backup replica (as the highest hash).
-  If we had a replication-factor of 3 rather than 2, C (with the second-highest hash) would be second backup replica.
-  This means that ownership of the timer (as primary) moves from D to A, but B remains the backup.
-  Because A, B and C's relative positions are unchanged by this hashing, timers cannot move between them - only on to D.

## Hash collision handling ##

Collisions are very rare: our hash function has 4.3 billion possible outputs. If two hashes collide, we will cope with this situation, although we may sacrifice some of our "desirable" properties to do so.

### Collision between server hashes ###

Assume we start with the cluster A1, A2, B, C, and are trying to choose a primary and a backup for a timer with ID 100.

- At start-of day, we hash "A1", "A2", "B" and "C". These might hash to the values 1000, 1000, 2000 and 3000. We resolve the clash between A1 and A2's hashes by incrementing the second hash until it is unique - so in this case, it would become 1000, 1001, 2000 and 3000. 
- When considering the timer with ID 100, we hash the value 100 four times:
    - once with the seed 1000 (server A1's hash), resulting in e.g. 7777
    - once with the seed 1001 (server A2's hash), resulting in e.g. 9000
    - once with the seed 2000 (server B's hash), resulting in e.g. 9999
    - once with the seed 3000 (server C's hash), resulting in e.g. 8888
-  We then reorder the servers by those hashes - A1/7777, C/8888, A2/9000, C/8888, B/9999
-  A1 would therefore be the primary replica (as the lowest hash), and B the first backup replica (as the highest hash).
-  If we had a replication-factor of 3 rather than 2, A2 (with the second-highest hash) would be second backup replica.


- We then scale-down, removing A1. We hash "A2", "B" and "C". These hash to the values 1000, 2000 and 3000. We no longer have clashing hashes, so don't increment A2's hash - it is now 1000 instead of 1001.
- When considering the timer with ID 100, we hash the value 100 three times:
    - once with the seed 1000 (server A2's new hash), resulting in e.g. 7777
    - once with the seed 2000 (server B's hash), resulting in e.g. 9999
    - once with the seed 3000 (server C's hash), resulting in e.g. 8888
-  We then reorder the servers by those hashes - A2/7777, C/8888, C/8888, B/9999
-  A2 would therefore be the primary replica (as the lowest hash), and B the first backup replica (as the highest hash).
-  If we had a replication-factor of 3 rather than 2, C (with the second-highest hash) would be second backup replica.
-  Note that this means that A2 has gone from being the second backup to being primary. This does not happen under normal circumstances - it only happens here because of the collision, which means A2 stays in the cluster but changes its hash.  

### Collision between timer hashes ###

Assume we start with the cluster A, B, C, and are trying to choose a primary and a backup for a timer with ID 200.

- At start-of day, we hash "A", "B" and "C". These might hash to the values 1000, 2000 and 3000. 
- When considering the timer with ID 200, we hash the value 200 three times:
    - once with the seed 1000 (server A's hash), resulting in e.g. 800
    - once with the seed 2000 (server B's hash), resulting in e.g. 800
    - once with the seed 3000 (server C's hash), resulting in e.g. 900
-  We resolve the collision (two values of 800) by incrementing the clashing value until it is unique. This effectively means that, in the case of a clash, the server first in the original list wins.
-  We then reorder the servers by those hashes - A/800, B/801, C/900
-  A would therefore be the primary replica (as the lowest hash), and C the first backup replica (as the highest hash).
-  If we had a replication-factor of 3 rather than 2, B (with the second-highest hash) would be second backup replica.

Note that there is no unusual behaviour in this case: if A or B are removed from the cluster, the result (B becoming primary, and no change, respectively) is the same as if B had actually hashed to 801 instead of 800.
