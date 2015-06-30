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

To build Chronos and all its dependencies, change to the top-level `chronos`
directory and issue `make`.  

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

To run the chronos unit test suite, change to the top-level `chronos` directory and issue `make test`.

Chronos unit tests use the [Google Test](https://code.google.com/p/googletest/)
framework, so the output from the test run looks something like this.

    [==========] Running 46 tests from 4 test cases.
    [----------] Global test environment set-up.
    [----------] 6 tests from TestHandler
    [ RUN      ] TestHandler.ValidJSONDeleteTimer
    [       OK ] TestHandler.ValidJSONDeleteTimer (1577 ms)
    [ RUN      ] TestHandler.ValidJSONCreateTimer
    [       OK ] TestHandler.ValidJSONCreateTimer (224 ms)
    [ RUN      ] TestHandler.InvalidNoTimerNoBody
    [       OK ] TestHandler.InvalidNoTimerNoBody (279 ms)
    [ RUN      ] TestHandler.InvalidMethodNoTimer
    [       OK ] TestHandler.InvalidMethodNoTimer (36 ms)
    [ RUN      ] TestHandler.InvalidMethodWithTimer
    [       OK ] TestHandler.InvalidMethodWithTimer (29 ms)
    [ RUN      ] TestHandler.InvalidTimer
    [       OK ] TestHandler.InvalidTimer (37 ms)
    ...
    [       OK ] TestTimerStore.DeleteOverdueTimer (175 ms)
    [----------] 23 tests from TestTimerStore (77891 ms total)

    [----------] Global test environment tear-down
    [==========] 46 tests from 4 test cases ran. (86766 ms total)
    [  PASSED  ] 46 tests.

The chronos makefile offers the following additional options and targets.

*   `make coverage` runs code coverage checks.
*   `make valgrind` runs the tests checking for memory leaks (using [Valgrind](http://valgrind.org/)).
*   Passing `NOISY=T` enables verbose logging during the tests; you can add
    a logging level (e.g., `NOISY=T:99`) to control which logs you see.

## Running Functional Tests

To run the tests of Chronos resynchronization, run `make resync_test`.
