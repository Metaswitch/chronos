#! /usr/bin/python2.7

# @file chronos_fv_test.py
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

from flask import Flask, request
import logging
import json
import requests
import threading
import os
import sys
import shutil
import signal
import random
from subprocess import Popen
from time import sleep
from collections import namedtuple
from textwrap import dedent
import unittest

# Test the resynchronization operations for Chronos.
# These tests use multiple Chronos processes that run on the same machine.
# The tests set up a Chronos cluster and add timers to it. They then
# perform scaling operations, and check that the correct number of
# timers still pop
CHRONOS_BINARY = 'build/bin/chronos'
PID_PATTERN = 'scripts/pid/chronos.fvtest.%i.pid'
CONFIG_FILE_PATTERN = 'scripts/log/chronos.fvtest.conf%i'
CLUSTER_CONFIG_FILE_PATTERN = 'scripts/log/chronos.cluster.fvtest.conf%i'
GR_CONFIG_FILE_PATTERN = 'scripts/log/chronos.gr.fvtest.conf%i'
LOG_FILE_DIR = 'scripts/log/'
LOG_FILE_PATTERN = LOG_FILE_DIR + 'chronos%s'

Node = namedtuple('Node', 'ip port')
flask_server = Node(ip='127.0.0.10', port='5001')
chronos_nodes = [
    Node(ip='127.0.0.11', port='7253'),
    Node(ip='127.0.0.12', port='7254'),
    Node(ip='127.0.0.13', port='7255'),
    Node(ip='127.0.0.14', port='7256'),
]

receiveCount = 0
processes = []
timerCounts = []
timerIDs = []

# Create log folders for each Chronos process. These are useful for
# debugging any problems. Running the tests deletes the logs from the
# previous run
for file_name in os.listdir(LOG_FILE_DIR):
    file_path = os.path.join(LOG_FILE_DIR, file_name)
    if os.path.isfile(file_path) and file_path != (LOG_FILE_DIR + '.gitignore'):
        os.unlink(file_path)
    elif os.path.isdir(file_path):
        shutil.rmtree(file_path)
for node in chronos_nodes:
    log_path = LOG_FILE_PATTERN % node.port
    os.mkdir(log_path)

# Raise the logging level of the Flask app, to silence it during normal tests
log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)

# Open /dev/null to redirect stdout and stderr of chronos. This avoids spamming
# the console during tests - comment this out to get the logs when debugging
FNULL = open(os.devnull, 'w')

# The Flask app. This is used to make timer requests and receive timer pops
app = Flask(__name__)


@app.route('/pop', methods=['POST'])
def pop():
    global receiveCount
    receiveCount += 1
    global timerCounts
    timerCounts.append(request.data)
    return 'success'


def run_app():
    app.run(host=flask_server.ip, port=flask_server.port)


# Helper functions for the Chronos tests
def start_nodes(lower, upper):
    # Start nodes with indexes [lower, upper) and allow them time to start
    for i in range(lower, upper):
        Popen([CHRONOS_BINARY, '--daemon', '--pidfile', PID_PATTERN % i,
              '--config-file', CONFIG_FILE_PATTERN % i,
              '--cluster-config-file', CLUSTER_CONFIG_FILE_PATTERN % i,
              '--gr-config-file', GR_CONFIG_FILE_PATTERN % i],
              stdout=FNULL, stderr=FNULL)
        sleep(2)

        f = open(PID_PATTERN % i)
        processes.append(f.read())
        f.close()


def kill_nodes(lower, upper):
    # Kill nodes with indexes [lower, upper)
    for p in processes[lower: upper]:
        try:
            os.kill(int(p), signal.SIGKILL)
        except OSError as e:
            if 'No such process' not in str(e):
                # At the end of the test we try and delete any Chronos
                # processes - it's fine for them to have already been
                # killed
                raise


def kill_random_nodes(count):
    # Kill a random count of the processes
    kill_list = random.sample(processes, count)
    for p in kill_list:
        os.kill(int(p), signal.SIGKILL)


def node_reload_config(lower, upper):
    # SIGHUP nodes with indexes [lower, upper)
    for p in processes[lower: upper]:
        os.kill(int(p), signal.SIGHUP)
    sleep(2)


def node_trigger_scaling(lower, upper):
    # SIGHUSR1 nodes with indexes [lower, upper)
    for p in processes[lower: upper]:
        os.kill(int(p), signal.SIGUSR1)
    sleep(2)


def create_timers(target, num, max_num):
    # Create and send timer requests. These are all sent to the first Chronos
    # process which will replicate the timers out to the other Chronos processes
    global timerIDs
    body_dict = {
        'timing': {
            'interval': 10,
            'repeat_for': 10,
        },
        'callback': {
            'http': {
                'uri': 'http://%s:%s/pop' % (flask_server.ip, flask_server.port),
                'opaque': 'REPLACE',
            }
        }
    }

    # Set the number of the timer in the opaque data - this way we can check we
    # get the correct timers back
    for i in range(num, max_num):
        body_dict['callback']['http']['opaque'] = str(i)
        r = requests.post('http://%s:%s/timers' % (target.ip, target.port),
                          data=json.dumps(body_dict)
                          )
        timerIDs.append(r.headers['location'])
        assert r.status_code == 200, 'Received unexpected status code: %i' % r.status_code

def delete_timers(target, num):
    for i in range(num):
        r = requests.delete('http://%s:%s%s' % (target.ip, target.port, timerIDs[i]))
        assert r.status_code == 200, 'Received unexpected status code: %i' % r.status_code

def write_conf(filename, this_node):
    # Create a configuration file for a chronos process. Use a generous token
    # bucket size so we can make lots of requests quickly.
    log_path = LOG_FILE_PATTERN % this_node.port
    with open(filename, 'w') as f:
        f.write(dedent("""\
        [http]
        bind-address = {this_node.ip}
        bind-port = {this_node.port}

        [throttling]
        max_tokens = 1000

        [logging]
        folder = {log_path}
        level = 5
        """).format(**locals()))


def write_cluster_conf(filename, this_node, joining, nodes, leaving):
    # Create a configuration file for a chronos process
    with open(filename, 'w') as f:
        f.write(dedent("""\
        [cluster]
        localhost = {this_node.ip}:{this_node.port}
        """).format(**locals()))
        for node in joining:
            f.write('joining = {node.ip}:{node.port}\n'.format(**locals()))
        for node in nodes:
            f.write('node = {node.ip}:{node.port}\n'.format(**locals()))
        for node in leaving:
            f.write('leaving = {node.ip}:{node.port}\n'.format(**locals()))


def write_gr_conf(filename, local_site, remote_sites):
    # Create a GR configuration file for a chronos process.
    with open(filename, 'w') as f:
        f.write(dedent("""\
        [sites]
        local_site = {}
        """).format(local_site))
        for site in remote_sites:
            f.write('remote_site = {site}\n'.format(**locals()))

# Test the resynchronization operations for Chronos.
class ChronosFVTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        # Start the flask app in its own thread
        threads = []
        t = threading.Thread(target=run_app)
        t.daemon = True
        threads.append(t)
        t.start()
        sleep(1)

    def setUp(self):
        # Track the Chronos processes and timer pops
        global receiveCount
        global processes
        global timerCounts
        receiveCount = 0
        processes = []
        timerCounts = []

    def tearDown(self):
        # Kill all the Chronos processes
        kill_nodes(0, len(processes))

    def assert_correct_timers_received(self, expected_number):
        # Check that enough timers pop as expected.
        # This should be as many as were added in the first place.
        # Ideally, we'd be checking where the timers popped from, but that's
        # not possible with these tests (as everything looks like it comes
        # from 127.0.0.1)
        self.assertEqual(receiveCount,
                         expected_number,
                         ('Incorrect number of popped timers: received %i, expected exactly %i' %
                         (receiveCount, expected_number)))

        for i in range(expected_number):
            assert str(i) in timerCounts, "Missing timer pop for %i" % i

    def write_config_for_nodes(self, staying, joining=[], leaving=[]):
        # Write configuration files for the nodes
        for num in staying + joining + leaving:
            write_conf(CONFIG_FILE_PATTERN % num,
                       chronos_nodes[num])
            write_cluster_conf(CLUSTER_CONFIG_FILE_PATTERN % num,
                               chronos_nodes[num],
                               [chronos_nodes[i] for i in joining],
                               [chronos_nodes[i] for i in staying],
                               [chronos_nodes[i] for i in leaving])

    def write_gr_config_for_nodes(self, nodes, local_site, remote_sites):
        # Write configuration files for the nodes
        for num in nodes:
            write_conf(CONFIG_FILE_PATTERN % num,
                       chronos_nodes[num])
            write_cluster_conf(CLUSTER_CONFIG_FILE_PATTERN % num,
                               this_node=chronos_nodes[num],
                               joining=[],
                               nodes=[chronos_nodes[i] for i in nodes],
                               leaving=[])
            write_gr_conf(GR_CONFIG_FILE_PATTERN % num,
                          local_site, remote_sites)
