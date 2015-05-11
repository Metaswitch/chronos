#! /usr/bin/python
#
# @file chronos_configuration_split.py
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

import ConfigParser
from collections import OrderedDict
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--current', default='/etc/chronos/chronos.conf', help='The current Chronos configuration')
parser.add_argument('--cluster', default='/etc/chronos/chronos_cluster.conf', help='The new Chronos cluster configuration')
args = parser.parse_args()

CLUSTER_SECTIONS = ['cluster']

# We're using this rather than the plain configparse as that can't 
# deal with duplicate keys
class MultiOrderedDict(OrderedDict):
    def __setitem__(self, key, value):
        if isinstance(value, list) and key in self:
            self[key].extend(value)
        else:
            super(MultiOrderedDict, self).__setitem__(key, value)

def write_section(section, file_obj):
    f.writelines('[%s]\n' % section)
    for key, val in config.items(section):
        # This is required to undo the multiline-value handling 
        # done by ConfigParser
        for v in val.split('\n'):
            file_obj.writelines('%s=%s\n' % (key, v))

config = ConfigParser.ConfigParser(dict_type=MultiOrderedDict)
config.read(args.current)

with open(args.cluster, 'w') as f:
    for section in config.sections():
        if section in CLUSTER_SECTIONS:
            write_section(section, f)

with open(args.current + ".bak", 'w') as f:
    for section in config.sections():
        write_section(section, f)
        f.write('\n')

with open(args.current, 'w') as f: 
    for section in config.sections():
        if section not in CLUSTER_SECTIONS:
            write_section(section, f)
            f.write('\n')
