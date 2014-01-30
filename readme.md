# Chronos

Chronos is a distributed, redundant, reliable timer service.  Designed to be generic to allow it to be used as part of any service infrastructure.

Chronos is designed the scale out horizontally to handle large loads on the system and also supports elastic, lossless scaling up and down of the cluster to handle extra load on the service.

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

    sudo apt-get install git build-essential devscripts debhelper libcurl4-gnutls-dev libboost-program-options-dev libboost-regex-dev libevent-dev google-mock

The only part that doesn't install is the actual `gtest` library, it does however install the library code to `/usr/src`.  You can build this into the correct library with:

    sudo apt-get install cmake
    cd /usr/src/gtest
    sudo cmake -f CMakeCache.txt
    sudo make
    sudo mv libgtest.a libgtest_main.a /usr/lib/

### Building

To fetch the code, use `git` and remember to fetch the sub-modules as well:

    git clone git@bitbucket.org:Metaswitch/chronos.git --recursive

The supplied makefile has a few useful targets:

 * `make` - Builds the Chronos executable
 * `make test` - Runs the UTs
 * `make coverage` - Runs the UTs and generates a code coverage report
 * `make valgrind` - Runs the UTs under valgrind and reports the results
 * `make deb` - Build a Debian package containing Chronos and a default configuration file.
