## Chronos configuration

Chronos's configuration is set up in two files. `/etc/chronos/chronos.conf` contains configuration options for
the individual Chronos process, and `/etc/chronos/chronos_cluster.conf` contains the configuration options that
control how the Chronos process clusters with other Chronos processes. 

The per-node configuration file has the following format:

    [http]
    bind-address = 1.2.3.4         # Address to bind the HTTP server to
    bind-port = 7253               # Port to bind the HTTP server to
    threads = 50                   # Number of HTTP threads to create
    
    [logging]
    folder = /var/log/chronos      # Location to output logs to
    level = 2                      # Logging level: 1(lowest) - 5(highest)

    [exceptions]
    max_ttl = 600                  # The maximum time before Chronos exits if it hits an exception

    [dns]  
    servers = 127.0.0.1            # DNS servers to use (up to three allowed)

A sample configuration is provided [here](https://github.com/Metaswitch/chronos/blob/dev/etc/chronos/chronos.conf.sample). To use this configuration, copy it to `/etc/chronos/chronos.conf`, and change the `bind_address` to the IP of the Chronos node. 

To update the per node configuration, make the desired changes in `etc/chronos/chronos.conf` and restart the Chronos service (e.g. run `service chronos stop` and allow monit to restart Chronos).

The cluster configuration file has the following format:

    [cluster]
    localhost = 1.2.3.4            # The address of the local host
    node = 1.2.3.4                 # The addresses of all nodes in the cluster. If the
    node = 1.2.3.5                 # address doesn't include a port, then the bind-port
    node = 1.2.3.6                 # value is used.
    leaving = 2.3.4.5              # The addresses of all nodes that are leaving
    leaving = 2.3.4.6              # the cluster (only used during scale-down)

Details of how to set up the configuration for clustering is [here](https://github.com/Metaswitch/chronos/blob/dev/doc/clustering.md).

To update the cluster configuration, make the desired changes in `etc/chronos/chronos_cluster.conf`, and reload Chronos (e.g. `service chronos reload`). This doesn't impact service. 

### Migration

We used to keep both types of configuration in the same file (in `/etc/chronos/chronos.conf`). To move to the new configuration files, you should move anything under the `[cluster]` section in `/etc/chronos/chronos.conf` to a new file `/etc/chronos/chronos_cluster.conf`. We have provided a script that does this for you; you can run this with:

    sudo /usr/share/clearwater/bin/chronos_configuration_split.py [--current "Your current configuration file"] [--cluster "Your new cluster configuration file"] 

The script uses `/etc/chronos/chronos.conf` as your current configuration file and `/etc/chronos/chronos_cluster` as your new cluster configuration file if you don't provide an alternative
