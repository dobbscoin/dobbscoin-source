#!/usr/bin/env python3
# Copyright (c) 2017-2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test that the wallet resends transactions periodically."""
import time

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import ToHex
from test_framework.p2p import P2PTxInvStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class ResendWalletTransactionsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        node = self.nodes[0]  # alias

        peer_first = node.add_p2p_connection(P2PTxInvStore())

        self.log.info("Create a new transaction and wait until it's broadcast")
        txid = node.sendtoaddress(node.getnewaddress(), 1)

        # Wallet rebroadcast is first scheduled 1 sec after startup (see
        # nNextResend in ResendWalletTransactions()). Tell scheduler to call
        # MaybeResendWalletTxn now to initialize nNextResend before the first
        # setmocktime call below.
        node.mockscheduler(1)

        # Can take a few seconds due to transaction trickling
        peer_first.wait_for_broadcast([txid])

        # Add a second peer since txs aren't rebroadcast to the same peer (see filterInventoryKnown)
        peer_second = node.add_p2p_connection(P2PTxInvStore())

        self.log.info("Create a block")
        # Create and submit a block without the transaction.
        # Transactions are only rebroadcast if there has been a block at least five minutes
        # after the last time we tried to broadcast. Use mocktime and give an extra minute to be sure.
        block_time = int(time.time()) + 6 * 60
        node.setmocktime(block_time)
        # (BOB): the block must carry the AuxPoW chain ID (regtest forks at 10), which
        # test_framework.blocktools does not stamp. generateblock goes through the node's
        # own BlockAssembler, and the empty transaction list keeps our transaction unconfirmed.
        height_before = node.getblockcount()
        node.generateblock(node.getnewaddress(), [])
        assert_equal(node.getblockcount(), height_before + 1)

        node.syncwithvalidationinterfacequeue()
        now = int(time.time())

        # (BOB) #54: the wallet re-sends every 5-15 minutes (Core 0.21: 12-36 hours). A re-send
        # may already fire at the block above (mocktime +6 min), which puts the next one at
        # +21 min at the latest, so +22 is certain.
        self.log.info("Bump time & check that transaction is rebroadcast within 22 minutes")
        with node.assert_debug_log(['ResendWalletTransactions: resubmit 1 unconfirmed transactions']):
            node.setmocktime(now + 22 * 60)
            node.mockscheduler(1)  # Tell scheduler to call MaybeResendWalletTxn now

        self.log.info("Check that it does not re-send again a minute later")
        with node.assert_debug_log([], unexpected_msgs=['ResendWalletTransactions: resubmit']):
            node.setmocktime(now + 23 * 60)
            node.mockscheduler(1)
        assert_equal(int(txid, 16) in peer_second.get_invs(), False)

        # Give some time for trickle to occur
        node.setmocktime(now + 33 * 60)
        node.mockscheduler(1)
        peer_second.wait_for_broadcast([txid])


if __name__ == '__main__':
    ResendWalletTransactionsTest().main()
