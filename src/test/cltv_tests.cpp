// Copyright (c) 2026 The Dobbscoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "chainparamsbase.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "script/script.h"
#include "script/script_error.h"

#include <limits>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(cltv_tests)

namespace {

// CheckLockTime reads exactly two things off the spending transaction: its
// nLockTime, and whether the input being verified is final.
static CTransaction SpendingTx(unsigned int nLockTime, uint32_t nSequence)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].nSequence = nSequence;
    tx.vout.resize(1);
    tx.nLockTime = nLockTime;
    return CTransaction(tx);
}

// Run "<claimed> CHECKLOCKTIMEVERIFY" against a spending transaction.
static bool Eval(int64_t claimed, const CTransaction& txTo, unsigned int flags,
                 ScriptError* err)
{
    CScript script = CScript() << CScriptNum(claimed) << OP_CHECKLOCKTIMEVERIFY;
    std::vector<std::vector<unsigned char> > stack;
    TransactionSignatureChecker checker(&txTo, 0);
    return EvalScript(stack, script, flags, checker, err);
}

const uint32_t SEQ_NONFINAL = 0;
const uint32_t SEQ_FINAL    = std::numeric_limits<uint32_t>::max();

// Below this an nLockTime is a block height, at or above it a UNIX timestamp.
const int64_t THRESHOLD = 500000000;

} // namespace

// ---------------------------------------------------------------------------
// Before activation the opcode must behave exactly as OP_NOP2 did. This is the
// property that makes activation a soft fork rather than a chain split.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(cltv_inactive_is_a_nop)
{
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    // A locktime that could never be satisfied still passes while the flag is off.
    const CTransaction tx = SpendingTx(0, SEQ_FINAL);
    BOOST_CHECK(Eval(999999, tx, SCRIPT_VERIFY_NONE, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);
}

BOOST_AUTO_TEST_CASE(cltv_inactive_still_discouraged_when_asked)
{
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    const CTransaction tx = SpendingTx(0, SEQ_FINAL);
    BOOST_CHECK(!Eval(999999, tx, SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS);
}

// ---------------------------------------------------------------------------
// After activation.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(cltv_satisfied_by_height)
{
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    const CTransaction tx = SpendingTx(500, SEQ_NONFINAL);
    BOOST_CHECK(Eval(500, tx, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_OK);

    // An output may also be spent later than its lock demands.
    const CTransaction later = SpendingTx(600, SEQ_NONFINAL);
    BOOST_CHECK(Eval(500, later, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
}

BOOST_AUTO_TEST_CASE(cltv_unsatisfied_is_rejected)
{
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    const CTransaction tx = SpendingTx(500, SEQ_NONFINAL);
    // Spending one block too early.
    BOOST_CHECK(!Eval(501, tx, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
}

BOOST_AUTO_TEST_CASE(cltv_negative_locktime_is_rejected)
{
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    const CTransaction tx = SpendingTx(500, SEQ_NONFINAL);
    BOOST_CHECK(!Eval(-1, tx, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_NEGATIVE_LOCKTIME);
}

BOOST_AUTO_TEST_CASE(cltv_final_input_defeats_the_lock)
{
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    // nLockTime is satisfied on its face, but a final input means the
    // transaction could be mined immediately, so the lock has no force.
    const CTransaction tx = SpendingTx(500, SEQ_FINAL);
    BOOST_CHECK(!Eval(500, tx, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
}

BOOST_AUTO_TEST_CASE(cltv_height_and_timestamp_do_not_mix)
{
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;

    // Script demands a timestamp; transaction carries a height.
    const CTransaction byHeight = SpendingTx(500, SEQ_NONFINAL);
    BOOST_CHECK(!Eval(THRESHOLD + 1, byHeight, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_UNSATISFIED_LOCKTIME);

    // Script demands a height; transaction carries a timestamp.
    const CTransaction byTime = SpendingTx(THRESHOLD + 1, SEQ_NONFINAL);
    BOOST_CHECK(!Eval(500, byTime, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_UNSATISFIED_LOCKTIME);

    // Both timestamps: fine.
    BOOST_CHECK(Eval(THRESHOLD, byTime, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
}

BOOST_AUTO_TEST_CASE(cltv_empty_stack_is_rejected)
{
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    const CTransaction tx = SpendingTx(500, SEQ_NONFINAL);
    CScript script = CScript() << OP_CHECKLOCKTIMEVERIFY;
    std::vector<std::vector<unsigned char> > stack;
    TransactionSignatureChecker checker(&tx, 0);
    BOOST_CHECK(!EvalScript(stack, script, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, checker, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_INVALID_STACK_OPERATION);
}

BOOST_AUTO_TEST_CASE(cltv_accepts_a_five_byte_operand)
{
    // The operand limit is 5 bytes, not the usual 4: a block height fits in 4
    // but a post-2038 UNIX timestamp does not. A 5-byte push must therefore
    // reach the locktime comparison and fail THERE, rather than being thrown
    // out as a script number overflow.
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    const CTransaction tx = SpendingTx(THRESHOLD + 1, SEQ_NONFINAL);
    const int64_t fiveBytes = 4294967296LL;   // 2^32, needs 5 bytes signed
    BOOST_CHECK(!Eval(fiveBytes, tx, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, &err));
    BOOST_CHECK_EQUAL(err, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
}

BOOST_AUTO_TEST_CASE(cltv_leaves_its_argument_on_the_stack)
{
    // BIP65 does not pop. Real scripts are "<n> CLTV DROP ...", and a version
    // that popped would silently change every such script.
    const CTransaction tx = SpendingTx(500, SEQ_NONFINAL);
    CScript script = CScript() << CScriptNum(500) << OP_CHECKLOCKTIMEVERIFY;
    std::vector<std::vector<unsigned char> > stack;
    TransactionSignatureChecker checker(&tx, 0);
    ScriptError err = SCRIPT_ERR_UNKNOWN_ERROR;
    BOOST_CHECK(EvalScript(stack, script, SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, checker, &err));
    BOOST_CHECK_EQUAL(stack.size(), 1U);
}

// ---------------------------------------------------------------------------
// Activation height. Deployed by height, not by version supermajority --
// IsSuperMajority compares the raw nVersion, and from the AuxPoW fork the chain
// ID makes every block read 11,534,339, so a version gate would be true
// unconditionally.
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(cltv_fork_height_per_network)
{
    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(CLTVForkHeight(), HARDFORK_CLTV_MAIN);
    BOOST_CHECK_EQUAL(CLTVForkHeight(), 2000000);

    // Mainnet CLTV and AuxPoW deliberately share a height: one upgrade event.
    BOOST_CHECK_EQUAL(CLTVForkHeight(), AuxPowForkHeight());

    // The fork-height helpers all key off AllowMinDifficultyBlocks, which is
    // true for testnet and regtest only. CUnitTestParams derives from
    // CMainParams and leaves it false, so it reports MAINNET heights -- check
    // the test-network heights under regtest, not under the unit-test params.
    SelectParams(CBaseChainParams::REGTEST);
    BOOST_CHECK_EQUAL(CLTVForkHeight(), HARDFORK_CLTV_TESTNET);
    // On the test networks the two forks are separated so a regtest chain can
    // cross each boundary on its own.
    BOOST_CHECK(CLTVForkHeight() > AuxPowForkHeight());

    SelectParams(CBaseChainParams::UNITTEST);
}

BOOST_AUTO_TEST_SUITE_END()
