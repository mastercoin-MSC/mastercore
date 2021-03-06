// Copyright (c) 2011-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletview.h"

#include "addressbookpage.h"
#include "askpassphrasedialog.h"
#include "bitcoingui.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "overviewpage.h"
#include "receivecoinsdialog.h"
#include "sendcoinsdialog.h"
#include "sendmpdialog.h"
#include "lookupspdialog.h"
#include "lookuptxdialog.h"
#include "lookupaddressdialog.h"
//#include "metadexcanceldialog.h"
//#include "metadexdialog.h"
#include "signverifymessagedialog.h"
#include "transactiontablemodel.h"
#include "transactionview.h"
#include "balancesview.h"
#include "walletmodel.h"
//#include "orderhistorydialog.h"
#include "txhistorydialog.h"
#include "ui_interface.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

#include <QDebug>
#include <QTableView>
#include <QDialog>
#include <QHeaderView>

WalletView::WalletView(QWidget *parent):
    QStackedWidget(parent),
    clientModel(0),
    walletModel(0)
{
    // Create tabs
    overviewPage = new OverviewPage();
    transactionsPage = new QWidget(this);
    balancesPage = new QWidget(this);

    // balances page
    QVBoxLayout *bvbox = new QVBoxLayout();
    QHBoxLayout *bhbox_buttons = new QHBoxLayout();
    balancesView = new BalancesView(this);
    bvbox->addWidget(balancesView);
    QPushButton *bexportButton = new QPushButton(tr("&Export"), this);
    bexportButton->setToolTip(tr("Export the data in the current tab to a file"));
#ifndef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    bexportButton->setIcon(QIcon(":/icons/export"));
#endif
    bhbox_buttons->addStretch();
    bhbox_buttons->addWidget(bexportButton);
    bvbox->addLayout(bhbox_buttons);
    balancesPage->setLayout(bvbox);

    // transactions page
    // bitcoin transactions in second tab, MP transactions in first
    //bitcoin
    bitcoinTXTab = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
#ifndef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    exportButton->setIcon(QIcon(":/icons/export"));
#endif
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    bitcoinTXTab->setLayout(vbox);
    mpTXTab = new TXHistoryDialog;
    transactionsPage = new QWidget(this);
    QVBoxLayout *txvbox = new QVBoxLayout();
    txTabHolder = new QTabWidget();
    txTabHolder->addTab(mpTXTab,tr("Omni Layer"));
    txTabHolder->addTab(bitcoinTXTab,tr("Bitcoin"));
    txvbox->addWidget(txTabHolder);
    transactionsPage->setLayout(txvbox);
    // receive page
    receiveCoinsPage = new ReceiveCoinsDialog();

    // sending page
    sendCoinsPage = new QWidget(this);
    QVBoxLayout *svbox = new QVBoxLayout();
    sendCoinsTab = new SendCoinsDialog();
    sendMPTab = new SendMPDialog();
    QTabWidget *tabHolder = new QTabWidget();
    tabHolder->addTab(sendMPTab,tr("Omni Layer"));
    tabHolder->addTab(sendCoinsTab,tr("Bitcoin"));
    svbox->addWidget(tabHolder);
    sendCoinsPage->setLayout(svbox);

    // exchange page
    /* no MetaDEx in this ver
    exchangePage = new QWidget(this);
    QVBoxLayout *exvbox = new QVBoxLayout();
    metaDExTab = new MetaDExDialog();
    cancelTab = new MetaDExCancelDialog();
    QTabWidget *exTabHolder = new QTabWidget();
    orderHistoryTab = new OrderHistoryDialog;
    //exTabHolder->addTab(new QWidget(),tr("Trade Bitcoin/Mastercoin")); not yet implemented
    exTabHolder->addTab(metaDExTab,tr("Trade Mastercoin/Smart Properties"));
    exTabHolder->addTab(orderHistoryTab,tr("Order History"));
    exTabHolder->addTab(cancelTab,tr("Cancel Orders"));
    exvbox->addWidget(exTabHolder);
    exchangePage->setLayout(exvbox);
    */

    // smart property page
    /*smartPropertyPage = new QWidget(this);
    QVBoxLayout *spvbox = new QVBoxLayout();
    QTabWidget *spTabHolder = new QTabWidget();
    spTabHolder->addTab(new QWidget(),tr("Crowdsale Participation"));
    spTabHolder->addTab(new QWidget(),tr("Property Issuance"));
    spTabHolder->addTab(new QWidget(),tr("Revoke or Grant Tokens"));
    spvbox->addWidget(spTabHolder);
    smartPropertyPage->setLayout(spvbox);
    */

    // toolbox page
    toolboxPage = new QWidget(this);
    QVBoxLayout *tvbox = new QVBoxLayout();
    addressLookupTab = new LookupAddressDialog();
    spLookupTab = new LookupSPDialog();
    txLookupTab = new LookupTXDialog();
    QTabWidget *tTabHolder = new QTabWidget();
    tTabHolder->addTab(addressLookupTab,tr("Lookup Address"));
    tTabHolder->addTab(spLookupTab,tr("Lookup Property"));
    tTabHolder->addTab(txLookupTab,tr("Lookup Transaction"));

    tvbox->addWidget(tTabHolder);
    toolboxPage->setLayout(tvbox);

    // add pages
    addWidget(overviewPage);
    addWidget(balancesPage);
    addWidget(transactionsPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);
    //addWidget(exchangePage);
    //addWidget(smartPropertyPage);
    addWidget(toolboxPage);

    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, SIGNAL(clicked()), transactionView, SLOT(exportClicked()));

    // Pass through messages from sendCoinsPage
    connect(sendCoinsPage, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
    // Pass through messages from transactionView
    connect(transactionView, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));
}

WalletView::~WalletView()
{
}

void WalletView::setBitcoinGUI(BitcoinGUI *gui)
{
    if (gui)
    {
        // Clicking on a transaction on the overview page simply sends you to bitcoin history tab
        connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), gui, SLOT(gotoBitcoinHistoryTab()));

        // Receive and report messages
        connect(this, SIGNAL(message(QString,QString,unsigned int)), gui, SLOT(message(QString,QString,unsigned int)));

        // Pass through encryption status changed signals
        connect(this, SIGNAL(encryptionStatusChanged(int)), gui, SLOT(setEncryptionStatus(int)));

        // Pass through transaction notifications
        connect(this, SIGNAL(incomingTransaction(QString,int,qint64,QString,QString)), gui, SLOT(incomingTransaction(QString,int,qint64,QString,QString)));
    }
}

void WalletView::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;

    overviewPage->setClientModel(clientModel);
    balancesView->setClientModel(clientModel);
    sendMPTab->setClientModel(clientModel);
    mpTXTab->setClientModel(clientModel);
}

void WalletView::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;

    // Put transaction list in tabs
    transactionView->setModel(walletModel);
    overviewPage->setWalletModel(walletModel);
    receiveCoinsPage->setModel(walletModel);
    sendCoinsTab->setModel(walletModel);
    sendMPTab->setWalletModel(walletModel);
    balancesView->setWalletModel(walletModel);
//    metaDExTab->setModel(walletModel);
    mpTXTab->setWalletModel(walletModel);
//    cancelTab->setModel(walletModel);
//    orderHistoryTab->setModel(walletModel);

    if (walletModel)
    {
        // Receive and pass through messages from wallet model
        connect(walletModel, SIGNAL(message(QString,QString,unsigned int)), this, SIGNAL(message(QString,QString,unsigned int)));

        // Handle changes in encryption status
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SIGNAL(encryptionStatusChanged(int)));
        updateEncryptionStatus();

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(processNewTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));

        // Show progress dialog
        connect(walletModel, SIGNAL(showProgress(QString,int)), this, SLOT(showProgress(QString,int)));
    }
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    // Prevent balloon-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();

    QString date = ttm->index(start, TransactionTableModel::Date, parent).data().toString();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
    QString type = ttm->index(start, TransactionTableModel::Type, parent).data().toString();
    QString address = ttm->index(start, TransactionTableModel::ToAddress, parent).data().toString();

    emit incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address);
}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewPage);
}

void WalletView::gotoBalancesPage()
{
    setCurrentWidget(balancesPage);
}

void WalletView::gotoHistoryPage()
{
    setCurrentWidget(transactionsPage);
}

void WalletView::gotoBitcoinHistoryTab()
{
    setCurrentWidget(transactionsPage);
    txTabHolder->setCurrentIndex(1);
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveCoinsPage);
}

void WalletView::gotoExchangePage()
{
    setCurrentWidget(exchangePage);
}

void WalletView::gotoToolboxPage()
{
    setCurrentWidget(toolboxPage);
}

void WalletView::gotoSmartPropertyPage()
{
    setCurrentWidget(smartPropertyPage);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendCoinsPage);

    if (!addr.isEmpty())
        sendCoinsTab->setAddress(addr);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    return sendCoinsTab->handlePaymentRequest(recipient);
}

void WalletView::showOutOfSyncWarning(bool fShow)
{
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::updateEncryptionStatus()
{
    emit encryptionStatusChanged(walletModel->getEncryptionStatus());
}

void WalletView::encryptWallet(bool status)
{
    if(!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Encrypt : AskPassphraseDialog::Decrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    updateEncryptionStatus();
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        tr("Wallet Data (*.dat)"), NULL);

    if (filename.isEmpty())
        return;

    if (!walletModel->backupWallet(filename)) {
        emit message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
        }
    else {
        emit message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void WalletView::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void WalletView::usedSendingAddresses()
{
    if(!walletModel)
        return;
    AddressBookPage *dlg = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModel(walletModel->getAddressTableModel());
    dlg->show();
}

void WalletView::usedReceivingAddresses()
{
    if(!walletModel)
        return;
    AddressBookPage *dlg = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModel(walletModel->getAddressTableModel());
    dlg->show();
}

void WalletView::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}
