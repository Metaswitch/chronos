#! /usr/bin/python2.7

# @file chronos_gr.py
#
# Project Clearwater - IMS in the Cloud
# Copyright (C) 2015  Metaswitch Networks Ltd
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

import unittest
from time import sleep

from os import sys, path
sys.path.append(path.dirname(path.abspath(__file__)))
from chronos_fv_test import ChronosFVTest, start_nodes, create_timers, kill_nodes, chronos_nodes, delete_timers, kill_random_nodes

# Test the GR capabilities of Chronos.
class ChronosGRTest(ChronosFVTest):

    def test_gr_no_site_failure(self):
        # Test that a GR system works. This test creates 2 Chronos nodes in
        # different sites, adds 100 timers, then checks that 100 timers pop.

        # Start initial nodes and add timers
        self.write_gr_config_for_nodes([0], 'site1', ['site2=127.0.0.12:7254'])
        self.write_gr_config_for_nodes([1], 'site2', ['site1=127.0.0.11:7253'])
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
        self.write_gr_config_for_nodes([0], 'site1', ['site2=127.0.0.12:7254','site3=127.0.0.13:7255','site4=127.0.0.14:7256'])
        self.write_gr_config_for_nodes([1], 'site2', ['site1=127.0.0.11:7253','site3=127.0.0.13:7255','site4=127.0.0.14:7256'])
        self.write_gr_config_for_nodes([2], 'site3', ['site1=127.0.0.11:7253','site2=127.0.0.12:7254','site4=127.0.0.14:7256'])
        self.write_gr_config_for_nodes([3], 'site4', ['site1=127.0.0.11:7253','site2=127.0.0.12:7254','site3=127.0.0.13:7255'])
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
        self.write_gr_config_for_nodes([0], 'site1', ['site2=127.0.0.12:7254'])
        self.write_gr_config_for_nodes([1], 'site2', ['site1=127.0.0.11:7253'])
        start_nodes(0, 2)
        create_timers(chronos_nodes[0], 0, 50)
        create_timers(chronos_nodes[1], 50, 100)
        sleep(2)

        # Check that no timers pop (where we'd expect them to take a maximum
        # of 10 seconds plus 2 second site delay plus replication delay)
        delete_timers(chronos_nodes[1], 100)
        sleep(14)
        self.assert_correct_timers_received(0)

if __name__ == '__main__':
    unittest.main()
