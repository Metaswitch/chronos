# @file chronos_plugin.py
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

from textwrap import dedent
from metaswitch.clearwater.cluster_manager.plugin_base import SynchroniserPluginBase
from metaswitch.clearwater.cluster_manager.plugin_utils import WARNING_HEADER
from metaswitch.common.alarms import alarm_manager
from metaswitch.clearwater.cluster_manager import pdlogs, alarm_constants, constants
from metaswitch.clearwater.etcd_shared.plugin_utils import run_command, safely_write
import logging

_log = logging.getLogger("chronos_plugin")

def write_chronos_cluster_settings(filename, cluster_view, current_server, instance_id, deployment_id):
    joining = [constants.JOINING_ACKNOWLEDGED_CHANGE,
               constants.JOINING_CONFIG_CHANGED]
    staying = [constants.NORMAL_ACKNOWLEDGED_CHANGE,
               constants.NORMAL_CONFIG_CHANGED,
               constants.NORMAL]
    leaving = [constants.LEAVING_ACKNOWLEDGED_CHANGE,
               constants.LEAVING_CONFIG_CHANGED]

    joining_servers = ([k for k, v in cluster_view.iteritems()
                        if v in joining])
    staying_servers = ([k for k, v in cluster_view.iteritems()
                        if v in staying])
    leaving_servers = ([k for k, v in cluster_view.iteritems()
                        if v in leaving])

    contents = dedent('''\
        {}
        [identity]
        instance_id = {}
        deployment_id = {}

        [cluster]
        localhost = {}
        ''').format(WARNING_HEADER, instance_id, deployment_id, current_server)

    for node in joining_servers:
        contents += 'joining = {}\n'.format(node)
    for node in staying_servers:
        contents += 'node = {}\n'.format(node)
    for node in leaving_servers:
        contents += 'leaving = {}\n'.format(node)

    safely_write(filename, contents)

class ChronosPlugin(SynchroniserPluginBase):
    def __init__(self, params):
        self.local_server = params.ip
        uuid_bytes = params.uuid.bytes

        # Extract a 7-bit instance ID and a three-bit deployment ID from the
        # UUID.
        self.instance_id = ord(uuid_bytes[0]) & 0b0111111
        self.deployment_id = ord(uuid_bytes[1]) & 0b00000111
        if self.instance_id > 127 or self.deployment_id > 7:
            _log.error("instance_id/deployment_id are out of expected range - %d and %d (max should be 127 and 7)", self.instance_id, self.deployment_id)
        self._key = "/{}/{}/{}/clustering/chronos".format(params.etcd_key, params.local_site, params.etcd_cluster_key)
        self._alarm = alarm_manager.get_alarm(
            'cluster-manager',
            alarm_constants.CHRONOS_NOT_YET_CLUSTERED)
        pdlogs.NOT_YET_CLUSTERED_ALARM.log(cluster_desc=self.cluster_description())

    def key(self):
        return self._key

    def files(self):
        return ["/etc/chronos/chronos_cluster.conf"]

    def cluster_description(self):
        return "local Chronos cluster"

    def on_cluster_changing(self, cluster_view):
        self._alarm.set()
        self.write_cluster_settings(cluster_view)

    def on_joining_cluster(self, cluster_view):
        self._alarm.set()
        self.write_cluster_settings(cluster_view)

    def on_new_cluster_config_ready(self, cluster_view):
        self._alarm.set()
        run_command("service chronos resync")
        run_command("service chronos wait-sync")

    def on_stable_cluster(self, cluster_view):
        self.write_cluster_settings(cluster_view)
        self._alarm.clear()

    def on_leaving_cluster(self, cluster_view):
        pass

    def write_cluster_settings(self, cluster_view):
        write_chronos_cluster_settings("/etc/chronos/chronos_cluster.conf",
                                       cluster_view,
                                       self.local_server,
                                       self.instance_id,
                                       self.deployment_id)
        run_command("service chronos reload")

def load_as_plugin(params):
    _log.info("Loading the Chronos plugin")
    return ChronosPlugin(params)
