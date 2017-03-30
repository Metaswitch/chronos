#! /usr/bin/python2.7

# @file chronos_delete_timers.py
#
# Project Clearwater - IMS in the Cloud
# Copyright (C) 2017  Metaswitch Networks Ltd
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
