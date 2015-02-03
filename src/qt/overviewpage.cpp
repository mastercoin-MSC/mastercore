// Copyright (c) 2011-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"
#include "wallet.h"

// potentially overzealous includes here
#include "base58.h"
#include "rpcserver.h"
#include "init.h"
#include "util.h"
#include <fstream>
#include <algorithm>
#include <vector>
#include <utility>
#include <string>
#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"
#include "leveldb/db.h"
#include "leveldb/write_batch.h"
// end potentially overzealous includes
using namespace json_spirit; // since now using Array in mastercore.h this needs to come first

#include "mastercore.h"
using namespace mastercore;

// potentially overzealous using here
using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace leveldb;
// end potentially overzealous using

#include "mastercore_dex.h"
#include "mastercore_tx.h"
#include "mastercore_sp.h"

#include <QAbstractItemDelegate>
#include <QPainter>

#define DECORATION_SIZE 64
#define NUM_ITEMS 5

//extern uint64_t global_MSC_total;
//extern uint64_t global_MSC_RESERVED_total;
extern std::map<uint32_t, uint64_t> global_balance_money_maineco;
extern std::map<uint32_t, uint64_t> global_balance_reserved_maineco;
extern std::map<uint32_t, uint64_t> global_balance_money_testeco;
extern std::map<uint32_t, uint64_t> global_balance_reserved_testeco;

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(): QAbstractItemDelegate(), unit(BitcoinUnits::BTC)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address);

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;

};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    txdelegate(new TxViewDelegate()),
    filter(0)
{
    ui->setupUi(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");
    ui->proclabel->setText("(" + tr("processing") + ")"); //msc processing label
    ui->proclabel_2->setText("(" + tr("processing") + ")"); //smart property processing label
    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(qint64 balance, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    // Mastercore alerts come as block transactions and do not trip bitcoin alertsChanged() signal so let's check the
    // alert status with the update balance signal that comes in after each block to see if it had any alerts in it
    updateAlerts();

    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + unconfirmedBalance + immatureBalance));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    ui->labelImmature->setVisible(showImmature);
    ui->labelImmatureText->setVisible(showImmature);

    // mastercoin balances - force refresh first
    set_wallet_totals();
    ui->MSClabelavailable->setText(BitcoinUnits::format(0, global_balance_money_maineco[1]).append(" MSC"));
    ui->MSClabelpending->setText("0.00 MSC"); // no unconfirmed support currently
    ui->MSClabelreserved->setText(BitcoinUnits::format(0, global_balance_reserved_maineco[1]).append(" MSC"));
    uint64_t totalbal = global_balance_money_maineco[1] + global_balance_reserved_maineco[1];
    ui->MSClabeltotal->setText(BitcoinUnits::format(0, totalbal).append(" MSC"));

    //scrappy way to do this, find a more efficient way of interacting with labels
    //show first 5 SPs with balances - needs to be converted to listwidget or something
    unsigned int propertyId;
    unsigned int lastFoundPropertyId = 1;
    string spName[7];
    uint64_t spBal[7];
    bool spDivisible[7];
    bool spFound[7];
    unsigned int spItem;
    bool foundProperty = false;

    for (spItem = 1; spItem < 7; spItem++)
    {
        spFound[spItem] = false;
        for (propertyId = lastFoundPropertyId+1; propertyId<100000; propertyId++)
        {
            foundProperty=false;
            if ((global_balance_money_maineco[propertyId] > 0) || (global_balance_reserved_maineco[propertyId] > 0))
            {
                lastFoundPropertyId = propertyId;
                foundProperty=true;
                spName[spItem] = getPropertyName(propertyId).c_str();
                if(spName[spItem].size()>22) spName[spItem]=spName[spItem].substr(0,22)+"...";
                spName[spItem] += " (#" + static_cast<ostringstream*>( &(ostringstream() << propertyId) )->str() + ")";
                spBal[spItem] = global_balance_money_maineco[propertyId];
                spDivisible[spItem] = isPropertyDivisible(propertyId);
                spFound[spItem] = true;
                break;
            }
        }
        // have we found a property in main eco?  If not let's try test eco
        if (!foundProperty)
        {
            for (propertyId = lastFoundPropertyId+1; propertyId<100000; propertyId++)
            {
                if ((global_balance_money_testeco[propertyId] > 0) || (global_balance_reserved_testeco[propertyId] > 0))
                {
                    lastFoundPropertyId = propertyId;
                    foundProperty=true;
                    spName[spItem] = getPropertyName(propertyId+2147483647).c_str();
                    if(spName[spItem].size()>22) spName[spItem]=spName[spItem].substr(0,22)+"...";
                    spName[spItem] += " (#" + static_cast<ostringstream*>( &(ostringstream() << propertyId+2147483647) )->str() + ")";
                    spBal[spItem] = global_balance_money_testeco[propertyId];
                    spDivisible[spItem] = isPropertyDivisible(propertyId+2147483647);
                    spFound[spItem] = true;
                    break;
                }
            }
        }
    }

    //set smart property info
    if (spFound[1])
    {
        // only need custom tokenLabel for SP1 since TMSC will always be first
        string tokenLabel;
        if (spName[1]=="Test MasterCoin (#2)") { tokenLabel = " TMSC"; } else { tokenLabel = " SPT"; }

        ui->SPname1->setText(spName[1].c_str());
        if (spDivisible[1])
        {
            ui->SPbal1->setText(BitcoinUnits::format(0, spBal[1]).append(QString::fromStdString(tokenLabel)));
        }
        else
        {
            string balText = static_cast<ostringstream*>( &(ostringstream() << spBal[1]) )->str();
            balText += tokenLabel;
            ui->SPbal1->setText(balText.c_str());
        }
    }
    else
    {
        ui->SPname1->setText("N/A");
        ui->SPbal1->setText("N/A");
        ui->SPname1->setVisible(false);
        ui->SPbal1->setVisible(false);
    }
    if (spFound[2])
    {
        ui->SPname2->setText(spName[2].c_str());
        if (spDivisible[2])
        {
            ui->SPbal2->setText(BitcoinUnits::format(0, spBal[2]).append(" SPT"));
        }
        else
        {
            string balText = static_cast<ostringstream*>( &(ostringstream() << spBal[2]) )->str();
            balText += " SPT";
            ui->SPbal2->setText(balText.c_str());
        }
    }
    else
    {
        ui->SPname2->setText("N/A");
        ui->SPbal2->setText("N/A");
        ui->SPname2->setVisible(false);
        ui->SPbal2->setVisible(false);
    }
    if (spFound[3])
    {
        ui->SPname3->setText(spName[3].c_str());
        if (spDivisible[3])
        {
            ui->SPbal3->setText(BitcoinUnits::format(0, spBal[3]).append(" SPT"));
        }
        else
        {
            string balText = static_cast<ostringstream*>( &(ostringstream() << spBal[3]) )->str();
            balText += " SPT";
            ui->SPbal3->setText(balText.c_str());
        }
    }
    else
    {
        ui->SPname3->setText("N/A");
        ui->SPbal3->setText("N/A");
        ui->SPname3->setVisible(false);
        ui->SPbal3->setVisible(false);
    }
    if (spFound[4])
    {
        ui->SPname4->setText(spName[4].c_str());
        if (spDivisible[4])
        {
            ui->SPbal4->setText(BitcoinUnits::format(0, spBal[4]).append(" SPT"));
        }
        else
        {
            string balText = static_cast<ostringstream*>( &(ostringstream() << spBal[4]) )->str();
            balText += " SPT";
            ui->SPbal4->setText(balText.c_str());
        }
    }
    else
    {
        ui->SPname4->setText("N/A");
        ui->SPbal4->setText("N/A");
        ui->SPname4->setVisible(false);
        ui->SPbal4->setVisible(false);
    }
    if (spFound[5])
    {
        ui->SPname5->setText(spName[5].c_str());
        if (spDivisible[5])
        {
            ui->SPbal5->setText(BitcoinUnits::format(0, spBal[5]).append(" SPT"));
        }
        else
        {
            string balText = static_cast<ostringstream*>( &(ostringstream() << spBal[5]) )->str();
            balText += " SPT";
            ui->SPbal5->setText(balText.c_str());
        }
    }
    else
    {
        ui->SPname5->setText("N/A");
        ui->SPbal5->setText("N/A");
        ui->SPname5->setVisible(false);
        ui->SPbal5->setVisible(false);
    }
    if (spFound[6]) { ui->notifyMoreSPLabel->setVisible(true); } else { ui->notifyMoreSPLabel->setVisible(false); }
}

void OverviewPage::switchToBalancesPage()
{
printf("switch to balances clicked\n");
//    WalletView::gotoBalancesPage();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts()));
        updateAlerts();
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Status, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance());
        connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64)), this, SLOT(setBalance(qint64, qint64, qint64)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts()
{
    // init variables
    bool showAlert = false;
    QString totalMessage;
    // override to check alert directly rather than passing in param as we won't always be calling from bitcoin in
    // the clientmodel emit for alertsChanged
    QString warnings = QString::fromStdString(GetWarnings("statusbar")); // get current bitcoin alert/warning directly
    QString alertMessage = QString::fromStdString(getMasterCoreAlertTextOnly()); // just return the text message from alert
    // any BitcoinCore or MasterCore alerts to display?
    if((!alertMessage.isEmpty()) || (!warnings.isEmpty())) showAlert = true;
    this->ui->labelAlerts->setVisible(showAlert);
    // check if we have a Bitcoin alert to display
    if(!warnings.isEmpty()) totalMessage = warnings + "\n";
    // check if we have a MasterProtocol alert to display
    if(!alertMessage.isEmpty()) totalMessage += alertMessage;
    // display the alert if needed
    if(showAlert) { this->ui->labelAlerts->setText(totalMessage); }
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
    ui->proclabel->setVisible(fShow);
    ui->proclabel_2->setVisible(fShow);
}
