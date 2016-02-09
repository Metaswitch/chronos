# Chronos Statistics Structures

Chronos is used in Clearwater to provide stateful statistics where a deployment otherwise does not maintain state. This is done through the use of tags attached to timers. Moreover, Chronos is able to accept any tag of up to 16 A-Z characters, and can handle counts of up to the maximum value of a uint32_t.  
This document will provide an overview of how these statistics are handled within Chronos, and how they are exposed over SNMP.

## Chronos side interactions

As detailed in the [API documentation](api.md), Chronos timer requests can be sent with a `"statistics"` object in the JSON body, specifying tags to be used in reporting statistics. These tags are parsed from the JSON, and stored as a map of `type` to `count`.
The tags are then passed through to the statistics tables by the update_statistics function in the timer handler. They are stored as part of the timer object so that comparisons can be made on timer updates to see how the statistics have been altered.

## Statistics tables

### Scalar vs Timer Count

The statistics themselves are stored using two subclasses of the `snmp_infinite_base_table`, the `snmp_infinite_scalar_table` and the `snmp_infinite_timer_count_table`. These are all found in cpp-common.  
The key difference between these two is the structure used beneath to store the data. The scalar table simply stores a single uint32_t value for each tag, as it is used to provide the instantaneous value for the stored data. The timer count table instead stores statistics using the `timer_counter` class, which calculates and stores the average, variance, and high and low water marks for the count of each tag for each of the following time periods:

* Previous five seconds
* Current five minutes
* Previous five minutes

Both of the tables simply expose increment and decrement interfaces to Chronos.

### The Infinite Table

Both of the above tables inherit their complex functionality from the `InfiniteBaseTable` class. The table is named as infinite because it appears to be infinite from the point of view of SNMP. i.e. SNMP can query any OID under the OID each table is registered to, and it will receive a value; if a real value is found under the table it is returned, if not a default value of 0 is returned.

This is done by registering each table to a root OID (e.g. .1.2.826.0.1.1578918.999), and having the table class pass the OID of any requests under this table to internal parsing functions. The functions attempt to parse the OID of a request for the information needed to find the statistics requested by considering requests as the following format:

> <root OID>.<Tag Length>.<Tag as . separated ASCII code>.<snmp table column>.<snmp table row>

For example, the request for column 2, row 1 of the table for tag "TAG" would look as follows:

> <root OID>.3.84.65.71.2.1

The internal functions can then work along this OID, from left to right, parsing each section as it goes, resulting in a basic logic as follows:

1. Move to the end of the root OID
2. Check the tag length is valid (1 - 16)
3. Move along the next <tag length> numbers, parsing them off as ASCII values to determine the tag.
4. Check the final two values for the table column and row.
5. Set up a default return value of 0, and then attempt to get a new value from the data structure for the parsed tag.
6. Return the value to net_snmp.

//how we handle rollover and rollback.
//why you shouldn't walk the table
