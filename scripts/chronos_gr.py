#! /usr/bin/python2.7

# @file chronos_gr.py
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
from chronos_fv_test import ChronosFVTest, start_nodes, create_timers, kill_nodes, chronos_nodes, delete_timers, kill_random_nodes, kill_specific_nodes

# Test the GR capabilities of Chronos.
class ChronosGRTest(ChronosFVTest):

    def test_gr_no_site_failure(self):
        # Test that a GR system works. This test creates 2 Chronos nodes in
        # different sites, adds 100 timers, then checks that 100 timers pop.

        # Start initial nodes and add timers
        self.write_shared_config_for_nodes([0], 'site1', ['site2=127.0.0.12:7254'], 'true')
        self.write_shared_config_for_nodes([1], 'site2', ['site1=127.0.0.11:7253'], 'true')
        start_nodes(0, 2)
        create_timers(chronos_nodes[0], 0, 100)

        # Check that all the timers have popped (10 secs with a slight delay
        # for replication)
        sleep(12)
        self.assert_correct_timers_received(100)

    def test_gr_site_failures(self):
        # Test that timers still pop on site failure. This test creates 4
        # Chronos nodes in different sites, adds 25 timers to each site, waits
        # for these to be replicated, kills sites 1-3, then checks that
        # 100 timers pop.

        # Start initial nodes and add timers
        self.write_shared_config_for_nodes([0], 'site1', ['site2=127.0.0.12:7254','site3=127.0.0.13:7255','site4=127.0.0.14:7256'], 'true')
        self.write_shared_config_for_nodes([1], 'site2', ['site1=127.0.0.11:7253','site3=127.0.0.13:7255','site4=127.0.0.14:7256'], 'true')
        self.write_shared_config_for_nodes([2], 'site3', ['site1=127.0.0.11:7253','site2=127.0.0.12:7254','site4=127.0.0.14:7256'], 'true')
        self.write_shared_config_for_nodes([3], 'site4', ['site1=127.0.0.11:7253','site2=127.0.0.12:7254','site3=127.0.0.13:7255'], 'true')
        start_nodes(0, 4)
        create_timers(chronos_nodes[0], 0, 25)
        create_timers(chronos_nodes[1], 25, 50)
        create_timers(chronos_nodes[2], 50, 75)
        create_timers(chronos_nodes[3], 75, 100)
        sleep(4)

        # Now kill all but a single site, and check that all timers pop within
        # expected delay (10 seconds, plus 6 seconds delay for the site failures,
        # plus a slight delay for replication)
        kill_random_nodes(3)
        sleep(18)
        self.assert_correct_timers_received(100)

    def test_gr_deleted_timers(self):
        # Test that a GR system works. This test creates 2 Chronos nodes in
        # different sites, adds 100 timers from site 1, waits for replication,
        # then deletes them from site 2 and checks that no timers pop.

        # Start initial nodes and add timers
        self.write_shared_config_for_nodes([0], 'site1', ['site2=127.0.0.12:7254'], 'true')
        self.write_shared_config_for_nodes([1], 'site2', ['site1=127.0.0.11:7253'], 'true')
        start_nodes(0, 2)
        create_timers(chronos_nodes[0], 0, 50)
        create_timers(chronos_nodes[1], 50, 100)
        sleep(2)

        # Check that no timers pop (where we'd expect them to take a maximum
        # of 10 seconds plus 2 second site delay plus replication delay)
        delete_timers(chronos_nodes[1], 100)
        sleep(14)
        self.assert_correct_timers_received(0)

    def test_no_gr(self):
        # Test that a system works correctly when GR is disabled. This test
        # creates 2 Chronos nodes in different sites, adds 25 timers to each,
        # then waits for them to pop, and checks 50 timers have popped.

        # Start initial noders and add timers.
        # GR is disabled through a shared config option.
        self.write_shared_config_for_nodes([0], 'site1', ['site2=127.0.0.12:7254'], 'false')
        self.write_shared_config_for_nodes([1], 'site2', ['site1=127.0.0.11:7253'], 'false')
        start_nodes(0, 2)
        create_timers(chronos_nodes[0], 0, 25)
        create_timers(chronos_nodes[1], 25, 50)

        # Check that all the timers have popped (after 10 secs with a slight
        # delay for replication).
        sleep(12)
        self.assert_correct_timers_received(50)

    def test_no_gr_site_failures(self):
        # Confirm that GR is disabled by checking that the timers on a site are
        # lost if that site goes down. This test creates 2 Chronos nodes in
        # different sites, adds 25 timers to each site, waits long enough for
        # them to be replicated (even though they shouldn't be), kills 1 site,
        # and then checks that only 25 timers pop.

        # Start initial nodes and add timers.
        # GR is disabled through a shared config option.
        self.write_shared_config_for_nodes([0], 'site1', ['site2=127.0.0.12:7254','site3=127.0.0.13:7255','site4=127.0.0.14:7256'], 'false')
        self.write_shared_config_for_nodes([1], 'site2', ['site1=127.0.0.11:7253','site3=127.0.0.13:7255','site4=127.0.0.14:7256'], 'false')
        start_nodes(0, 2)
        create_timers(chronos_nodes[0], 0, 25)
        create_timers(chronos_nodes[1], 25, 50)
        sleep(4)

        # Now kill the node in the second site, wait a sufficient time for any
        # existing timers to pop (10 seconds, plus 6 seconds delay for the site
        # failures, plus a slight delay for replication), and check that only
        # the timers set in the first site pop.
        kill_specific_nodes([1])
        sleep(18)
        self.assert_correct_timers_received(25)

if __name__ == '__main__':
    unittest.main()
