// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2014-2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/miningpage.h>
#include <qt/forms/ui_miningpage.h>

#include <qt/guiutil.h>
#include <qt/walletmodel.h>

#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <net.h>
#include <outputtype.h>
#include <script/standard.h>
#include <stratum.h>
#include <util/system.h>
#include <util/time.h>

#include <QSettings>
#include <QStandardItemModel>
#include <QThread>
#include <QTimer>

#include <memory>

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

static QString FormatRate(double hps)
{
    if (hps < 1000.0) return QObject::tr("%1 H/s").arg(hps, 0, 'f', 1);
    if (hps < 1e6) return QObject::tr("%1 kH/s").arg(hps / 1e3, 0, 'f', 2);
    return QObject::tr("%1 MH/s").arg(hps / 1e6, 0, 'f', 2);
}

MiningPage::MiningPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MiningPage)
{
    ui->setupUi(this);

    // This checkbox sits straight on the background tile, which is near-black
    // in the dark theme. Fusion draws the box outline as the Window color
    // darkened, so lift Window (and the box fill) for this widget only (#49).
    if (GUIUtil::IsDarkTheme()) {
        QPalette pal = ui->miningEnable->palette();
        pal.setColor(QPalette::Window, QColor(0x8a, 0x8a, 0x8a));
        pal.setColor(QPalette::Base, QColor(0x3a, 0x3a, 0x3a));
        ui->miningEnable->setPalette(pal);
    }

    int idealThreads = QThread::idealThreadCount();
    if (idealThreads < 1) idealThreads = 1;
    ui->miningThreads->setMaximum(idealThreads);

    pollTimer = new QTimer(this);
    pollTimer->setInterval(kMiningPollMs);
    connect(pollTimer, &QTimer::timeout, this, &MiningPage::updateMiningStatus);
    connect(pollTimer, &QTimer::timeout, this, &MiningPage::updatePoolMiningStatus);

    {
        QSettings settings;
        ui->miningThreads->blockSignals(true);
        int nThreads = settings.value("soloMiningThreads", 1).toInt();
        // Reflect -gen=1 -genproclimit=n from the command line or dobbscoin.conf.
        if (gArgs.GetBoolArg("-gen", false) && gArgs.GetArg("-genproclimit", 1) > 0) {
            nThreads = (int)gArgs.GetArg("-genproclimit", 1);
        }
        ui->miningThreads->setValue(qBound(1, nThreads, idealThreads));
        ui->miningThreads->blockSignals(false);
    }

    // Pool mode: restore last-used settings.
    {
        QSettings settings;
        // Default to the low-difficulty pool port: 3033 starts at difficulty 4 with
        // varDiff minDiff 2, where a ~8 kH/s CPU lands its first share in about a
        // minute. 3032 starts at 15000 for ASICs and rented hashrate; there the wallet
        // hashes, reports a healthy rate, and the pool never sees a share. A saved
        // value always wins over this default.
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
    connect(autoStartTimer, &QTimer::timeout, this, &MiningPage::autoStartSoloWhenCaughtUp);
}

MiningPage::~MiningPage()
{
    // The solo miner holds this page's wallet (for coinbase addresses). If this page
    // started it, stop it with the page, so closing the wallet releases it.
    if (fStartedSolo && model) {
        std::string error;
        model->node().bobSetGenerate(false, 0, nullptr, error);
    }
    delete ui;
}

void MiningPage::setModel(WalletModel *_model)
{
    this->model = _model;
    if (model) {
        updateMiningStatus();
        updatePoolMiningStatus();
        autoStart();
    }
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

bool MiningPage::setSoloMining(bool on, int threads)
{
    if (!model) return false;
    interfaces::Node& node = model->node();
    std::string error;
    if (!on) {
        fStartedSolo = false;
        return node.bobSetGenerate(false, 0, nullptr, error);
    }
    // The miner runs on the node side; give it its own handle on this wallet.
    std::shared_ptr<interfaces::Wallet> wallet;
    const std::string name = model->wallet().getWalletName();
    for (auto& w : node.walletClient().getWallets()) {
        if (w->getWalletName() == name) {
            wallet = std::shared_ptr<interfaces::Wallet>(std::move(w));
            break;
        }
    }
    if (!wallet) return false;
    auto coinbase = [wallet](CScript& script, std::string& err) {
        CTxDestination dest;
        if (!wallet->getNewDestination(OutputType::LEGACY, "", dest)) {
            err = "the wallet could not give a new address (keypool empty or wallet locked?)";
            return false;
        }
        script = GetScriptForDestination(dest);
        return true;
    };
    fStartedSolo = node.bobSetGenerate(true, threads, coinbase, error);
    return fStartedSolo;
}

void MiningPage::on_miningEnable_toggled(bool checked)
{
    // A hand on the checkbox overrides a solo start still waiting to catch up.
    if (autoStartTimer)
        autoStartTimer->stop();
    if (!setSoloMining(checked, ui->miningThreads->value()) && checked) {
        ui->miningEnable->blockSignals(true);
        ui->miningEnable->setChecked(false);
        ui->miningEnable->blockSignals(false);
        ui->miningStatus->setText(tr("Could not start mining: this wallet cannot give a new address."));
        return;
    }
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
        setSoloMining(true, value);
}

void MiningPage::updateMiningStatus()
{
    if (!model) return;
    interfaces::Node& node = model->node();
    // The miner is one per node: show what it is doing, whoever started it.
    const bool generating = node.bobIsGenerating();
    if (ui->miningEnable->isChecked() != generating && !(autoStartTimer && autoStartTimer->isActive())) {
        ui->miningEnable->blockSignals(true);
        ui->miningEnable->setChecked(generating);
        ui->miningEnable->blockSignals(false);
    }
    if (!generating) {
        if (autoStartTimer && autoStartTimer->isActive())
            ui->miningStatus->setText(tr("Mining starts once the wallet has caught up with the network…"));
        else
            ui->miningStatus->setText(tr("Mining stopped."));
        return;
    }

    const double hps = node.bobHashesPerSec();
    if (hps <= 0.0)
        ui->miningStatus->setText(tr("Mining starting…"));
    else
        ui->miningStatus->setText(tr("Mining at %1").arg(FormatRate(hps)));
}

// ============================================================================
// Pool mode (in-wallet stratum client)
// ============================================================================

void MiningPage::on_poolMiningToggle_clicked(bool checked)
{
    if (!model) return;
    interfaces::Node& node = model->node();
    if (!checked)
    {
        node.bobStopStratum();
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
    else if (strAddress.isEmpty() || !model->validateAddress(strAddress.section('.', 0, 0)))
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
    if (!node.bobStartStratum(strHost.toStdString(), nPort, strAddress.toStdString(),
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
    if (!model) return;
    const StratumStatus st = model->node().bobStratumStatus();

    // The pool client is one per node (it may also come from -stratum or setstratum).
    if (ui->poolMiningToggle->isChecked() != st.running) {
        ui->poolMiningToggle->blockSignals(true);
        ui->poolMiningToggle->setChecked(st.running);
        ui->poolMiningToggle->blockSignals(false);
        ui->poolMiningToggle->setText(st.running ? tr("Stop Pool Mining") : tr("Start Pool Mining"));
        ui->poolMiningEndpoint->setEnabled(!st.running);
        ui->poolMiningAddress->setEnabled(!st.running);
        ui->poolMiningThreads->setEnabled(!st.running);
    }
    if (!st.running)
    {
        ui->poolMiningStatus->setText(tr("Pool mining stopped."));
        return;
    }
    if (!st.connected)
    {
        QString strErr = QString::fromStdString(st.last_error);
        ui->poolMiningStatus->setText(strErr.isEmpty()
            ? tr("Connecting…")
            : tr("Reconnecting… (%1)").arg(strErr));
        return;
    }
    if (!st.authorized)
    {
        ui->poolMiningStatus->setText(tr("Connected — authorizing…"));
        return;
    }

    ui->poolMiningStatus->setText(tr("Offering at %1 · share diff %2 · accepted %3 / rejected %4")
        .arg(FormatRate(st.hashrate))
        .arg(st.difficulty)
        .arg(st.accepted)
        .arg(st.rejected));
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
    if (gArgs.IsArgSet("-gen") || gArgs.IsArgSet("-stratum"))
        return;
    // Another wallet's page already started what is running.
    if (model->node().bobIsGenerating() || model->node().bobStratumStatus().running)
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
    if (!model) return;
    interfaces::Node& node = model->node();
    // Not IsInitialBlockDownload(): on this chain it stays true whenever the
    // newest block is more than a day old, and (BOB) can sit that long with
    // nobody mining. Caught up here means: a peer has been connected long enough
    // to tell us its headers, and we have every block up to the best header.
    if (node.getImporting() || node.getReindex())
        return;
    if (node.getNodeCount(CConnman::CONNECTIONS_ALL) == 0)
    {
        nPeersSince = 0;
        return;
    }
    if (nPeersSince == 0)
        nPeersSince = GetTime();
    if (GetTime() - nPeersSince < kAutoStartPeerSettleSecs)
        return;
    int header_height;
    int64_t header_time;
    if (!node.getHeaderTip(header_height, header_time) || node.getNumBlocks() < header_height)
        return;
    autoStartTimer->stop();
    ui->miningEnable->setChecked(true);   // runs on_miningEnable_toggled
}
