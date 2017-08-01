## Chronos configuration

Chronos's configuration is set up in three files.

*   `/etc/chronos/chronos.conf` contains configuration options for the
    individual Chronos process.
*   `/etc/chronos/chronos_cluster.conf` contains the configuration options that
    control how the Chronos process clusters with other Chronos processes.
*   `/etc/chronos/chronos_shared.conf` contains the configuration options that
    are site wide (e.g. how the Chronos cluster connects to other clusters for
    geographic redundancy.)

The per-node configuration file has the following format:

    [http]
    bind-address = 1.2.3.4         # Address to bind the HTTP server to
    bind-port = 7253               # Port to bind the HTTP server to
    threads = 50                   # Number of HTTP threads to create

    [logging]
    folder = /var/log/chronos      # Location to output logs to
    level = 2                      # Logging level: 1(lowest) - 5(highest)

A sample configuration is provided [here](https://github.com/Metaswitch/chronos/blob/dev/chronos.root/etc/chronos/chronos.conf.sample). To use this configuration, copy it to `/etc/chronos/chronos.conf`, and change the `bind_address` to the IP of the Chronos node.

To update the per node configuration, make the desired changes in `/etc/chronos/chronos.conf` and restart the Chronos service (e.g. run `service chronos stop` and allow monit to restart Chronos).

The cluster configuration file has the following format:

    [cluster]
    localhost = 1.2.3.4            # The address of the local host
    joining = 3.4.5.6              # The addresses of all nodes that are joining
    joining = 3.4.5.7              # the cluster (only used during scale-up)
    node = 1.2.3.4                 # The addresses of all nodes in the cluster. If the
    node = 1.2.3.5                 # address doesn't include a port, then the bind-port
    node = 1.2.3.6                 # value is used.
    leaving = 2.3.4.5              # The addresses of all nodes that are leaving
    leaving = 2.3.4.6              # the cluster (only used during scale-down)

Details of how to set up the configuration for clustering is [here](https://github.com/Metaswitch/chronos/blob/dev/doc/clustering.md).

To update the cluster configuration, make the desired changes in `/etc/chronos/chronos_cluster.conf`, and reload Chronos (e.g. `service chronos reload`). This doesn't impact service.

The shared configuration file has the following format:

    [exceptions]
    max_ttl = 600                            # The maximum time before Chronos exits if it hits an exception

    [dns]
    servers = 127.0.0.1                      # DNS servers to use (up to three allowed)
    timeout = 200                            # Amount of time to wait for a DNS response

    [sites]
    local_site = local-site-name             # The name of the local site
    remote_site = site-b=bar.foo.com:8000    # The name of a remote site, and a DNS name resolving to
    remote_site = site-c=cat.foo.com:7000    # all Chronos nodes in that remote site. If the address
    remote_site = site-d=delta.foo.com:5000  # doesn't include a port the bind-port will be used.
    remote_site = site-e=echo.foo.com        # Each site is listed in a separate entry

To update the shared configuration, make the desired changes in `/etc/chronos/chronos_shared.conf` and restart the Chronos service (e.g. run `service chronos stop` and allow monit to restart Chronos).

In addition, when setting up a geographically redundant Chronos deployment, you should set up local DNS config so that Chronos callbacks can be redirected to a server running in the local site. Details of how to do so are [here](http://clearwater.readthedocs.io/en/stable/Modifying_Clearwater_settings.html#modifying-dns-config)
