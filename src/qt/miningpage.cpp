// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2013-2014 The Offerings developers
// Copyright (c) 2026 The Offerings Conclave / SubGenius.Finance community
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miningpage.h"
#include "ui_miningpage.h"

#include "walletmodel.h"

#include "init.h"
#include "miner.h"
#include "stratum.h"
#include "util.h"
#include "utilstrencodings.h"
#include "wallet.h"

#include <QSettings>
#include <QThread>
#include <QTimer>

// dHashesPerSec is the same global the gethashespersec / getmininginfo RPCs
// read. It lives inside ENABLE_WALLET in miner.cpp - solo mining needs a
// wallet to pay the reward into - and this page is only built in a wallet
// build, so it is always here.
static const int kMiningPollMs = 2000;

MiningPage::MiningPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiningPage),
    model(0),
    pollTimer(0)
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

    // Pool mode: restore last-used settings; reflect a client already started
    // via -stratum/-stratumuser command-line args.
    {
        QSettings settings;
        // No default pool endpoint on purpose. Solo is the zero-config path here:
        // at the current network difficulty a single CPU core solves a block in
        // minutes and keeps the whole reward. The live pool port (3032) is the
        // ASIC port at minDiff 512 -- a CPU would need ~14 years per share there,
        // so defaulting to it would look broken. Pool mode stays opt-in until a
        // low-difficulty port exists (see (OFF) port 3040, minDiff 0.0001).
        ui->poolMiningEndpoint->setText(
            settings.value("poolMiningEndpoint", "").toString());
        ui->poolMiningAddress->setText(settings.value("poolMiningAddress", "").toString());
        int nSavedThreads = settings.value("poolMiningThreads", 1).toInt();
        ui->poolMiningThreads->setMaximum(idealThreads);
        ui->poolMiningThreads->setValue(qBound(1, nSavedThreads, idealThreads));
        if (g_pStratumClient && g_pStratumClient->IsRunning())
        {
            ui->poolMiningToggle->setChecked(true);
            ui->poolMiningToggle->setText(tr("Stop Pool Mining"));
        }
    }

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
    // on the next toggle-on.
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
