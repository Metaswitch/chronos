#!/bin/bash

# @file chronos.init.d
#
# Copyright (C) Metaswitch Networks
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

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
PIDFILE=/var/run/$NAME/$NAME.pid
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
  # Allow us to write to the pidfile directory
  install -m 755 -o $NAME -g root -d /var/run/$NAME && chown -R $NAME /var/run/$NAME

  setup_environment
  start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON --test > /dev/null || return 1
  $start_prefix start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON --chuid $NAME -- --daemon --pidfile=$PIDFILE || return 2
}

do_stop()
{
  start-stop-daemon --stop --quiet --retry=TERM/30/KILL/5 --user $NAME --pidfile $PIDFILE --name $EXECNAME
  RETVAL="$?"
  [ "$RETVAL" = 2 ] && return 2
  return $RETVAL
}

do_run()
{
  # Allow us to write to the pidfile directory
  install -m 755 -o $NAME -g root -d /var/run/$NAME && chown -R $NAME /var/run/$NAME

  setup_environment
  $start_prefix start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON --chuid $NAME -- --pidfile=$PIDFILE || return 2
}

do_reload()
{
  start-stop-daemon --stop --signal 1 --quiet --pidfile $PIDFILE --name $EXECNAME
  return 0
}

do_reload_dns()
{
  # Send a SIGUSR2 (12)
  start-stop-daemon --stop --signal 12 --quiet --pidfile $PIDFILE --name $EXECNAME
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
  start-stop-daemon --stop --quiet --retry=ABRT/60/KILL/5 --user $NAME --pidfile $PIDFILE --name $EXECNAME
  RETVAL="$?"
  # If the abort failed, it may be because the PID in PIDFILE doesn't match the right process
  # In this window condition, we may not recover, so remove the PIDFILE to get it running
  if [ $RETVAL != 0 ]; then
    rm -f $PIDFILE
  fi
  [ "$RETVAL" = 2 ] && return 2
  return $RETVAL
}

#
# Send Chronos a SIGUSR1 (used in resync operations)
#
do_resync_operation()
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

  # Query Chronos via the 0MQ socket, parse out the number of Chronos nodes
  # still needing to be queried, and check if it's 0.
  # If not, wait for 5s and try again.
  num_cycles_unchanged=0
  while true
  do
    # Retrieve the statistics.
    # Temporarily uses -c clearwater community string
    nodes=`snmpget -Oqv -v2c -c clearwater-internal localhost .1.2.826.0.1.1578918.9.10.1.0`

    # If the nodes left to query is 0 or unset, we're finished
    if [ "$nodes" = "0" ]
    then
      break
    fi

    # If the nodes left to query hasn't changed for the last 120 cycles (i.e.
    # over the last 10 minutes), make a syslog and stop waiting.  We don't
    # expect resyncs to take more than 10 minutes in normal operation, so this
    # suggests that the local service has failed.  We need to
    # end the wait in this case as the potential service impact of aborting
    # the wait early is far outweighed by the impact on management operations
    # of an infinite wait.
    if [ "$nodes" = "$last_nodes" ]
    then
      num_cycles_unchanged=$(( $num_cycles_unchanged + 1 ))

      if [ $num_cycles_unchanged -ge 120 ]
      then
        logger chronos: Wait sync aborting as unsynced node count apparently stuck at $nodes
        break
      fi

    else
      last_nodes=$nodes
      num_cycles_unchanged=0
    fi

    # Indicate that we're still waiting, then sleep for 5 secs and repeat
    echo -n "..."
    sleep 5
  done
  return 0
}

# There should only be at most one chronos process, and it should be the one in /var/run/chronos.pid.
# Sanity check this, and kill and log any leaked ones.
if [ -f $PIDFILE ] ; then
  leaked_pids=$(pgrep -f "^$DAEMON" | grep -v $(cat $PIDFILE))
else
  leaked_pids=$(pgrep -f "^$DAEMON")
fi
if [ -n "$leaked_pids" ] ; then
  for pid in $leaked_pids ; do
    logger -p daemon.error -t $NAME Found leaked chronos $pid \(correct is $(cat $PIDFILE)\) - killing $pid
    kill -9 $pid
  done
fi

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
  reload-dns)
        do_reload_dns
        ;;
  resync)
        do_resync_operation
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
        echo "Usage: $SCRIPTNAME {start|stop|run|status|restart|force-reload|reload-dns|resync|wait-sync}" >&2
        exit 3
        ;;
esac
:
