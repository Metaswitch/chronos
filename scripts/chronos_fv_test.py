#! /usr/bin/python2.7

# @file chronos_fv_test.py
#
# Copyright (C) Metaswitch Networks 2017
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

from flask import Flask, request
import flask
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
CHRONOS_LIBS = 'usr/lib/'
PID_PATTERN = 'scripts/pid/chronos.fvtest.%i.pid'
CONFIG_FILE_PATTERN = 'scripts/log/chronos.fvtest.conf%i'
CLUSTER_CONFIG_FILE_PATTERN = 'scripts/log/chronos.cluster.fvtest.conf%i'
SHARED_CONFIG_FILE_PATTERN = 'scripts/log/chronos.shared.fvtest.conf%i'
LOG_FILE_DIR = 'scripts/log/'
LOG_FILE_PATTERN = LOG_FILE_DIR + 'chronos%s'

Node = namedtuple('Node', 'ip port')
flask_server = Node(ip='127.0.0.10', port=5001)
chronos_nodes = [
    Node(ip='127.0.0.11', port='7253'),
    Node(ip='127.0.0.12', port='7254'),
    Node(ip='127.0.0.13', port='7255'),
    Node(ip='127.0.0.14', port='7256'),
]

successReceiveCount = 0
failureReceiveCount = 0
processes = []
timerIDs = []
successTimerCounts = []
failureTimerCounts = []

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
    global successReceiveCount
    successReceiveCount += 1
    global successTimerCounts
    successTimerCounts.append(request.data)
    return 'success'


@app.route('/pop-with-errors', methods=['POST'])
def pop_with_errors():
    # The first time a timer pops, it fails with 503. Any other replicas will
    # succeed
    global successTimerCounts
    global failureTimerCounts
    global successReceiveCount
    global failureReceiveCount

    if request.data in failureTimerCounts:
        successReceiveCount += 1
        successTimerCounts.append(request.data)
        return 'success'
    else:
        failureReceiveCount += 1
        failureTimerCounts.append(request.data)
        return flask.make_response("failure", 503)


def run_app():
    app.run(host=flask_server.ip, port=flask_server.port)


# Helper functions for the Chronos tests
def start_nodes(lower, upper):
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = CHRONOS_LIBS

    # Start nodes with indexes [lower, upper) and allow them time to start
    for i in range(lower, upper):
        Popen([CHRONOS_BINARY, '--daemon', '--pidfile', PID_PATTERN % i,
              '--local-config-file', CONFIG_FILE_PATTERN % i,
              '--cluster-config-file', CLUSTER_CONFIG_FILE_PATTERN % i,
              '--shared-config-file', SHARED_CONFIG_FILE_PATTERN % i],
              stdout=FNULL, stderr=FNULL, env=environment)
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


def kill_specific_nodes(nodes):
    # Kill a set of specified nodes. These are specified by the order in which
    # they were set up (ie. node set up first is node 0, etc.).
    kill_list = []
    for node in nodes:
        kill_list.append(processes[node])
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


def create_timers(target, num, max_num, path="pop", replicas=2):
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
                'uri': 'http://%s:%s/%s' % (flask_server.ip, flask_server.port, path),
                'opaque': 'REPLACE',
            }
        },
        'reliability': {
            'replication-factor': replicas
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


def write_shared_conf(filename, local_site, remote_sites, replicate_across_sites):
    # Create a shared configuration file for a chronos process.
    with open(filename, 'w') as f:
        f.write(dedent("""\
        [sites]
        local_site = {}
        """).format(local_site))
        for site in remote_sites:
            f.write('remote_site = {site}\n'.format(**locals()))
        # TODO - make this neater
        replicate = 0
        if replicate_across_sites:
            replicate = 1
        f.write('replicate_timers_across_sites = {}'.format(replicate))

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
        global successReceiveCount
        global failureReceiveCount
        global processes
        global successTimerCounts
        global failureTimerCounts
        successReceiveCount = 0
        failureReceiveCount = 0
        processes = []
        successTimerCounts = []
        failureTimerCounts = []

    def tearDown(self):
        # Kill all the Chronos processes
        kill_nodes(0, len(processes))

    def assert_correct_timers_received(self, expected_number):
        # Check that enough timers pop as expected.
        # This should be as many as were added in the first place.
        # Ideally, we'd be checking where the timers popped from, but that's
        # not possible with these tests (as everything looks like it comes
        # from 127.0.0.1)
        self.assertEqual(successReceiveCount,
                         expected_number,
                         ('Incorrect number of popped timers: received %i, expected exactly %i' %
                         (successReceiveCount, expected_number)))

        for i in range(expected_number):
            assert str(i) in successTimerCounts, "Missing timer pop for %i" % i


    def assert_correct_timers_failed(self, expected_number):
        # Check that the expected number of timers failed.
        # This should be as many as were added in the first place if using the
        # "pop-with-errors" path.
        self.assertEqual(failureReceiveCount,
                         expected_number,
                         ('Incorrect number of popped timers failed: actual %i, expected exactly %i' %
                         (failureReceiveCount, expected_number)))

        for i in range(expected_number):
            assert str(i) in failureTimerCounts, "Missing timer failure for %i" % i

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

    def write_shared_config_for_nodes(self, nodes, local_site, remote_sites, replicate_across_sites):
        # Write configuration files for the nodes
        for num in nodes:
            write_conf(CONFIG_FILE_PATTERN % num,
                       chronos_nodes[num])
            write_cluster_conf(CLUSTER_CONFIG_FILE_PATTERN % num,
                               this_node=chronos_nodes[num],
                               joining=[],
                               nodes=[chronos_nodes[i] for i in nodes],
                               leaving=[])
            write_shared_conf(SHARED_CONFIG_FILE_PATTERN % num,
                              local_site, remote_sites, replicate_across_sites)
