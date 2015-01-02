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

//
// There are conflicts, because of the "watch-only" feature,
// overviewpage_009.ui.bak can be found in /forms.
//
// TODO: adjust UI elements, enable section
// TODO: probably all includes can be removed except "mastercore.h" and <sstream>
// 
#include "mastercore.h" // file_log, set_wallet_totals, ...

#include <stdint.h>     // uintN_t, intN_t
#include <stdio.h>      // printf
#include <sstream>      // std::ostringstream

#include <boost/algorithm/string.hpp> // boost::split, boost::is_any_of, boost::token_compress_on

#include <QAbstractItemDelegate>
#include <QPainter>

#define DECORATION_SIZE 64
#ifdef USE_MASTERCORE_UI_009 // TODO: adjust UI, then remove this check
#define NUM_ITEMS 5
#else
#define NUM_ITEMS 3
#endif

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
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

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
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
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
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
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

#ifdef USE_MASTERCORE_UI_009 // TODO: adjust UI, then remove this check
    ui->proclabel->setText("(" + tr("processing") + ")");   // msc processing label
    ui->proclabel_2->setText("(" + tr("processing") + ")"); // smart property processing label
#endif

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

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::formatWithUnit(unit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + unconfirmedBalance + immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::formatWithUnit(unit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance
    
#ifdef USE_MASTERCORE_UI_009 // TODO: adjust UI, then remove this check

    /**
     * Note to keep track of relations:
     * 
     * requires <sstream>
     * 
     *   std::ostringstream
     * 
     * 
     * requires mastercore.h
     * 
     *   set_wallet_totals()
     * 
     *   global_balance_money_maineco
     *   global_balance_reserved_maineco
     *   global_balance_money_testeco
     *   global_balance_reserved_testeco
     * 
     *   mastercore::isPropertyDivisible
     *   mastercore::getPropertyName
     */

    // mastercoin balances - force refresh first
    set_wallet_totals();
    ui->MSClabelavailable->setText(BitcoinUnits::format(0, global_balance_money_maineco[1]).append(" MSC"));
    ui->MSClabelpending->setText("0.00 MSC"); // no unconfirmed support currently
    ui->MSClabelreserved->setText(BitcoinUnits::format(0, global_balance_reserved_maineco[1]).append(" MSC"));
    uint64_t totalbal = global_balance_money_maineco[1] + global_balance_reserved_maineco[1];
    ui->MSClabeltotal->setText(BitcoinUnits::format(0, totalbal).append(" MSC"));

    // scrappy way to do this, find a more efficient way of interacting with labels
    // show first 5 SPs with balances - needs to be converted to listwidget or something
    unsigned int propertyId;
    unsigned int lastFoundPropertyId = 1;
    std::string spName[7];
    uint64_t spBal[7];
    bool spDivisible[7];
    bool spFound[7];
    unsigned int spItem;
    bool foundProperty = false;

    // TODO: less magic numbers
    // TODO: use map instead of array for total balances

    for (spItem = 1; spItem < 7; spItem++)
    {
        spFound[spItem] = false;
        for (propertyId = lastFoundPropertyId + 1; propertyId < 100000; propertyId++)
        {
            foundProperty = false;
            if ((global_balance_money_maineco[propertyId] > 0) || (global_balance_reserved_maineco[propertyId] > 0))
            {
                lastFoundPropertyId = propertyId;
                foundProperty = true;
                spName[spItem] = mastercore::getPropertyName(propertyId);
                if (spName[spItem].size()> 22) spName[spItem] = spName[spItem].substr(0, 22) + "...";
                spName[spItem] += " (#" + static_cast<std::ostringstream*>(&(std::ostringstream() << propertyId))->str() + ")";
                spBal[spItem] = global_balance_money_maineco[propertyId];
                spDivisible[spItem] = mastercore::isPropertyDivisible(propertyId);
                spFound[spItem] = true;
                break;
            }
        }
        // have we found a property in main eco?  If not let's try test eco
        if (!foundProperty)
        {
            for (propertyId = lastFoundPropertyId + 1; propertyId < 100000; propertyId++)
            {
                if ((global_balance_money_testeco[propertyId] > 0) || (global_balance_reserved_testeco[propertyId] > 0))
                {
                    lastFoundPropertyId = propertyId;
                    foundProperty = true;
                    spName[spItem] = mastercore::getPropertyName(propertyId+2147483647);
                    if (spName[spItem].size() > 22) spName[spItem] = spName[spItem].substr(0, 22) + "...";
                    spName[spItem] += " (#" + static_cast<std::ostringstream*>(&(std::ostringstream() << propertyId+2147483647))->str() + ")";
                    spBal[spItem] = global_balance_money_testeco[propertyId];
                    spDivisible[spItem] = mastercore::isPropertyDivisible(propertyId+2147483647);
                    spFound[spItem] = true;
                    break;
                }
            }
        }
    }

    // TODO: use loop, stop at max height or when there are no more properties

    // set smart property info
    if (spFound[1])
    {
        // only need custom tokenLabel for SP1 since TMSC will always be first
        std::string tokenLabel;
        if (spName[1] == "Test MasterCoin (#2)") // TODO: no magic name
            tokenLabel = " TMSC";
        else
            tokenLabel = " SPT";

        ui->SPname1->setText(spName[1].c_str());
        if (spDivisible[1]) {
            ui->SPbal1->setText(BitcoinUnits::format(0, spBal[1]).append(QString::fromStdString(tokenLabel)));
        } else {
            std::string balText = std::static_cast<ostringstream*>(&(std::ostringstream() << spBal[1]))->str();
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
        if (spDivisible[2]) {
            ui->SPbal2->setText(BitcoinUnits::format(0, spBal[2]).append(" SPT"));
        } else {
            std::string balText = static_cast<std::ostringstream*>(&(std::ostringstream() << spBal[2]))->str();
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
        if (spDivisible[3]) {
            ui->SPbal3->setText(BitcoinUnits::format(0, spBal[3]).append(" SPT"));
        } else {
            std::string balText = static_cast<std::ostringstream*>(&(std::ostringstream() << spBal[3]))->str();
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
        } else {
            std::string balText = static_cast<std::ostringstream*>(&(std::ostringstream() << spBal[4]))->str();
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
        } else {
            std::string balText = static_cast<std::ostringstream*>(&(std::ostringstream() << spBal[5]))->str();
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
    if (spFound[6]) {
        ui->notifyMoreSPLabel->setVisible(true);
    } else {
        ui->notifyMoreSPLabel->setVisible(false);
    }
#endif // USE_MASTERCORE_UI_009
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly)
        ui->labelWatchImmature->hide();
}

void OverviewPage::switchToBalancesPage()
{
    // TODO: ?
    printf("switch to balances clicked\n");
    // WalletView::gotoBalancesPage();
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
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
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    // TODO: move alert processing, use original body:
    // this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    // this->ui->labelAlerts->setText(warnings);

    std::string alertMessage = mastercore::getMasterCoreAlertString();
    // Bitcoin or Master Protocol alerts to display?
    bool showAlert = (!alertMessage.empty() || !warnings.isEmpty());
    this->ui->labelAlerts->setVisible(showAlert);
    QString totalMessage;
    std::vector<std::string> vstr;

    // check if we have a Bitcoin alert to display
    if (!warnings.isEmpty()) {
        totalMessage = warnings + "\n";
    }

    // check if we have a Master Protocol alert to display
    if (!alertMessage.empty())
    {
        boost::split(vstr, alertMessage, boost::is_any_of(":"), boost::token_compress_on);
        // make sure there are 5 tokens
        if (5 == vstr.size()) {
            totalMessage += QString::fromStdString(vstr[4]);
        } else {
            file_log("DEBUG ALERT ERROR - Something went wrong decoding the global alert string.\n");
        }
    }

    // display the alert if needed
    if (showAlert) {
        this->ui->labelAlerts->setText(totalMessage);
    }
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);

#ifdef USE_MASTERCORE_UI_009 // TODO: adjust UI, then remove this check
    ui->proclabel->setVisible(fShow);
    ui->proclabel_2->setVisible(fShow);
#endif // USE_MASTERCORE_UI_009
}
