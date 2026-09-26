### Seeds ###

Utility to generate the seeds.txt list that is compiled into the client
(see [src/chainparamsseeds.h](/src/chainparamsseeds.h) and [share/seeds](/share/seeds)).

`makeseeds.py` filters a DNS seeder's dump (the `dnsseed.dump` format of
[bitcoin-seeder](https://github.com/sipa/bitcoin-seeder)) down to good
(BOB) nodes: IPv4 on port 19985, a `/EndTimes:0.13.x/` or `/EndTimes:0.14.x/`
user agent, a recent height and good uptime:

	python3 makeseeds.py < dnsseed.dump > ../../share/seeds/nodes_main.txt

Then regenerate the header:

	python3 ../../share/seeds/generate-seeds.py ../../share/seeds > ../../src/chainparamsseeds.h

(BOB) has no seeder dump today. The current list was written by hand from
the nodes behind the DNS seeds; `share/seeds/nodes_main.txt` says how.
