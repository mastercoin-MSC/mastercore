// Copyright (c) 2011-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "balancesview.h"

#include "mastercore.h"

#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QScrollBar>
#include <QSignalMapper>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

#include <string>
#include <sstream>

using std::string;
using std::ostringstream;

using namespace mastercore;


BalancesView::BalancesView(QWidget *parent) :
    QWidget(parent), model(0), balancesView(0)
{
    // Build filter row
    setContentsMargins(0,0,0,0);

    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->setContentsMargins(0,0,0,0);
#ifdef Q_OS_MAC
    hlayout->setSpacing(5);
    hlayout->addSpacing(26);
#else
    hlayout->setSpacing(0);
    hlayout->addSpacing(23);
#endif
    hlayout->addStretch();
    // property ID selector
    propSelLabel = new QLabel("Show Balances For: ");
    hlayout->addWidget(propSelLabel);

    propSelectorWidget = new QComboBox(this);
#ifdef Q_OS_MAC
    propSelectorWidget->setFixedWidth(301);
#else
    propSelectorWidget->setFixedWidth(300);
#endif
    //add the selector to the layout
    hlayout->addWidget(propSelectorWidget);

    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0,0,0,0);
    vlayout->setSpacing(0);

    //populate
    //prep matrix
    const int numRows = 3000;
    const int numColumns = 3;
    uint matrix[numRows][numColumns];
    MatrixModel *mmp = NULL;
    QTableView *view = NULL;
    //create matrix
    for (int i = 0; i < numRows; ++i)
         for (int j = 0; j < numColumns; ++j)
              matrix[i][j] = (i+1) * (j+1);
    //create a model which adapts the data (the matrix) to the view.
    mmp = new MatrixModel(numRows, numColumns, (uint*)matrix, 2147483646);  //id for all (summary)
    view = new QTableView(this);
    view->setModel(mmp);
    //adjust sizing
    view->horizontalHeader()->resizeSection(0, 160);
    #if QT_VERSION < 0x050000
       view->horizontalHeader()->setResizeMode(1, QHeaderView::Stretch);
    #else
       view->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    #endif
    view->horizontalHeader()->resizeSection(2, 140);
    view->horizontalHeader()->resizeSection(3, 140);
    view->verticalHeader()->setVisible(false);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    vlayout->addLayout(hlayout);
    vlayout->addWidget(view);
    vlayout->setSpacing(0);
    int width = view->verticalScrollBar()->sizeHint().width();
    // Cover scroll bar width with spacing
#ifdef Q_OS_MAC
    hlayout->addSpacing(width+2);
#else
    hlayout->addSpacing(width);
#endif
    // Always show scroll bar
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setTabKeyNavigation(false);
    view->setContextMenuPolicy(Qt::CustomContextMenu);

    balancesView = view;

    // Actions
    QAction *balancesCopyAddressAction = new QAction(tr("Copy address"), this);
    QAction *balancesCopyLabelAction = new QAction(tr("Copy label"), this);
    QAction *balancesCopyAmountAction = new QAction(tr("Copy amount"), this);

    contextMenu = new QMenu();
    contextMenu->addAction(balancesCopyAddressAction);
    contextMenu->addAction(balancesCopyLabelAction);
    contextMenu->addAction(balancesCopyAmountAction);

    mapperThirdPartyTxUrls = new QSignalMapper(this);

    // Connect actions
    connect(propSelectorWidget, SIGNAL(activated(int)), this, SLOT(propSelectorChanged(int)));

    connect(balancesCopyAddressAction, SIGNAL(triggered()), this, SLOT(balancesCopyAddress()));
    connect(balancesCopyLabelAction, SIGNAL(triggered()), this, SLOT(balancesCopyLabel()));
    connect(balancesCopyAmountAction, SIGNAL(triggered()), this, SLOT(balancesCopyAmount()));
    UpdatePropSelector();
}

void BalancesView::UpdatePropSelector()
{
    //printf("balancesview::propselectorupdatecalled\n");
    QString spId = propSelectorWidget->itemData(propSelectorWidget->currentIndex()).toString();
    propSelectorWidget->clear();
    propSelectorWidget->addItem("Wallet Totals (Summary)","2147483646"); //use last possible ID for summary for now
    // trigger update of global balances
    set_wallet_totals();
    // populate property selector
    for (unsigned int propertyId = 1; propertyId<100000; propertyId++)
    {
        if ((global_balance_money_maineco[propertyId] > 0) || (global_balance_reserved_maineco[propertyId] > 0))
        {
            string spName;
            spName = getPropertyName(propertyId).c_str();
            if(spName.size()>20) spName=spName.substr(0,20)+"...";
            string spId = static_cast<ostringstream*>( &(ostringstream() << propertyId) )->str();
            spName += " (#" + spId + ")";
            propSelectorWidget->addItem(spName.c_str(),spId.c_str());
        }
    }
    for (unsigned int propertyId = 1; propertyId<100000; propertyId++)
    {
        if ((global_balance_money_testeco[propertyId] > 0) || (global_balance_reserved_testeco[propertyId] > 0))
        {
            string spName;
            spName = getPropertyName(propertyId+2147483647).c_str();
            if(spName.size()>20) spName=spName.substr(0,20)+"...";
            string spId = static_cast<ostringstream*>( &(ostringstream() << propertyId+2147483647) )->str();
            spName += " (#" + spId + ")";
            propSelectorWidget->addItem(spName.c_str(),spId.c_str());
        }
    }
    int propIdx = propSelectorWidget->findData(spId);
    if (propIdx != -1) { propSelectorWidget->setCurrentIndex(propIdx); }
}

void BalancesView::setModel(WalletModel *model)
{
    this->model = model;
    connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64)), this, SLOT(balancesUpdated()));
}

void BalancesView::UpdateBalances()
{
    //printf("balancesview::updatebalances()\n");
    propSelectorChanged(0);
    UpdatePropSelector();
}

void BalancesView::propSelectorChanged(int idx)
{
    //printf("balancesview::propselectorchanged()\n");
    QString spId = propSelectorWidget->itemData(propSelectorWidget->currentIndex()).toString();
    unsigned int propertyId = spId.toUInt();
    //repopulate with new selected balances
    //prep matrix
    const int numRows = 3000;
    const int numColumns = 3;
    uint matrix[numRows][numColumns];
    MatrixModel *mmp = NULL;
    //create matrix
    for (int i = 0; i < numRows; ++i)
         for (int j = 0; j < numColumns; ++j)
              matrix[i][j] = (i+1) * (j+1);
    //create a model which adapts the data (the matrix) to the view.
    mmp = new MatrixModel(numRows, numColumns, (uint*)matrix, propertyId);
    balancesView->setModel(mmp);
}

void BalancesView::contextualMenu(const QPoint &point)
{
    QModelIndex index = balancesView->indexAt(point);
    if(index.isValid())
    {
//        contextMenu->exec(QCursor::pos());
    }
}

void BalancesView::balancesCopyAddress()
{
//    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::AddressRole);
}

void BalancesView::balancesCopyLabel()
{
//    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::LabelRole);
}

void BalancesView::balancesCopyAmount()
{
//    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::FormattedAmountRole);
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void BalancesView::resizeEvent(QResizeEvent* event)
{
//    QWidget::resizeEvent(event);
//    columnResizingFixer->stretchColumnWidth(TransactionTableModel::ToAddress);
}

void BalancesView::balancesUpdated()
{
    UpdateBalances();
}

