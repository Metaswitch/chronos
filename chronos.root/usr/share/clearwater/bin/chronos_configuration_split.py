#! /usr/bin/python
#
# @file chronos_configuration_split.py
#
# Copyright (C) Metaswitch Networks
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

import ConfigParser
from collections import OrderedDict
import argparse
import os
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--current', default='/etc/chronos/chronos.conf', help='The current Chronos configuration')
parser.add_argument('--cluster', default='/etc/chronos/chronos_cluster.conf', help='The new Chronos cluster configuration')
args = parser.parse_args()

# Bail out early if the cluster configuration file already exists
if os.path.exists(args.cluster): 
    sys.exit("Exiting as the cluster configuration file already exists")

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
