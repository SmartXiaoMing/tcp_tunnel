Another TCP-TUNNEL

We can visit a machine behind a NAT or firewall.


## Tunnle server side

./tunnel --mode=server

## Tunnel client side

./tunnle --mode=client

## Server monitor side

./tunnel --mode=monitor

## TODO list

* add client connect tunnel after prepare client.traffic
* when client is ready for traffic, report to server
* close or lower the client after client echo reset
* before read traffic client buffer, check tunnel client sendBuffer, then put the whole read as tunnelPackage
* when traffic client is connecting or disconnecting, put the CREATE package to tunnel client sendBuffer
* when write error on one side, tell the other side close on send buffer is flushed.
* before read tunnel client buffer, we don not care about the traffic client sendBuffer, just read then put directly always.

Report tunnel bugs to 95813422(at)qq.com.
