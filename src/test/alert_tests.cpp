// Copyright (c) 2013 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for alert system
//

#include "alert.h"
#include "clientversion.h"
#include "data/alertTests.raw.h"

#include "serialize.h"
#include "streams.h"
#include "util.h"
#include "utilstrencodings.h"

#include <fstream>

#include <boost/filesystem/operations.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>

#if 0
//
// alertTests contains 7 alerts, generated with this code:
// (SignAndSave code not shown, alert signing key is secret)
//
{
    CAlert alert;
    alert.nRelayUntil   = 60;
    alert.nExpiration   = 24 * 60 * 60;
    alert.nID           = 1;
    alert.nCancel       = 0;   // cancels previous messages up to this ID number
    alert.nMinVer       = 0;  // These versions are protocol versions
    alert.nMaxVer       = 999001;
    alert.nPriority     = 1;
    alert.strComment    = "Alert comment";
    alert.strStatusBar  = "Alert 1";

    SignAndSave(alert, "test/alertTests");

    alert.setSubVer.insert(std::string("/Satoshi:0.1.0/"));
    alert.strStatusBar  = "Alert 1 for Satoshi 0.1.0";
    SignAndSave(alert, "test/alertTests");

    alert.setSubVer.insert(std::string("/Satoshi:0.2.0/"));
    alert.strStatusBar  = "Alert 1 for Satoshi 0.1.0, 0.2.0";
    SignAndSave(alert, "test/alertTests");

    alert.setSubVer.clear();
    ++alert.nID;
    alert.nCancel = 1;
    alert.nPriority = 100;
    alert.strStatusBar  = "Alert 2, cancels 1";
    SignAndSave(alert, "test/alertTests");

    alert.nExpiration += 60;
    ++alert.nID;
    SignAndSave(alert, "test/alertTests");

    ++alert.nID;
    alert.nMinVer = 11;
    alert.nMaxVer = 22;
    SignAndSave(alert, "test/alertTests");

    ++alert.nID;
    alert.strStatusBar  = "Alert 2 for Satoshi 0.1.0";
    alert.setSubVer.insert(std::string("/Satoshi:0.1.0/"));
    SignAndSave(alert, "test/alertTests");

    ++alert.nID;
    alert.nMinVer = 0;
    alert.nMaxVer = 999999;
    alert.strStatusBar  = "Evil Alert'; /bin/ls; echo '";
    alert.setSubVer.clear();
    SignAndSave(alert, "test/alertTests");
}
#endif

