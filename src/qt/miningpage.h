// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2013-2014 The Offerings developers
// Copyright (c) 2026 The Offerings Conclave / SubGenius.Finance community
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DOBBSCOIN_QT_MININGPAGE_H
#define DOBBSCOIN_QT_MININGPAGE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QHideEvent;
class QShowEvent;
class QTimer;
QT_END_NAMESPACE

class WalletModel;

namespace Ui {
    class MiningPage;
}

/** Main-window Mining page: solo CPU generation plus the in-wallet stratum
    (pool) client. This used to be a tab in the debug window; it lives out
    front now, between Receive and Transactions. */
class MiningPage : public QWidget
{
    Q_OBJECT

public:
    explicit MiningPage(QWidget *parent = 0);
    ~MiningPage();

    void setModel(WalletModel *model);

protected:
    /** The page only polls while it is the visible page of the wallet stack. */
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    /** Solo mode */
    void on_miningEnable_toggled(bool checked);
    void on_miningThreads_valueChanged(int value);
    /** Polled from a QTimer while the page is visible; refreshes the live status line. */
    void updateMiningStatus();
    /** Pool mode (in-wallet stratum client) */
    void on_poolMiningToggle_clicked(bool checked);
    void updatePoolMiningStatus();

private:
    Ui::MiningPage *ui;
    WalletModel *model;
    QTimer *pollTimer;
};

#endif // DOBBSCOIN_QT_MININGPAGE_H
