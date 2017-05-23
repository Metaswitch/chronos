#! /usr/bin/python2.7

# @file chronos_resync.py
#
# Copyright (C) Metaswitch Networks 2017
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

import unittest
from time import sleep

from os import sys, path
sys.path.append(path.dirname(path.abspath(__file__)))
from chronos_fv_test import ChronosFVTest, start_nodes, create_timers, kill_nodes, chronos_nodes, node_reload_config, node_trigger_scaling

# Test the resynchronization operations for Chronos.
class ChronosResyncTest(ChronosFVTest):

    def test_scale_up(self):
        # Test that scaling up works. This test creates 2 Chronos nodes,
        # adds 100 timers, scales up to 4 Chronos nodes, then checks that
        # 100 timers pop.

        # Start initial nodes and add timers
        self.write_config_for_nodes([0,1])
        start_nodes(0, 2)
        create_timers(chronos_nodes[0], 0, 100)

        # Scale up
        self.write_config_for_nodes([0,1], [2,3])
        start_nodes(2, 4)
        node_reload_config(0, 2)
        node_trigger_scaling(0, 4)

        # Check that all the timers have popped
        sleep(12)
        self.assert_correct_timers_received(100)

    def test_scale_down(self):
        # Test that scaling down works. This test creates 4 Chronos nodes,
        # adds 100 timers, scales down to 2 Chronos nodes, then checks that
        # 100 timers pop.

        # Start initial nodes and add timers
        self.write_config_for_nodes([0,1,2,3])
        start_nodes(0, 4)
        create_timers(chronos_nodes[0], 0, 100)

        # Scale down
        self.write_config_for_nodes([0,1], [], [2,3])
        node_reload_config(0, 4)
        node_trigger_scaling(0, 4)
        kill_nodes(2, 4)

        # Check that all the timers have popped
        sleep(12)
        self.assert_correct_timers_received(100)

    def test_scale_up_scale_down(self):
        # Test a scale up and scale down. This test creates 2 Chronos nodes,
        # and adds 100 timers. It then scales up to 4 Chronos nodes, then
        # scales back down to the 2 new Chronos nodes. It then checks that
        # 100 timers pop.

        # Start initial nodes and add timers
        self.write_config_for_nodes([0,1])
        start_nodes(0, 2)
        create_timers(chronos_nodes[0], 0, 100)

        # Scale up
        self.write_config_for_nodes([0,1], [2,3])
        start_nodes(2, 4)
        node_reload_config(0, 2)
        node_trigger_scaling(0, 4)
        sleep(10)

        # Scale down the initial nodes
        self.write_config_for_nodes([2,3], [], [0,1])
        node_reload_config(0, 4)
        node_trigger_scaling(0, 4)

        # Check that all the timers have popped
        sleep(12)
        self.assert_correct_timers_received(100)

    def test_scale_up_and_kill(self):
        # Test that scaling up definitely moves timers. This test creates 1
        # Chronos node and adds 100 timers. It then scales up to 2 Chronos
        # nodes, then kills the first node. It then checks all 100 timers pop

        # Start initial nodes and add timers
        self.write_config_for_nodes([0])
        start_nodes(0, 1)
        create_timers(chronos_nodes[0], 0, 100)

        # Scale up
        self.write_config_for_nodes([0], [1])
        start_nodes(1, 2)
        node_reload_config(0, 1)
        node_trigger_scaling(0, 2)

        # Now kill the first node
        kill_nodes(0, 1)

        # Check that all the timers have popped
        sleep(12)
        self.assert_correct_timers_received(100)

if __name__ == '__main__':
    unittest.main()
