// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2013-2014 The Offerings developers
// Copyright (c) 2026 The Offerings Conclave / SubGenius.Finance community
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miningpage.h"
#include "ui_miningpage.h"

#include "walletmodel.h"

#include "init.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "stratum.h"
#include "util.h"
#include "utilstrencodings.h"
#include "wallet.h"

#include <QSettings>
#include <QStandardItemModel>
#include <QThread>
#include <QTimer>

// dHashesPerSec is the same global the gethashespersec / getmininginfo RPCs
// read. It lives inside ENABLE_WALLET in miner.cpp - solo mining needs a
// wallet to pay the reward into - and this page is only built in a wallet
// build, so it is always here.
static const int kMiningPollMs = 2000;

// "When the wallet opens" choices, in the order of the miningAutoStart combo box.
enum { AUTOSTART_OFF = 0, AUTOSTART_SOLO = 1, AUTOSTART_POOL = 2 };
static const char* const kAutoStartNames[] = { "off", "solo", "pool" };
static const int kAutoStartPollMs = 5000;
// How long a peer has to have been connected before a matching block height
// means "caught up". Right after connecting, the best header we know of is
// still our own tip until the peer's headers arrive, so tip == best header
// says nothing for the first few seconds.
static const int64_t kAutoStartPeerSettleSecs = 30;

MiningPage::MiningPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiningPage),
    model(0),
    pollTimer(0),
    autoStartTimer(0),
    fAutoStartDone(false),
    nPeersSince(0)
{
    ui->setupUi(this);

    int idealThreads = QThread::idealThreadCount();
    if (idealThreads < 1) idealThreads = 1;
    ui->miningThreads->setMaximum(idealThreads);

    pollTimer = new QTimer(this);
    pollTimer->setInterval(kMiningPollMs);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateMiningStatus()));
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updatePoolMiningStatus()));

    // Reflect generation already started from -gen on the command line, so the
    // checkbox does not claim we are idle while threads are grinding.
    if (GetBoolArg("-gen", false))
    {
        ui->miningEnable->blockSignals(true);
        ui->miningEnable->setChecked(true);
        ui->miningEnable->blockSignals(false);
        int nArgThreads = GetArg("-genproclimit", 1);
        if (nArgThreads > 0)
        {
            ui->miningThreads->blockSignals(true);
            ui->miningThreads->setValue(qBound(1, nArgThreads, idealThreads));
            ui->miningThreads->blockSignals(false);
        }
    }
    else
    {
        QSettings settings;
        ui->miningThreads->blockSignals(true);
        ui->miningThreads->setValue(qBound(1, settings.value("soloMiningThreads", 1).toInt(), idealThreads));
        ui->miningThreads->blockSignals(false);
    }

    // Pool mode: restore last-used settings; reflect a client already started
    // via -stratum/-stratumuser command-line args.
    {
        QSettings settings;
        // Default to the low-difficulty pool port. This REVERSES the earlier
        // "no default on purpose" decision, whose own stated condition was
        // "pool mode stays opt-in until a low-difficulty port exists". It now
        // exists: 3033 starts at difficulty 4 with varDiff minDiff 2, where a
        // ~8 kH/s CPU lands its first share in about a minute. The old note
        // also said solo was the zero-config path because a CPU solved a block
        // in minutes -- that stopped being true when network difficulty went
        // from 3.3 to the hundreds, so solo got harder as pool got easier.
        //
        // Leaving this blank sent people to 3032, which starts at difficulty
        // 15000 for ASICs and rented hashrate. There the wallet hashes, reports
        // a healthy rate, and the pool never sees a share -- indistinguishable
        // from the stratum bug fixed in v0.13.7, and the reason that bug hid
        // for as long as it did. A saved value always wins over this default.
        ui->poolMiningEndpoint->setText(
            settings.value("poolMiningEndpoint", "pool.dobbscoin.info:3033").toString());
        ui->poolMiningAddress->setText(settings.value("poolMiningAddress", "").toString());
        // The only way this box arrives filled is a previous session on this
        // computer: no address ships with the wallet. Say so, so nobody takes
        // it for a preset that pays someone else.
        ui->poolMiningAddressNote->setVisible(!ui->poolMiningAddress->text().isEmpty());
        int nSavedThreads = settings.value("poolMiningThreads", 1).toInt();
        ui->poolMiningThreads->setMaximum(idealThreads);
        ui->poolMiningThreads->setValue(qBound(1, nSavedThreads, idealThreads));
        if (g_pStratumClient && g_pStratumClient->IsRunning())
        {
            ui->poolMiningToggle->setChecked(true);
            ui->poolMiningToggle->setText(tr("Stop Pool Mining"));
        }
    }

    // When the wallet opens: restore the saved choice. Off unless someone
    // picked otherwise -- a fresh install never starts using the CPU unasked.
    {
        QSettings settings;
        QString strSaved = settings.value("miningAutoStart", "off").toString();
        int nChoice = AUTOSTART_OFF;
        for (int i = 0; i < 3; i++)
            if (strSaved == kAutoStartNames[i]) nChoice = i;
        updateAutoStartChoices();
        ui->miningAutoStart->blockSignals(true);
        ui->miningAutoStart->setCurrentIndex(nChoice);
        ui->miningAutoStart->blockSignals(false);
    }

    autoStartTimer = new QTimer(this);
    autoStartTimer->setInterval(kAutoStartPollMs);
    connect(autoStartTimer, SIGNAL(timeout()), this, SLOT(autoStartSoloWhenCaughtUp()));

    updateMiningStatus();
    updatePoolMiningStatus();
}

MiningPage::~MiningPage()
{
    delete ui;
}

void MiningPage::setModel(WalletModel *model)
{
    this->model = model;
    if (model)
        autoStart();
}

void MiningPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateMiningStatus();
    updatePoolMiningStatus();
    if (pollTimer)
        pollTimer->start();
}

void MiningPage::hideEvent(QHideEvent *event)
{
    // Mining keeps running when you leave the page - only the status poll stops.
    if (pollTimer)
        pollTimer->stop();
    QWidget::hideEvent(event);
}

// ============================================================================
// Solo mode
// ============================================================================

void MiningPage::on_miningEnable_toggled(bool checked)
{
    // A hand on the checkbox overrides a solo start still waiting to catch up.
    if (autoStartTimer)
        autoStartTimer->stop();
    int nThreads = ui->miningThreads->value();
    // Same two lines setgenerate runs, so getgenerate/getmininginfo stay honest.
    mapArgs["-gen"] = (checked ? "1" : "0");
    mapArgs["-genproclimit"] = itostr(nThreads);
    GenerateDobbscoins(checked, pwalletMain, nThreads);

    updateMiningStatus();
}

void MiningPage::on_miningThreads_valueChanged(int value)
{
    // If we're currently mining, restart generation to pick up the thread-count
    // change. Otherwise the new value just sits in the spinner and takes effect
    // on the next toggle-on. Saved either way, for the next time the wallet
    // opens and starts solo mining by itself.
    QSettings().setValue("soloMiningThreads", value);
    if (ui->miningEnable->isChecked())
    {
        mapArgs["-genproclimit"] = itostr(value);
        GenerateDobbscoins(true, pwalletMain, value);
    }
}

void MiningPage::updateMiningStatus()
{
    if (!ui->miningEnable->isChecked())
    {
        if (autoStartTimer && autoStartTimer->isActive())
            ui->miningStatus->setText(tr("Mining starts once the wallet has caught up with the network…"));
        else
            ui->miningStatus->setText(tr("Mining stopped."));
        return;
    }

    double hps = dHashesPerSec;
    if (hps <= 0.0)
    {
        ui->miningStatus->setText(tr("Mining starting…"));
    }
    else if (hps < 1000.0)
    {
        ui->miningStatus->setText(tr("Mining at %1 H/s").arg(hps, 0, 'f', 1));
    }
    else if (hps < 1e6)
    {
        ui->miningStatus->setText(tr("Mining at %1 kH/s").arg(hps / 1e3, 0, 'f', 2));
    }
    else
    {
        ui->miningStatus->setText(tr("Mining at %1 MH/s").arg(hps / 1e6, 0, 'f', 2));
    }
}

// ============================================================================
// Pool mode (in-wallet stratum client).
// Talks directly to the in-process g_pStratumClient - same pattern as the
// dHashesPerSec global the solo section reads.
// ============================================================================

void MiningPage::on_poolMiningToggle_clicked(bool checked)
{
    if (!checked)
    {
        StopStratum();
        ui->poolMiningToggle->setText(tr("Start Pool Mining"));
        ui->poolMiningEndpoint->setEnabled(true);
        ui->poolMiningAddress->setEnabled(true);
        ui->poolMiningThreads->setEnabled(true);
        updatePoolMiningStatus();
        return;
    }

    // Validate inputs before spinning anything up.
    QString strEndpoint = ui->poolMiningEndpoint->text().trimmed();
    QString strAddress = ui->poolMiningAddress->text().trimmed();
    int nColon = strEndpoint.lastIndexOf(':');
    QString strHost = (nColon > 0) ? strEndpoint.left(nColon) : QString();
    int nPort = (nColon > 0) ? strEndpoint.mid(nColon + 1).toInt() : 0;

    QString strProblem;
    if (strHost.isEmpty() || nPort <= 0 || nPort > 65535)
        strProblem = tr("Enter the pool as host:port.");
    else if (strAddress.isEmpty() || (model && !model->validateAddress(strAddress)))
        strProblem = tr("Enter a valid (BOB) pay-to address.");

    if (!strProblem.isEmpty())
    {
        ui->poolMiningToggle->blockSignals(true);
        ui->poolMiningToggle->setChecked(false);
        ui->poolMiningToggle->blockSignals(false);
        ui->poolMiningStatus->setText(strProblem);
        return;
    }

    // Replace any prior client (also covers a client left over from
    // -stratum command-line args or the setstratum RPC) with one built
    // from the form.
    if (!StartStratum(strHost.toStdString(), nPort, strAddress.toStdString(),
                      ui->poolMiningThreads->value()))
    {
        ui->poolMiningToggle->blockSignals(true);
        ui->poolMiningToggle->setChecked(false);
        ui->poolMiningToggle->blockSignals(false);
        ui->poolMiningStatus->setText(tr("Failed to start the pool client."));
        return;
    }

    QSettings settings;
    settings.setValue("poolMiningEndpoint", strEndpoint);
    settings.setValue("poolMiningAddress", strAddress);
    settings.setValue("poolMiningThreads", ui->poolMiningThreads->value());
    updateAutoStartChoices();

    ui->poolMiningToggle->setText(tr("Stop Pool Mining"));
    ui->poolMiningEndpoint->setEnabled(false);
    ui->poolMiningAddress->setEnabled(false);
    ui->poolMiningThreads->setEnabled(false);
    ui->poolMiningStatus->setText(tr("Connecting to %1…").arg(strEndpoint));
}

void MiningPage::updatePoolMiningStatus()
{
    if (!g_pStratumClient || !g_pStratumClient->IsRunning())
    {
        if (!ui->poolMiningToggle->isChecked())
            ui->poolMiningStatus->setText(tr("Pool mining stopped."));
        return;
    }

    if (!g_pStratumClient->IsConnected())
    {
        QString strErr = QString::fromStdString(g_pStratumClient->GetLastError());
        ui->poolMiningStatus->setText(strErr.isEmpty()
            ? tr("Connecting…")
            : tr("Reconnecting… (%1)").arg(strErr));
        return;
    }
    if (!g_pStratumClient->IsAuthorized())
    {
        ui->poolMiningStatus->setText(tr("Connected — authorizing…"));
        return;
    }

    double dRate = g_pStratumClient->GetHashRate();
    QString strRate;
    if (dRate < 1000.0)
        strRate = tr("%1 H/s").arg(dRate, 0, 'f', 1);
    else if (dRate < 1e6)
        strRate = tr("%1 kH/s").arg(dRate / 1e3, 0, 'f', 2);
    else
        strRate = tr("%1 MH/s").arg(dRate / 1e6, 0, 'f', 2);

    ui->poolMiningStatus->setText(tr("Offering at %1 · share diff %2 · accepted %3 / rejected %4")
        .arg(strRate)
        .arg(g_pStratumClient->GetDifficulty())
        .arg(g_pStratumClient->GetSharesAccepted())
        .arg(g_pStratumClient->GetSharesRejected()));
}

// ============================================================================
// When the wallet opens (#47).
// Starts one mode, with the settings the page already remembers. It never
// stops anything: changing the choice only changes what happens next time.
// ============================================================================

void MiningPage::on_miningAutoStart_currentIndexChanged(int index)
{
    if (index < 0 || index > AUTOSTART_POOL)
        return;
    QSettings().setValue("miningAutoStart", kAutoStartNames[index]);
}

void MiningPage::updateAutoStartChoices()
{
    QStandardItemModel *choices = qobject_cast<QStandardItemModel*>(ui->miningAutoStart->model());
    if (!choices || !choices->item(AUTOSTART_POOL))
        return;
    bool fHaveAddress = !QSettings().value("poolMiningAddress", "").toString().isEmpty();
    choices->item(AUTOSTART_POOL)->setEnabled(fHaveAddress);
    choices->item(AUTOSTART_POOL)->setToolTip(fHaveAddress ? QString() :
        tr("Pool mine once with the form below first. That saves the pay-to address this uses."));
}

void MiningPage::autoStart()
{
    if (fAutoStartDone)
        return;
    fAutoStartDone = true;

    // gen= or stratum= in dobbscoin.conf or on the command line is in charge;
    // this choice never starts a second miner beside it.
    if (mapArgs.count("-gen") || mapArgs.count("-stratum"))
        return;

    switch (ui->miningAutoStart->currentIndex())
    {
    case AUTOSTART_SOLO:
        if (!ui->miningEnable->isChecked())
        {
            autoStartTimer->start();
            autoStartSoloWhenCaughtUp();
            updateMiningStatus();
        }
        break;
    case AUTOSTART_POOL:
        // Same path as pressing Start Pool Mining, so the same checks run and
        // a bad saved pool or address shows up in the status line.
        if (!ui->poolMiningToggle->isChecked())
            ui->poolMiningToggle->click();
        break;
    }
}

void MiningPage::autoStartSoloWhenCaughtUp()
{
    // Not IsInitialBlockDownload(): on this chain it stays true whenever the
    // newest block is more than a day old, and (BOB) can sit that long with
    // nobody mining (see the note in miner.cpp). Caught up here means: a peer
    // has been connected long enough to tell us its headers, and we have every
    // block up to the best header we know of.
    if (fImporting || fReindex)
        return;
    bool fHavePeers;
    {
        LOCK(cs_vNodes);
        fHavePeers = !vNodes.empty();
    }
    if (!fHavePeers)
    {
        nPeersSince = 0;
        return;
    }
    if (nPeersSince == 0)
        nPeersSince = GetTime();
    if (GetTime() - nPeersSince < kAutoStartPeerSettleSecs)
        return;
    {
        LOCK(cs_main);
        if (!chainActive.Tip() || !pindexBestHeader || chainActive.Height() < pindexBestHeader->nHeight)
            return;
    }
    autoStartTimer->stop();
    ui->miningEnable->setChecked(true);   // runs on_miningEnable_toggled
}
