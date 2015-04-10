# Chronos

Chronos is a distributed, redundant, reliable timer service.  It is designed to be generic to allow it to be used as part of any service infrastructure.

Chronos is designed to scale out horizontally to handle large loads on the system and also supports elastic, lossless scaling up and down of the cluster to handle extra load on the service.  See [here](doc/technical.md) for a more detailed discussion of how Chronos works.

The HTTP API is described [here](doc/api.md), and the procedure for clustering a group of Chronos nodes together is described [here](doc/clustering.md).

## Development

### Dependencies

To develop on Chronos, you'll need to install a few dependencies first.

 * General building tools (make/gcc/libc/devscripts)
 * libcurl
 * libboost-program-options
 * libboost-regex
 * libevent
 * google-mock
 * google-test

On Ubuntu most of these can be installed with:

    sudo apt-get install git build-essential devscripts debhelper libcurl4-gnutls-dev libboost-program-options-dev libboost-regex-dev libevent-dev

### Building

To fetch the code, use `git` and remember to fetch the sub-modules as well:

    git clone git@github.com:Metaswitch/chronos.git --recursive

The supplied makefile has a few useful targets:

 * `make` - Builds the Chronos executable
 * `make test` - Runs the UTs
 * `make coverage` - Runs the UTs and generates a code coverage report
 * `make valgrind` - Runs the UTs under valgrind and reports the results
 * `make deb` - Build a Debian package containing Chronos and a default configuration file.
