#! /usr/bin/python2.7

# @file chronos_delete_timers.py
#
# Copyright (C) Metaswitch Networks
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

import unittest
from time import sleep

from os import sys, path
sys.path.append(path.dirname(path.abspath(__file__)))
from chronos_fv_test import ChronosFVTest, start_nodes, create_timers, chronos_nodes, delete_timers

# Test that Chronos correctly deletes timers if requested by the client
class ChronosDeleteTimers(ChronosFVTest):

    def test_deletes(self):
        # Test that Chronos replicates deletes from a client. This test creates 3 Chronos
        # nodes, adds 100 timers, then deletes the 100 timers again.
        # The test checks that we get 0 timer pops

        # Start initial nodes and add timers
        self.write_config_for_nodes([0,1,2])
        start_nodes(0, 3)
        create_timers(chronos_nodes[0], 0, 100, "pop", 3)
        delete_timers(chronos_nodes[0], 100) 

        # Check that no timer pops are recieved
        sleep(16)
        self.assert_correct_timers_received(0)


if __name__ == '__main__':
    unittest.main()
