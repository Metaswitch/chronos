#! /usr/bin/python2.7

# @file chronos_pop_errors.py
#
# Copyright (C) Metaswitch Networks 2016
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

import unittest
from time import sleep

from os import sys, path
sys.path.append(path.dirname(path.abspath(__file__)))
from chronos_fv_test import ChronosFVTest, start_nodes, create_timers, chronos_nodes, node_reload_config

# Test that Chronos correctly handles responses to its timer pops
class ChronosPopErrorTest(ChronosFVTest):

    def test_503_errors(self):
        # Test that Chronos replicas pop correctly. This test creates 3 Chronos
        # nodes and adds 100 timers that will get 503 errors on their first pop
        # and successes on subsequent pops.
        # We expect that the primary replica for each timer will get a 503
        # error, so the secondary replica will pop and succeed, which should
        # prevent the tertiary replica from popping.
        # The test checks that we get 100 successful pops and 100 failed pops.

        # Start initial nodes and add timers
        self.write_config_for_nodes([0,1,2])
        start_nodes(0, 3)
        create_timers(chronos_nodes[0], 0, 100, "pop-with-errors", 3)

        # Check that all the timers have popped
        sleep(16)
        self.assert_correct_timers_received(100)
        self.assert_correct_timers_failed(100)


if __name__ == '__main__':
    unittest.main()
