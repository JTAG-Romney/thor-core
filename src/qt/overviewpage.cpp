// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>
#include <qt/forgetablemodel.h>  // Thor: Forge
#include <qt/forgedialog.h>      // Thor: Forge: For formatLargeNoLocale()

#include <QAbstractItemDelegate>
#include <QPainter>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
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
    const PlatformStyle *platformStyle;

};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentCreatedBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchCreatedBalance(-1),
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));

    // Thor: Forge
    cost = rewardsPaid = profit = 0;
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& createdBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchCreatedBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentCreatedBalance = createdBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchCreatedBalance = watchCreatedBalance;
    ui->labelBalance->setText(BitcoinUnits::formatWithUnit(unit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::formatWithUnit(unit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelCreated->setText(BitcoinUnits::formatWithUnit(unit, createdBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::formatWithUnit(unit, balance + unconfirmedBalance + createdBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchCreated->setText(BitcoinUnits::formatWithUnit(unit, watchCreatedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance + watchUnconfBalance + watchCreatedBalance, false, BitcoinUnits::separatorAlways));

    // only show created (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showCreated = createdBalance != 0;
    bool showWatchOnlyCreated = watchCreatedBalance != 0;

    // for symmetry reasons also show created label when the watch-only one is shown
    ui->labelCreated->setVisible(showCreated || showWatchOnlyCreated);
    ui->labelCreatedText->setVisible(showCreated || showWatchOnlyCreated);
    ui->labelWatchCreated->setVisible(showWatchOnlyCreated); // show watch-only created balance
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
        ui->labelWatchCreated->hide();
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
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getCreatedBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchCreatedBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));

        // Thor: Forge: Connect summary updater
        connect(model, SIGNAL(newForgeSummaryAvailable()), this, SLOT(updateForgeSummary()));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

// Thor: Forge: Update the forge summary
void OverviewPage::updateForgeSummary() {
    if (walletModel && walletModel->getForgeTableModel()) {
        int created, ready, dead, blocksFound;
        walletModel->getForgeTableModel()->getSummaryValues(created, ready, dead, blocksFound, cost, rewardsPaid, profit);

        ui->rewardsPaidLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rewardsPaid)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->costLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), cost)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->profitLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), profit)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->readyLabel->setText(ForgeDialog::formatLargeNoLocale(ready));
        ui->createdLabel->setText(ForgeDialog::formatLargeNoLocale(created));
        ui->blocksFoundLabel->setText(ForgeDialog::formatLargeNoLocale(blocksFound));
        
        if (dead > 0) {
            ui->deadLabel->setText(ForgeDialog::formatLargeNoLocale(dead));
            ui->deadLabel->setVisible(true);
            ui->deadPreLabel->setVisible(true);
            ui->deadPostLabel->setVisible(true);
        } else {
            ui->deadLabel->setVisible(false);
            ui->deadPreLabel->setVisible(false);
            ui->deadPostLabel->setVisible(false);         
        }
    }
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentCreatedBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchCreatedBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();

        // Thor: Forge: Update CAmounts in forge summary
        ui->rewardsPaidLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rewardsPaid)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->costLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), cost)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
        ui->profitLabel->setText(
            BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), profit)
            + " "
            + BitcoinUnits::shortName(walletModel->getOptionsModel()->getDisplayUnit())
        );
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

// Thor: Forge: Handle hammer button click
void OverviewPage::on_hammerButton_clicked() {
    Q_EMIT hammerButtonClicked();
}