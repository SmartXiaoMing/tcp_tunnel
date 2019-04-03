Another TCP-TUNNEL

We can visit a machine behind a NAT or firewall.


## how to use

    # start tunnel broker
    ./tunnel_broker --port 8120

    # start tunel agent peers
    ./tunnel_agent --brokerPort 8120 --serverPort 9000 --targetAddress 127.0.0.1:22 --name jack
    ./tunnel_agent --brokerPort 8120 --name jack

    # try below
    ssh 127.0.0.1:9000

## note

the two tunnel_agent with the same name is a pair.
we can visit A.serverPort to reach A.targetAddress through B.
when A.targetAddress is not provided, the agent A will guess.

Report tunnel bugs to 95813422(at)qq.com.
