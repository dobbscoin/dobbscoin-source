// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2014-2026 The Dobbscoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_MININGPAGE_H
#define BITCOIN_QT_MININGPAGE_H

#include <QWidget>

#include <cstdint>

QT_BEGIN_NAMESPACE
class QHideEvent;
class QShowEvent;
class QTimer;
QT_END_NAMESPACE

class WalletModel;

namespace Ui {
    class MiningPage;
}

/** (BOB) Main-window Mining page, from 0.14: solo CPU generation plus the in-wallet
    stratum (pool) client, between Receive and Transactions. The miner and the pool
    client are one per node; with several wallets open every Mining page shows the
    same state, and solo mining pays into the wallet whose page started it. */
class MiningPage : public QWidget
{
    Q_OBJECT

public:
    explicit MiningPage(QWidget *parent = nullptr);
    ~MiningPage();

    void setModel(WalletModel *model);

protected:
    /** The page only polls while it is the visible page of the wallet stack. */
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private Q_SLOTS:
    /** Solo mode */
    void on_miningEnable_toggled(bool checked);
    void on_miningThreads_valueChanged(int value);
    /** Polled from a QTimer while the page is visible; refreshes the live status line. */
    void updateMiningStatus();

    /** Pool mode (in-wallet stratum client) */
    void on_poolMiningToggle_clicked(bool checked);
    void updatePoolMiningStatus();

    /** Start mining when the wallet opens (#47) */
    void on_miningAutoStart_currentIndexChanged(int index);
    void autoStartSoloWhenCaughtUp();

private:
    /** Start or stop the solo miner, paying a new address of this page's wallet per block. */
    bool setSoloMining(bool on, int threads);
    /** Pool is only offered once a pay-to address has been saved. */
    void updateAutoStartChoices();
    /** Runs once, from setModel: pool needs the model to check the address. */
    void autoStart();

    Ui::MiningPage *ui;
    WalletModel *model{nullptr};
    QTimer *pollTimer{nullptr};
    QTimer *autoStartTimer{nullptr};
    bool fAutoStartDone{false};
    bool fStartedSolo{false};   //!< this page started the solo miner (stopped with the page)
    uint64_t nStartCount{0};    //!< bobminer start count at that start
    int64_t nPeersSince{0};
};

#endif // BITCOIN_QT_MININGPAGE_H
