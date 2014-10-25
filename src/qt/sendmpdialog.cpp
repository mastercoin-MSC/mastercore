// Copyright (c) 2011-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sendmpdialog.h"
#include "ui_sendmpdialog.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "walletmodel.h"
#include "wallet.h"
#include "base58.h"
#include "coincontrol.h"
#include "ui_interface.h"

#include <boost/filesystem.hpp>

#include "leveldb/db.h"
#include "leveldb/write_batch.h"

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

#include "mastercore.h"
using namespace mastercore;

// potentially overzealous using here
using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace leveldb;
// end potentially overzealous using

#include "mastercore_dex.h"
#include "mastercore_parse_string.h"
#include "mastercore_tx.h"
#include "mastercore_sp.h"

#include <QDateTime>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>

SendMPDialog::SendMPDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SendMPDialog),
    model(0)
{
    ui->setupUi(this);
    this->model = model;

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->clearButton->setIcon(QIcon());
    ui->sendButton->setIcon(QIcon());
#endif

    // populate placeholder text
    ui->sendToLineEdit->setPlaceholderText("Enter a Master Protocol address (e.g. 1MaSTeRPRotocolADDreSShef77z6A5S4P)");
    ui->amountLineEdit->setPlaceholderText("Enter Amount");

    // populate property selector
    for (unsigned int propertyId = 1; propertyId<100000; propertyId++)
    {
        if ((global_balance_money_maineco[propertyId] > 0) || (global_balance_reserved_maineco[propertyId] > 0))
        {
            string spName;
            spName = getPropertyName(propertyId).c_str();
            if(spName.size()>20) spName=spName.substr(0,23)+"...";
            string spId = static_cast<ostringstream*>( &(ostringstream() << propertyId) )->str();
            spName += " (#" + spId + ")";
            if (isPropertyDivisible(propertyId)) { spName += " [D]"; } else { spName += " [I]"; }
            ui->propertyComboBox->addItem(spName.c_str(),spId.c_str());
        }
    }
    for (unsigned int propertyId = 1; propertyId<100000; propertyId++)
    {
        if ((global_balance_money_testeco[propertyId] > 0) || (global_balance_reserved_testeco[propertyId] > 0))
        {
            string spName;
            spName = getPropertyName(propertyId+2147483647).c_str();
            if(spName.size()>20) spName=spName.substr(0,23)+"...";
            string spId = static_cast<ostringstream*>( &(ostringstream() << propertyId+2147483647) )->str();
            spName += " (#" + spId + ")";
            if (isPropertyDivisible(propertyId+2147483647)) { spName += " [D]"; } else { spName += " [I]"; }
            ui->propertyComboBox->addItem(spName.c_str(),spId.c_str());
        }
    }

    // connect actions
    connect(ui->propertyComboBox, SIGNAL(activated(int)), this, SLOT(propertyComboBoxChanged(int)));
    connect(ui->sendFromComboBox, SIGNAL(activated(int)), this, SLOT(sendFromComboBoxChanged(int)));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clearButtonClicked()));
    connect(ui->sendButton, SIGNAL(clicked()), this, SLOT(sendButtonClicked()));
    connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64)), this, SLOT(balancesUpdated()));

    // initial update
    updateProperty();
    updateFrom();
}

void SendMPDialog::setModel(WalletModel *model)
{
    this->model = model;
}

void SendMPDialog::clearFields()
{
    ui->sendToLineEdit->setText("");
    ui->amountLineEdit->setText("");
}

void SendMPDialog::updateFrom()
{
    // update wallet balances
    set_wallet_totals();
    updateBalances();

    // check if this from address has sufficient fees for a send, if not light up warning label
    QString selectedFromAddress = ui->sendFromComboBox->currentText();
    int64_t inputTotal = feeCheck(selectedFromAddress.toStdString());
    if (inputTotal>=50000)
    {
       ui->feeWarningLabel->setVisible(false);
    }
    else
    {
       string feeWarning = "Only " + FormatDivisibleAmount(inputTotal) + " BTC are available at the sending address for fees, you can attempt to send the transaction anyway but this *may* not be sufficient.";
       ui->feeWarningLabel->setText(QString::fromStdString(feeWarning));
       ui->feeWarningLabel->setVisible(true);
    }
}

void SendMPDialog::updateProperty()
{
    // update wallet balances
    set_wallet_totals();

    // get currently selected from address
    QString currentSetFromAddress = ui->sendFromComboBox->currentText();

    // clear address selector
    ui->sendFromComboBox->clear();

    // populate from address selector
    QString spId = ui->propertyComboBox->itemData(ui->propertyComboBox->currentIndex()).toString();
    unsigned int propertyId = spId.toUInt();
    LOCK(cs_tally);
    for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
    {
        string address = (my_it->first).c_str();
        unsigned int id;
        bool includeAddress=false;
        (my_it->second).init();
        while (0 != (id = (my_it->second).next()))
        {
            if(id==propertyId) { includeAddress=true; break; }
        }
        if (!includeAddress) continue; //ignore this address, has never transacted in this propertyId
        if (!IsMyAddress(address)) continue; //ignore this address, it's not ours
        ui->sendFromComboBox->addItem((my_it->first).c_str());
    }

    // attempt to set from address back to what was originally in there before update
    int fromIdx = ui->sendFromComboBox->findText(currentSetFromAddress);
    if (fromIdx != -1) { ui->sendFromComboBox->setCurrentIndex(fromIdx); } // -1 means the currently set from address doesn't have a balance in the newly selected property

    // update placeholder text
    if (isPropertyDivisible(propertyId))
    {
        ui->amountLineEdit->setPlaceholderText("Enter Divisible Amount");
    }
    else
    {
        ui->amountLineEdit->setPlaceholderText("Enter Indivisible Amount");
    }
    updateBalances();
}

void SendMPDialog::updateBalances()
{
    // populate balance for currently selected address and global wallet balance
    QString spId = ui->propertyComboBox->itemData(ui->propertyComboBox->currentIndex()).toString();
    unsigned int propertyId = spId.toUInt();
    QString selectedFromAddress = ui->sendFromComboBox->currentText();
    std::string stdSelectedFromAddress = selectedFromAddress.toStdString();
    int64_t balanceAvailable = getUserAvailableMPbalance(stdSelectedFromAddress, propertyId);
    int64_t globalAvailable = 0;
    if (propertyId<2147483648) { globalAvailable = global_balance_money_maineco[propertyId]; } else { globalAvailable = global_balance_money_testeco[propertyId-2147483647]; }
    QString balanceLabel;
    QString globalLabel;
    std::string tokenLabel;
    if (propertyId==1) tokenLabel = " MSC";
    if (propertyId==2) tokenLabel = " TMSC";
    if (propertyId>2) tokenLabel = " SPT";
    if (isPropertyDivisible(propertyId))
    {
        balanceLabel = QString::fromStdString("Address Balance (Available): " + FormatDivisibleAmount(balanceAvailable) + tokenLabel);
        globalLabel = QString::fromStdString("Wallet Balance (Available): " + FormatDivisibleAmount(globalAvailable) + tokenLabel);
    }
    else
    {
        balanceLabel = QString::fromStdString("Address Balance (Available): " + FormatIndivisibleAmount(balanceAvailable) + tokenLabel);
        globalLabel = QString::fromStdString("Wallet Balance (Available): " + FormatIndivisibleAmount(globalAvailable) + tokenLabel);
    }
    ui->addressBalanceLabel->setText(balanceLabel);
    ui->globalBalanceLabel->setText(globalLabel);
}

void SendMPDialog::sendMPTransaction()
{
    // get the property being sent and get divisibility
    QString spId = ui->propertyComboBox->itemData(ui->propertyComboBox->currentIndex()).toString();
    if (spId.toStdString().empty())
    {
        QMessageBox::critical( this, "Unable to send transaction",
        "The property selected is not valid.\n\nPlease double-check the transction details thoroughly before retrying your send transaction." );
        return;
    }
    unsigned int propertyId = spId.toUInt();
    bool divisible = isPropertyDivisible(propertyId);

    // obtain the selected sender address
    string strFromAddress = ui->sendFromComboBox->currentText().toStdString();
    // push recipient address into a CBitcoinAddress type and check validity
    CBitcoinAddress fromAddress;
    if (false == strFromAddress.empty()) { fromAddress.SetString(strFromAddress); }
    if (!fromAddress.IsValid())
    {
        QMessageBox::critical( this, "Unable to send transaction",
        "The sender address selected is not valid.\n\nPlease double-check the transction details thoroughly before retrying your send transaction." );
        return;
    }

    // obtain the entered recipient address
    string strRefAddress = ui->sendToLineEdit->text().toStdString();
    // push recipient address into a CBitcoinAddress type and check validity
    CBitcoinAddress refAddress;
    if (false == strRefAddress.empty()) { refAddress.SetString(strRefAddress); }
    if (!refAddress.IsValid())
    {
        QMessageBox::critical( this, "Unable to send transaction",
        "The recipient address entered is not valid.\n\nPlease double-check the transction details thoroughly before retrying your send transaction." );
        return;
    }

    // warn if we have to truncate the amount due to a decimal amount for an indivisible property, but allow send to continue
    string strAmount = ui->amountLineEdit->text().toStdString();
    if (!divisible)
    {
        size_t pos = strAmount.find(".");
        if (pos!=std::string::npos)
        {
            string tmpStrAmount = strAmount.substr(0,pos);
            string strMsgText = "The amount entered contains a decimal however the property being sent is indivisible.\n\nThe amount entered will be truncated as follows:\n";
            strMsgText += "Original amount entered: " + strAmount + "\nAmount that will be sent: " + tmpStrAmount + "\n\n";
            strMsgText += "Do you still wish to process with the transaction?";
            QString msgText = QString::fromStdString(strMsgText);
            QMessageBox::StandardButton responseClick;
            responseClick = QMessageBox::question(this, "Amount truncation warning", msgText, QMessageBox::Yes|QMessageBox::No);
            if (responseClick == QMessageBox::No)
            {
                QMessageBox::critical( this, "Send transaction cancelled",
                "The send transaction has been cancelled.\n\nPlease double-check the transction details thoroughly before retrying your send transaction." );
                return;
            }
            strAmount = tmpStrAmount;
            ui->amountLineEdit->setText(QString::fromStdString(strAmount));
        }
    }

    // use strToInt64 function to get the amount, using divisibility of the property
    int64_t sendAmount = StrToInt64(strAmount, divisible);
    if (0>=sendAmount)
    {
        QMessageBox::critical( this, "Unable to send transaction",
        "The amount entered is not valid.\n\nPlease double-check the transction details thoroughly before retrying your send transaction." );
        return;
    }

    // check if sending address has enough funds
    int64_t balanceAvailable = getUserAvailableMPbalance(fromAddress.ToString(), propertyId); //getMPbalance(fromAddress.ToString(), propertyId, MONEY);
    if (sendAmount>balanceAvailable)
    {
        QMessageBox::critical( this, "Unable to send transaction",
        "The selected sending address does not have a sufficient balance to cover the amount entered.\n\nPlease double-check the transction details thoroughly before retrying your send transaction." );
        return;
    }

    // check if wallet is still syncing, as this will currently cause a lockup if we try to send - compare our chain to peers to see if we're up to date
    // Bitcoin Core devs have removed GetNumBlocksOfPeers, switching to a time based best guess scenario
    uint32_t intBlockDate = GetLatestBlockTime();  // uint32, not using time_t for portability
    QDateTime currentDate = QDateTime::currentDateTime();
    int secs = QDateTime::fromTime_t(intBlockDate).secsTo(currentDate);
    if(secs > 90*60)
    {
        QMessageBox::critical( this, "Unable to send transaction",
        "The client is still synchronizing.  Sending transactions can currently be performed only when the client has completed synchronizing." );
        return;
    }

    // validation checks all look ok, let's throw up a confirmation dialog
    string strMsgText = "You are about to send the following transaction, please check the details thoroughly:\n\n";
    string propDetails = getPropertyName(propertyId).c_str();
    string spNum = static_cast<ostringstream*>( &(ostringstream() << propertyId) )->str();
    propDetails += " (#" + spNum + ")";
    strMsgText += "From: " + fromAddress.ToString() + "\nTo: " + refAddress.ToString() + "\nProperty: " + propDetails + "\nAmount that will be sent: ";
    if (divisible) { strMsgText += FormatDivisibleAmount(sendAmount); } else { strMsgText += FormatIndivisibleAmount(sendAmount); }
    strMsgText += "\n\nAre you sure you wish to send this transaction?";
    QString msgText = QString::fromStdString(strMsgText);
    QMessageBox::StandardButton responseClick;
    responseClick = QMessageBox::question(this, "Confirm send transaction", msgText, QMessageBox::Yes|QMessageBox::No);
    if (responseClick == QMessageBox::No)
    {
        QMessageBox::critical( this, "Send transaction cancelled",
        "The send transaction has been cancelled.\n\nPlease double-check the transction details thoroughly before retrying your send transaction." );
        return;
    }

    // unlock the wallet
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled/failed
        QMessageBox::critical( this, "Send transaction failed",
        "The send transaction has been cancelled.\n\nThe wallet unlock process must be completed to send a transaction." );
        return;
    }

    // send the transaction - UI will not send any extra reference amounts at this stage
    int code = 0;
    uint256 sendTXID = send_INTERNAL_1packet(fromAddress.ToString(), refAddress.ToString(), fromAddress.ToString(), propertyId, sendAmount, 0, 0, MSC_TYPE_SIMPLE_SEND, 0, &code);
    if (0 != code)
    {
        string strCode = boost::lexical_cast<string>(code);
        string strError;
        switch(code)
        {
            case -212:
                strError = "Error choosing inputs for the send transaction";
                break;
            case -233:
                strError = "Error with redemption address";
                break;
            case -220:
                strError = "Error with redemption address key ID";
                break;
            case -221:
                strError = "Error obtaining public key for redemption address";
                break;
            case -222:
                strError = "Error public key for redemption address is not valid";
                break;
            case -223:
                strError = "Error validating redemption address";
                break;
            case -205:
                strError = "Error with wallet object";
                break;
            case -206:
                strError = "Error with selected inputs for the send transaction";
                break;
            case -211:
                strError = "Error creating transaction (wallet may be locked or fees may not be sufficient)";
                break;
            case -213:
                strError = "Error committing transaction";
                break;
        }
        if (strError.empty()) strError = "Error code does not have associated error text.";
        QMessageBox::critical( this, "Send transaction failed",
        "The send transaction has failed.\n\nThe error code was: " + QString::fromStdString(strCode) + "\nThe error message was:\n" + QString::fromStdString(strError));
        return;
    }
    else
    {
        // call an update of the balances
        set_wallet_totals();
        updateBalances();

        // display the result
        string strSentText = "Your Master Protocol transaction has been sent.\n\nThe transaction ID is:\n\n";
        strSentText += sendTXID.GetHex() + "\n\n";
        QString sentText = QString::fromStdString(strSentText);
        QMessageBox sentDialog;
        sentDialog.setIcon(QMessageBox::Information);
        sentDialog.setWindowTitle("Transaction broadcast successfully");
        sentDialog.setText(sentText);
        sentDialog.setStandardButtons(QMessageBox::Yes|QMessageBox::Ok);
        sentDialog.setDefaultButton(QMessageBox::Ok);
        sentDialog.setButtonText( QMessageBox::Yes, "Copy TXID to clipboard" );
        if(sentDialog.exec() == QMessageBox::Yes)
        {
            // copy TXID to clipboard
            GUIUtil::setClipboard(QString::fromStdString(sendTXID.GetHex()));
        }
        // clear the form
        clearFields();
    }
}

void SendMPDialog::sendFromComboBoxChanged(int idx)
{
    updateFrom();
}

void SendMPDialog::propertyComboBoxChanged(int idx)
{
    updateProperty();
}

void SendMPDialog::clearButtonClicked()
{
    clearFields();
}

void SendMPDialog::sendButtonClicked()
{
    sendMPTransaction();
}

void SendMPDialog::balancesUpdated()
{
    updateBalances();
}
