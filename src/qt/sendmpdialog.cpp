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

#include "mastercore.h"

#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>

SendMPDialog::SendMPDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SendMPDialog),
    model(0)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->addButton->setIcon(QIcon());
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

    // initial update
    updateProperty();
    updateFrom();
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

    // populate balance for currently selected address
    QString spId = ui->propertyComboBox->itemData(ui->propertyComboBox->currentIndex()).toString();
    unsigned int propertyId = spId.toUInt();
    LOCK(cs_tally);
    QString selectedFromAddress = ui->sendFromComboBox->currentText();
    std::string stdSelectedFromAddress = selectedFromAddress.toStdString();
    int64_t balanceAvailable = getMPbalance(stdSelectedFromAddress, propertyId, MONEY);
    QString balanceLabel;
    std::string tokenLabel;
    if (propertyId==1) tokenLabel = " MSC";
    if (propertyId==2) tokenLabel = " TMSC";
    if (propertyId>2) tokenLabel = " SPT";
    if (isPropertyDivisible(propertyId))
    {
        balanceLabel = QString::fromStdString("Address Balance (Available): " + FormatDivisibleMP(balanceAvailable) + tokenLabel);
    }
    else
    {
        balanceLabel = QString::fromStdString("Address Balance (Available): " + FormatIndivisibleMP(balanceAvailable) + tokenLabel);
    }
    ui->addressBalanceLabel->setText(balanceLabel);
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

    // populate balance for currently selected address and global wallet balance
    QString selectedFromAddress = ui->sendFromComboBox->currentText();
    std::string stdSelectedFromAddress = selectedFromAddress.toStdString();
    int64_t balanceAvailable = getMPbalance(stdSelectedFromAddress, propertyId, MONEY);
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
        balanceLabel = QString::fromStdString("Address Balance (Available): " + FormatDivisibleMP(balanceAvailable) + tokenLabel);
        globalLabel = QString::fromStdString("Wallet Balance (Available): " + FormatDivisibleMP(globalAvailable) + tokenLabel);
        ui->amountLineEdit->setPlaceholderText("Enter Divisible Amount");
    }
    else
    {
        balanceLabel = QString::fromStdString("Address Balance (Available): " + FormatIndivisibleMP(balanceAvailable) + tokenLabel);
        globalLabel = QString::fromStdString("Wallet Balance (Available): " + FormatIndivisibleMP(globalAvailable) + tokenLabel);
        ui->amountLineEdit->setPlaceholderText("Enter Indivisible Amount");
    }
    ui->addressBalanceLabel->setText(balanceLabel);
    ui->globalBalanceLabel->setText(globalLabel);
}

void SendMPDialog::sendMPTransaction()
{

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

}

/*
    //GUIUtil::setupAddressWidget(ui->lineEditCoinControlChange, this);

    //addEntry();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    // Coin Control
    connect(ui->pushButtonCoinControl, SIGNAL(clicked()), this, SLOT(coinControlButtonClicked()));
    connect(ui->checkBoxCoinControlChange, SIGNAL(stateChanged(int)), this, SLOT(coinControlChangeChecked(int)));
    connect(ui->lineEditCoinControlChange, SIGNAL(textEdited(const QString &)), this, SLOT(coinControlChangeEdited(const QString &)));

    // Coin Control: clipboard actions
    QAction *clipboardQuantityAction = new QAction(tr("Copy quantity"), this);
    QAction *clipboardAmountAction = new QAction(tr("Copy amount"), this);
    QAction *clipboardFeeAction = new QAction(tr("Copy fee"), this);
    QAction *clipboardAfterFeeAction = new QAction(tr("Copy after fee"), this);
    QAction *clipboardBytesAction = new QAction(tr("Copy bytes"), this);
    QAction *clipboardPriorityAction = new QAction(tr("Copy priority"), this);
    QAction *clipboardLowOutputAction = new QAction(tr("Copy low output"), this);
    QAction *clipboardChangeAction = new QAction(tr("Copy change"), this);
    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardBytes()));
    connect(clipboardPriorityAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardPriority()));
    connect(clipboardLowOutputAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardLowOutput()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(coinControlClipboardChange()));
    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlPriority->addAction(clipboardPriorityAction);
    ui->labelCoinControlLowOutput->addAction(clipboardLowOutputAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

//    fNewRecipientAllowed = true;
}

void SendMPDialog::setModel(WalletModel *model)
{
    this->model = model;

    if(model && model->getOptionsModel())
    {
 //       for(int i = 0; i < ui->entries->count(); ++i)
 //       {
 //           SendMPEntry *entry = qobject_cast<SendMPEntry*>(ui->entries->itemAt(i)->widget());
 //           if(entry)
 //           {
 //               entry->setModel(model);
 //           }
 //       }

 //       setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance());
 //       connect(model, SIGNAL(balanceChanged(qint64, qint64, qint64)), this, SLOT(setBalance(qint64, qint64, qint64)));
 //       connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        // Coin Control
 //       connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
 //       connect(model->getOptionsModel(), SIGNAL(coinControlFeaturesChanged(bool)), this, SLOT(coinControlFeatureChanged(bool)));
 //       connect(model->getOptionsModel(), SIGNAL(transactionFeeChanged(qint64)), this, SLOT(coinControlUpdateLabels()));
 //       ui->frameCoinControl->setVisible(model->getOptionsModel()->getCoinControlFeatures());
 //       coinControlUpdateLabels();
    }
}

SendMPDialog::~SendMPDialog()
{
    delete ui;
}

void SendMPDialog::on_sendButton_clicked()
{
    if(!model || !model->getOptionsModel())
        return;

//    QList<SendCoinsRecipient> recipients;
//    bool valid = true;

//    for(int i = 0; i < ui->entries->count(); ++i)
//    {
//        SendMPEntry *entry = qobject_cast<SendMPEntry*>(ui->entries->itemAt(i)->widget());
//        if(entry)
//        {
//            if(entry->validate())
//            {
//                recipients.append(entry->getValue());
//            }
//            else
//            {
//                valid = false;
//            }
//        }
//    }

//    if(!valid || recipients.isEmpty())
//    {
//        return;
//    }

    // Format confirmation message
    QStringList formatted;
    foreach(const SendCoinsRecipient &rcp, recipients)
    {
        // generate bold amount string
        QString amount = "<b>" + BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), rcp.amount);
        amount.append("</b>");
        // generate monospace address string
        QString address = "<span style='font-family: monospace;'>" + rcp.address;
        address.append("</span>");

        QString recipientElement;

        if (!rcp.paymentRequest.IsInitialized()) // normal payment
        {
            if(rcp.label.length() > 0) // label with address
            {
                recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label));
                recipientElement.append(QString(" (%1)").arg(address));
            }
            else // just address
            {
                recipientElement = tr("%1 to %2").arg(amount, address);
            }
        }
        else if(!rcp.authenticatedMerchant.isEmpty()) // secure payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.authenticatedMerchant));
        }
        else // insecure payment request
        {
            recipientElement = tr("%1 to %2").arg(amount, address);
        }

        formatted.append(recipientElement);
    }

    fNewRecipientAllowed = false;


    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
//        fNewRecipientAllowed = true;
        return;
    }

    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendCoinsReturn prepareStatus;
    if (model->getOptionsModel()->getCoinControlFeatures()) // coin control enabled
        prepareStatus = model->prepareTransaction(currentTransaction, CoinControlDialog::coinControl);
    else
        prepareStatus = model->prepareTransaction(currentTransaction);

    // process prepareStatus and on error generate message shown to user
    processSendMPReturn(prepareStatus,
        BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), currentTransaction.getTransactionFee()));

    if(prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return;
    }

    qint64 txFee = currentTransaction.getTransactionFee();
    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    if(txFee > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    qint64 totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    foreach(BitcoinUnits::Unit u, BitcoinUnits::availableUnits())
    {
        if(u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(BitcoinUnits::formatWithUnit(u, totalAmount));
    }
    questionString.append(tr("Total Amount %1 (= %2)")
        .arg(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount))
        .arg(alternativeUnits.join(" " + tr("or") + " ")));

    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm send coins"),
        questionString.arg(formatted.join("<br />")),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->sendCoins(currentTransaction);
    // process sendStatus and on error generate message shown to user
    processSendMPReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK)
    {
        accept();
        CoinControlDialog::coinControl->UnSelectAll();
        coinControlUpdateLabels();
    }
    fNewRecipientAllowed = true;
}

void SendMPDialog::clear()
{
    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateTabsAndLabels();
}

void SendMPDialog::reject()
{
    clear();
}

void SendMPDialog::accept()
{
    clear();
}

SendMPEntry *SendMPDialog::addEntry()
{
    SendMPEntry *entry = new SendMPEntry(this);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendMPEntry*)), this, SLOT(removeEntry(SendMPEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    updateTabsAndLabels();

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());
    return entry;
}

void SendMPDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    coinControlUpdateLabels();
}

void SendMPDialog::removeEntry(SendMPEntry* entry)
{
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

    entry->deleteLater();

    updateTabsAndLabels();
}

QWidget *SendMPDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendMPEntry *entry = qobject_cast<SendMPEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->sendButton);
    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    return ui->addButton;
}

void SendMPDialog::setAddress(const QString &address)
{
    SendMPEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendMPEntry *first = qobject_cast<SendMPEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setAddress(address);
}

void SendMPDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendMPEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendMPEntry *first = qobject_cast<SendMPEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
    updateTabsAndLabels();
}

bool SendMPDialog::handlePaymentRequest(const SendCoinsRecipient &rv)
{
    QString strSendMP = tr("Send Coins");
    if (rv.paymentRequest.IsInitialized()) {
        // Expired payment request?
        const payments::PaymentDetails& details = rv.paymentRequest.getDetails();
        if (details.has_expires() && (int64_t)details.expires() < GetTime())
        {
            emit message(strSendMP, tr("Payment request expired"),
                CClientUIInterface::MSG_WARNING);
            return false;
        }
    }
    else {
        CBitcoinAddress address(rv.address.toStdString());
        if (!address.IsValid()) {
            emit message(strSendMP, tr("Invalid payment address %1").arg(rv.address),
                CClientUIInterface::MSG_WARNING);
            return false;
        }
    }

    pasteEntry(rv);
    return true;
}

void SendMPDialog::setBalance(qint64 balance, qint64 unconfirmedBalance, qint64 immatureBalance)
{
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);

    if(model && model->getOptionsModel())
    {
        ui->labelMPBalance->setText(BitcoinUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance));
    }
}

void SendMPDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0);
}

void SendMPDialog::processSendMPReturn(const WalletModel::SendCoinsReturn &SendCoinsReturn, const QString &msgArg)
{
    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to SendMPDialog usage of WalletModel::SendCoinsReturn.
    // WalletModel::TransactionCommitFailed is used only in WalletModel::sendMP()
    // all others are used only in WalletModel::prepareTransaction()
    switch(SendCoinsReturn.status)
    {
    case WalletModel::InvalidAddress:
        msgParams.first = tr("The recipient address is not valid, please recheck.");
        break;
    case WalletModel::InvalidAmount:
        msgParams.first = tr("The amount to pay must be larger than 0.");
        break;
    case WalletModel::AmountExceedsBalance:
        msgParams.first = tr("The amount exceeds your balance.");
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        msgParams.first = tr("The total exceeds your balance when the %1 transaction fee is included.").arg(msgArg);
        break;
    case WalletModel::DuplicateAddress:
        msgParams.first = tr("Duplicate address found, can only send to each address once per send operation.");
        break;
    case WalletModel::TransactionCreationFailed:
        msgParams.first = tr("Transaction creation failed!");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::TransactionCommitFailed:
        msgParams.first = tr("The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    // included to prevent a compiler warning.
    case WalletModel::OK:
    default:
        return;
    }

    emit message(tr("Send Coins"), msgParams.first, msgParams.second);
}

*/

