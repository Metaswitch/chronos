# Development

This document describes how to build and test Chronos

Chronos development is ongoing on Ubuntu 14.04, so the processes described
below are targetted for (and tested on) this platform.  The code has been
written to be portable, though, and should compile on other platforms once the
required dependencies are installed.

## Dependencies

Chronos depend on a number of tools and libraries.  Some of these are
included as git submodules, but the rest must be installed separately.

On Ubuntu 14.04,

1.  update the package list

        sudo apt-get update

2.  install the required packages

        sudo apt-get install git debhelper devscripts build-essential libboost-program-options-dev libcurl4-openssl-dev libevent-dev libboost-regex-dev libboost-filesystem-dev libtool automake make cmake libzmq3-dev libsnmp-dev valgrind python-flask python-requests

## Getting the Code

The Chronos code is all in the `chronos` repository, and its submodules, which
are in the `modules` subdirectory.

To get all the code, clone the chronos repository with the `--recursive` flag to
indicate that submodules should be cloned too.

    git clone --recursive git@github.com:Metaswitch/chronos.git

This accesses the repository over SSH on Github, and will not work unless you have a Github account and registered SSH key. If you do not have both of these, you will need to configure Git to read over HTTPS instead:

    git config --global url."https://github.com/".insteadOf git@github.com:
    git clone --recursive git@github.com:Metaswitch/chronos.git

## Building Binaries

To build Chronos, change to the top-level `chronos` directory and issue `make`.

On completion,

* the chronos binary is in `build/bin`
* libraries on which it depends are in `usr/lib`.

## Building Debian Packages

To build Debian packages, run `make deb`.  On completion, Debian packages
are in the parent of the top-level `chronos` directory.

`make deb` can push the resulting binaries to a Debian
repository server.  To push to a repository server on the build machine, set
the `REPO_DIR` environment variable to the appropriate path.  To push (via
scp) to a repository server on a remote machine, also set the `REPO_SERVER`
environment variable to the user and server name.

## Running Unit Tests

Chronos uses our common infrastructure to run the unit tests. How to run the UTs, and the different options available when running the UTs are described [here](http://clearwater.readthedocs.io/en/latest/Running_unit_tests.html#c-unit-tests).

## Running Functional Tests

To run the Chronos FV tests, change to the top-level `chronos` directory and run `make fv_test`.
