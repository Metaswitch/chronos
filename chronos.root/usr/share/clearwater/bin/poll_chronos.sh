#!/bin/bash

# @file poll_chronos.sh
#
# Copyright (C) Metaswitch Networks 2016
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

. /etc/clearwater/config
/usr/share/clearwater/bin/poll-http localhost:7253
rc=$?

# If the chronos process is not stable, we ignore a non-zero return code and
# return zero.
if [ $rc != 0 ]; then
  /usr/share/clearwater/infrastructure/monit_stability/chronos-stability check
  if [ $? != 0 ]; then
    echo "return code $rc ignored" >&2
    rc=0
  fi
fi

exit $?
