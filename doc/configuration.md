## Chronos configuration

Chronos's configuration is set up in `/etc/chronos/chronos.conf`, and takes the following format:

    [http]
    bind-address = 1.2.3.4         # Address to bind the HTTP server to
    bind-port = 7253               # Port to bind the HTTP server to
    threads = 50                   # Number of HTTP threads to create
    
    [cluster]
    localhost = 1.2.3.4            # The address of the local host
    node = 1.2.3.4                 # The addresses of all nodes in the cluster. If the 
    node = 1.2.3.5                 # address doesn't include a port, then the bind-port
    node = 1.2.3.6                 # value is used. 
    leaving = 2.3.4.5              # The addresses of all nodes that are leaving
    leaving = 2.3.4.6              # the cluster (only used during scale-down)

    [logging]
    folder = /var/log/chronos      # Location to output logs to
    level = 2                      # Logging level: 1(lowest) - 5(highest)

    [alarms]
    enabled = true                 # Whether SNMP alarms are enabled

    [exceptions]
    max_ttl = 600                  # The maximum time before Chronos exits if it hits an exception

    [dns]  
    servers = 127.0.0.1            # DNS servers to use (up to three allowed)

A sample configuration is provided [here](https://github.com/Metaswitch/chronos/blob/dev/etc/chronos/chronos.conf.sample). To use this configuration, copy it to `/etc/chronos/chronos.conf`, and change the bind_address, localhost and node values to the IP of the Chronos node. Details of the configuration changes needed for clustering is [here](https://github.com/Metaswitch/chronos/blob/dev/doc/clustering.md).

To update the Chronos configuration, make the desired changes in `etc/chronos/chronos.conf`. Changes to the `http` section require that the Chronos service is restarted (e.g. run `service chronos stop` and allow monit to restart Chronos); all other changes can be detected by sending a SIGHUP to the Chronos service.
