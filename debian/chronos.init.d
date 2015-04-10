#!/bin/bash

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
        echo "Usage: $SCRIPTNAME {start|stop|run|status|restart|force-reload}" >&2
        exit 3
        ;;
esac
:
