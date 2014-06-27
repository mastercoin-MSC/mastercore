Master Core integration/staging tree
=================================================

What is the Master Protocol
----------------------------
The Master Protocol is a communications protocol that uses the Bitcoin block chain to enable features such as smart contracts, user currencies and decentralized peer-to-peer exchanges. A common analogy that is used to describe the relation of the Master Protocol to Bitcoin is that of HTTP to TCP/IP: HTTP, like the Master Protocol, is the application layer to the more fundamental transport and internet layer of TCP/IP, like Bitcoin.

http://www.mastercoin.org

What is Master Core
---------------------------

Master Core is a fast, portable Master Protocol implementation that is based off the Bitcoin Core codebase (currently 0.9.1). This implementation requires no external dependencies extraneous to Bitcoin Core, and is native to the Bitcoin network just like other Bitcoin nodes. It currently has two modes, in its wallet form it will be seamlessly available on 3 platforms: Windows, Linux & Mac OS, and its node form exposes Master Protocol extensions via JSON-RPC. Development has been consolidated on the Master Core product, and once officially released it will become the reference client for the Master Protocol.

Disclaimer, Warning
--------------

This software is EXPERIMENTAL software for **TESTNET TRANSACTIONS** only. *USE ON MAINNET AT YOUR OWN RISK.*

The protocol and transaction processing rules for Mastercoin are still under active development and are subject to change in future. 

Master Core should be considered an alpha-level product, and you use it at your own risk.  Neither the Mastercoin Foundation nor the Master Core developers assumes any responsibility for funds misplaced, mishandled, lost, or misallocated.

Further, please note that this particular installation of Master Core should be viewed as experimental.  Your wallet data may be lost, deleted, or corrupted, with or without warning due to bugs or glitches. Please take caution.

This software is provided open-source at no cost.  You are responsible for knowing the law in your country and determining if your use of this software contravenes any local laws.

*You all know, BUT: DO NOT use wallet(s) with significant amount of any currency while working!*

Testnet
-------------------

1. To run Master Core in testnet mode, run mastercore with the following option in place: ``` -testnet ```.
2. Add your address to the list of addresses in your Bitcoin data Testnet dir (usually: ~/.bitcoin/testnet3/mastercoin_balances.txt) to give yourself testnet MSC. 

All functions in this mode will be TESTNET-ONLY (eg. send_MP).

Preseeding
--------------------

During initial development balances have been pre-seeded up until block 290629 (the block prior to the Distributed Exchange launch).  Please copy the preseed file into your bitcoin data directory prior to first run. As persistence has been achieved pre-seeding will be removed during future development. 

To install:

* Add the mastercoin-balances.txt file in the Bitcoin data dir ( usually: ~/.bitcoin/ )


Installation
------------

*NOTE: This will only build on Linux for now.*

```
./autogen
./configure
make
```

Known Issues:
----------------
* Payments for DEx transactions not currently available in history

* Transactions before preseed (290630) not currently available in history

* Feel free to open more Github issues with other new bugs or improvement suggestions

* Make sure send_MP returns an appropriate error code when out of funds
Pending additions:

* Bug on fee calculation in gettransaction_MP - unreliable
------------------

* gettransaction_MP output should display matched sell offer txid

