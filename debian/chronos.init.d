#!/bin/bash

# @file chronos.init.d
#
# Project Clearwater - IMS in the Cloud
# Copyright (C) 2013  Metaswitch Networks Ltd
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version, along with the "Special Exception" for use of
# the program along with SSL, set forth below. This program is distributed
# in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details. You should have received a copy of the GNU General Public
# License along with this program.  If not, see
# <http://www.gnu.org/licenses/>.
#
# The author can be reached by email at clearwater@metaswitch.com or by
# post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
#
# Special Exception
# Metaswitch Networks Ltd  grants you permission to copy, modify,
# propagate, and distribute a work formed by combining OpenSSL with The
# Software, or a work derivative of such a combination, even if such
# copying, modification, propagation, or distribution would otherwise
# violate the terms of the GPL. You must comply with the GPL in all
# respects for all of the code used other than OpenSSL.
# "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
# Project and licensed under the OpenSSL Licenses, or a work based on such
# software and licensed under the OpenSSL Licenses.
# "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
# under which the OpenSSL Project distributes the OpenSSL toolkit software,
# as those licenses appear in the file LICENSE-OPENSSL.

### BEGIN INIT INFO
# Provides:          chronos
# Required-Start:    $remote_fs $syslog
# Required-Stop:     $remote_fs $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Description:       Chronos Network Timer node
### END INIT INFO

# Author: Andy Caldwell <andrew.caldwell@metaswitch.com>

PATH=/sbin:/usr/sbin:/usr/bin:/bin
DESC="Chronos network timer service"
NAME=chronos
EXECNAME=chronos
PIDFILE=/var/run/$NAME.pid
DAEMON=/usr/bin/chronos

# Exit unless daemon exists
[ -x $DAEMON ] || exit 0

# Read override configuration if present
[ -r /etc/default/$NAME ] && . /etc/default/$NAME

# Load the VERBOSE setting and other rc.d vars.
. /lib/init/vars.sh

# Load LSB functions
. /lib/lsb/init-functions

setup_environment()
{
  # Allow chronos to write out core files.
  ulimit -c unlimited

  # Include the libraries that come with chronos.
  export LD_LIBRARY_PATH=/usr/share/chronos/lib:$LD_LIBRARY_PATH
}

do_start()
{
  setup_environment
  start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON --test > /dev/null || return 1
  $start_prefix start-stop-daemon --start --quiet --background --make-pidfile --pidfile $PIDFILE --exec $DAEMON || return 2
}

do_stop()
{
  start-stop-daemon --stop --quiet --retry=TERM/30/KILL/5 --pidfile $PIDFILE --name $EXECNAME
  RETVAL="$?"
  [ "$RETVAL" = 2 ] && return 2
  rm -f $PIDFILE
  return $RETVAL
}

do_run()
{
  setup_environment
  $start_prefix start-stop-daemon --start --quiet --exec $DAEMON || return 2
}

do_reload()
{
  start-stop-daemon --stop --signal 1 --quiet --pidfile $PIDFILE --name $EXECNAME
  return 0
}

#
# Function that aborts chronos
#
# This is very similar to do_stop except it sends SIGABRT to dump a core file
# and waits longer for it to complete.
#
do_abort()
{
  start-stop-daemon --stop --quiet --retry=ABRT/60/KILL/5 --pidfile $PIDFILE --name $EXECNAME
  RETVAL="$?"
  [ "$RETVAL" = 2 ] && return 2
  rm -f $PIDFILE
  return $RETVAL
}

#
# Send Chronos a SIGUSR1 (used in scale operations)
#
do_scale_operation()
{
  start-stop-daemon --stop --signal 10 --quiet --pidfile $PIDFILE --name $EXECNAME
  return 0
}

#
# Polls Chronos until resynchronization completes
#
do_wait_sync() {
  # Wait for 2s to give Chronos a chance to have updated its statistics.
  sleep 2
  snmp_exists=3

  # Query Chronos via the 0MQ socket, parse out the number of Chronos nodes
  # still needing to be queried, and check if it's 0.
  # If not, wait for 5s and try again.
  while true
  do
    # Retrieve the statistics.
    # Temporarily uses -c clearwater community string
    nodes=`snmpget -Oqv -v2c -c clearwater localhost .1.2.826.0.1.1578918.9.10.1`

    # If the nodes left to query is 0 or unset, we're finished
    if [ "$nodes" = "0" ]
    then
      break
    fi

    # If we have 3 failures at getting the nodes, give up
    if [ "$nodes" = "No Such Instance currently exists at this OID" ]
    then
      ((snmp_exists--))
      if [ "$snmp_exists" == "0" ]
        echo -n "Could not get number of remaining nodes over SNMP"
        break
      fi
    fi

    # Indicate that we're still waiting, then sleep for 5 secs and repeat
    echo -n "..."
    sleep 5
  done
  return 0
}

case "$1" in
  start)
        [ "$VERBOSE" != no ] && log_daemon_msg "Starting $DESC" "$NAME"
        do_start
        case "$?" in
                0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
                2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
        esac
        ;;
  stop)
        [ "$VERBOSE" != no ] && log_daemon_msg "Stopping $DESC" "$NAME"
        do_stop
        case "$?" in
                0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
                2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
        esac
        ;;
  run)
        [ "$VERBOSE" != no ] && log_daemon_msg "Running $DESC" "$NAME"
        do_run
        case "$?" in
                0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
                2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
        esac
        ;;
  status)
        status_of_proc "$DAEMON" "$NAME" && exit 0 || exit $?
        ;;
  reload|force-reload)
        do_reload
        ;;
  resync)
        do_scale_operation
        ;;
  wait-sync)
        log_daemon_msg "Waiting for synchronization - $DESC"
        do_wait_sync
        ;;
  restart)
        log_daemon_msg "Restarting $DESC" "$NAME"
        do_stop
        case "$?" in
          0|1)
                do_start
                case "$?" in
                        0) log_end_msg 0 ;;
                        1) log_end_msg 1 ;; # Old process is still running
                        *) log_end_msg 1 ;; # Failed to start
                esac
                ;;
          *)
                # Failed to stop
                log_end_msg 1
                ;;
        esac
        ;;
  abort)
        [ "$VERBOSE" != no ] && log_daemon_msg "Abort $DESC" "$NAME"
        do_abort
        case "$?" in
                0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
                2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
        esac
        ;;
  *)
        echo "Usage: $SCRIPTNAME {start|stop|run|status|restart|force-reload|resync|wait-sync}" >&2
        exit 3
        ;;
esac
:
