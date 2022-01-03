/*
 * Qt4 bitcoin GUI.
 *
 * W.J. van der Laan 2011-2012
 * The Bitcoin Developers 2011-2012
 */

#include <QApplication>

#include "bitcoingui.h"

#include "transactiontablemodel.h"
#include "addressbookpage.h"
#include "sendcoinsdialog.h"
#include "signmessagepage.h"
#include "verifymessagepage.h"
#include "stakingpage.h"
#include "dynamicpegpage.h"
#include "blockchainpage.h"
#include "blockchainmodel.h"
#include "optionsdialog.h"
#include "aboutdialog.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "editaddressdialog.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "addresstablemodel.h"
#include "transactionview.h"
#include "overviewpage.h"
#include "bitcoinunits.h"
#include "guiconstants.h"
#include "askpassphrasedialog.h"
#include "notificator.h"
#include "guiutil.h"
#include "rpcconsole.h"
#include "wallet.h"
#include "init.h"
#include "ui_interface.h"
#include "qwt/reserve_meter.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QStackedWidget>
#include <QDateTime>
#include <QMovie>
#include <QFileDialog>
#include <QDesktopServices>
#include <QTimer>
#include <QDragEnterEvent>
#include <QUrl>
#include <QMimeData>
#include <QStyle>
#include <QToolButton>
#include <QButtonGroup>

#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>

#include <iostream>

#include "qwt/qwt_plot.h"
#include "qwt/qwt_plot_curve.h"

extern CWallet* pwalletMain;
extern int64_t nLastCoinStakeSearchInterval;
double GetPoSKernelPS();

BitcoinGUI::BitcoinGUI(QWidget *parent):
    QMainWindow(parent),
    clientModel(0),
    walletModel(0),
    encryptWalletAction(0),
    changePassphraseAction(0),
    unlockWalletAction(0),
    lockWalletAction(0),
    aboutQtAction(0),
    trayIcon(0),
    notificator(0),
    rpcConsole(0),
    prevBlocks(0),
    nWeight(0)
{
    resize(850+95, 550);
    setWindowTitle(tr("BitBay") + " - " + tr("Wallet"));
#ifndef Q_OS_MAC
    qApp->setWindowIcon(QIcon(":icons/bitcoin"));
    setWindowIcon(QIcon(":icons/bitcoin"));
#else
    //setUnifiedTitleAndToolBarOnMac(true);
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif
    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the tray icon (or setup the dock icon)
    createTrayIcon();

    // Create tabs
    overviewPage = new OverviewPage();

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    transactionsPage->setLayout(vbox);

    addressBookPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab);

    receiveCoinsPage = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab);

    sendCoinsPage = new SendCoinsDialog(this);

    signMessagePage = new SignMessagePage(this);

    verifyMessagePage = new VerifyMessagePage(this);

    stakingPage = new StakingPage(this);

    dynamicPegPage = new DynamicPegPage(this);
    
    infoPage = new BlockchainPage(this);
    
    centralStackedWidget = new QStackedWidget(this);
    centralStackedWidget->addWidget(overviewPage);
    centralStackedWidget->addWidget(transactionsPage);
    centralStackedWidget->addWidget(addressBookPage);
    centralStackedWidget->addWidget(receiveCoinsPage);
    centralStackedWidget->addWidget(sendCoinsPage);
    centralStackedWidget->addWidget(signMessagePage);
    centralStackedWidget->addWidget(verifyMessagePage);
    centralStackedWidget->addWidget(stakingPage);
    centralStackedWidget->addWidget(dynamicPegPage);
    centralStackedWidget->addWidget(infoPage);

    connect(centralStackedWidget, SIGNAL(currentChanged(int)),
            this, SLOT(onTabChanged(int)));
    
    QWidget * leftPanel = new QWidget();
    leftPanel->setFixedWidth(160);
    leftPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding);
    leftPanel->setStyleSheet("QWidget { background-color: rgb(255,255,255); }");

    QHBoxLayout * centralHLayout = new QHBoxLayout;
    centralHLayout->setSpacing(0);
    centralHLayout->setMargin(0);
    centralHLayout->addWidget(leftPanel);
    centralHLayout->addWidget(centralStackedWidget);

    QHBoxLayout * headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(0);
    headerLayout->setMargin(0);

    QWidget* header = new QWidget();
    header->setFixedSize(160, 70);
    header->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    header->setStyleSheet("QWidget { background-color: rgb(75,78,162); background-repeat: no-repeat; background-image: url(:/images/header); background-position: top center; }");
    headerLayout->addWidget(header);

    QWidget* space1 = new QWidget();
    space1->setFixedHeight(70);
    space1->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    space1->setStyleSheet("QWidget { background-color: rgb(75,78,162); }");
    headerLayout->addWidget(space1);
    
    QGridLayout * topHeaderLayout = new QGridLayout;
    topHeaderLayout->setSpacing(0);
    topHeaderLayout->setMargin(7);
    space1->setLayout(topHeaderLayout);

    lastBlockLabel = new QLabel;
    lastBlockLabel->setText(tr("Last block:"));
    lastBlockLabel->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    lastBlockLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    topHeaderLayout->addWidget(lastBlockLabel, 0,0);

    oneBayRateLabel = new QLabel;
    oneBayRateLabel->setText(tr("1 BAY = ??? USD"));
    oneBayRateLabel->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    oneBayRateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    topHeaderLayout->addWidget(oneBayRateLabel, 1,0);

    oneUsdRateLabel = new QLabel;
    oneUsdRateLabel->setText(tr("1 USD = ??? BAY"));
    oneUsdRateLabel->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    oneUsdRateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    topHeaderLayout->addWidget(oneUsdRateLabel, 2,0);
    
    QWidget* space12 = new QWidget();
    space12->setFixedHeight(70);
    space12->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    space12->setStyleSheet("QWidget { background-color: rgb(75,78,162); }");
    topHeaderLayout->addWidget(space12, 2,1);

    pegNowTextLabel = new QLabel;
    pegNowTextLabel->setText(tr("Peg index now - 200: "));
    pegNowTextLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pegNowTextLabel->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    topHeaderLayout->addWidget(pegNowTextLabel, 0,2);
    pegNextTextLabel = new QLabel;
    pegNextTextLabel->setText(tr("200 - 400: "));
    pegNextTextLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pegNextTextLabel->setStyleSheet("QLabel { color: rgba(240,240,240, 170); }");
    topHeaderLayout->addWidget(pegNextTextLabel, 1,2);
    pegNextNextTextLabel = new QLabel;
    pegNextNextTextLabel->setText(tr("400 - 600: "));
    pegNextNextTextLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pegNextNextTextLabel->setStyleSheet("QLabel { color: rgba(240,240,240, 100); }");
    topHeaderLayout->addWidget(pegNextNextTextLabel, 2,2);
    
    pegNowLabel = new QLabel;
    pegNowLabel->setText(tr(""));
    pegNowLabel->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    pegNowLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    pegNowLabel->setMinimumWidth(20);
    topHeaderLayout->addWidget(pegNowLabel, 0,3);
    pegNextLabel = new QLabel;
    pegNextLabel->setText(tr(""));
    pegNextLabel->setStyleSheet("QLabel { color: rgba(240,240,240, 170); }");
    pegNextLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    topHeaderLayout->addWidget(pegNextLabel, 1,3);
    pegNextNextLabel = new QLabel;
    pegNextNextLabel->setText(tr(""));
    pegNextNextLabel->setStyleSheet("QLabel { color: rgba(240,240,240, 100); }");
    pegNextNextLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    topHeaderLayout->addWidget(pegNextNextLabel, 2,3);
    
    QWidget* space13 = new QWidget();
    space13->setFixedHeight(10);
    space13->setFixedWidth(50);
    space13->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    space13->setStyleSheet("QWidget { background-color: rgb(75,78,162); }");
    topHeaderLayout->addWidget(space13, 2,4);
    
    auto lv1 = new QLabel;
    lv1->setText(tr("Inflate: "));
    lv1->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lv1->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    topHeaderLayout->addWidget(lv1, 0,5);
    auto lv2 = new QLabel;
    lv2->setText(tr("Deflate: "));
    lv2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lv2->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    topHeaderLayout->addWidget(lv2, 1,5);
    auto lv3 = new QLabel;
    lv3->setText(tr("No change: "));
    lv3->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lv3->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    topHeaderLayout->addWidget(lv3, 2,5);
    
    inflateLabel = new QLabel;
    inflateLabel->setText(tr(""));
    inflateLabel->setStyleSheet("QLabel { color: #2da5e0; }");
    inflateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    inflateLabel->setMinimumWidth(20);
    topHeaderLayout->addWidget(inflateLabel, 0,6);
    deflateLabel = new QLabel;
    deflateLabel->setText(tr(""));
    deflateLabel->setStyleSheet("QLabel { color: #c06a15; }");
    deflateLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    topHeaderLayout->addWidget(deflateLabel, 1,6);
    nochangeLabel = new QLabel;
    nochangeLabel->setText(tr(""));
    nochangeLabel->setStyleSheet("QLabel { color: rgb(240,240,240); }");
    nochangeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    topHeaderLayout->addWidget(nochangeLabel, 2,6);
    
    liquidMeter = new ReserveMeter;
    liquidMeter->setFixedSize(180,180);
    topHeaderLayout->addWidget(liquidMeter, 0,7, 3,1);
    
    QWidget *centralWidget = new QWidget();
    QVBoxLayout *centralLayout = new QVBoxLayout(centralWidget);
    centralLayout->addLayout(headerLayout);
    centralLayout->addLayout(centralHLayout);
    centralLayout->setMargin(0);
    centralLayout->setSpacing(0);

    setCentralWidget(centralWidget);

    // Create status bar
    statusBar();

    QVBoxLayout * leftPanelLayout = new QVBoxLayout(leftPanel);
    leftPanelLayout->setMargin(0);
    leftPanelLayout->setSpacing(0);

    QButtonGroup * tabsGroup = new QButtonGroup(leftPanel);

    QString tabStyle = R"(
        QWidget {
            background-color: rgb(255,255,255);
        }
        QToolButton {
            border: 0px solid rgb(220,220,220);
            border-bottom: 1px solid rgb(220,220,220);
            padding: 3px;
        }
        QToolButton:checked {
            background-color: rgb(237,238,246);
        }
    )";
    leftPanel->setStyleSheet(tabStyle);

#ifdef Q_OS_MAC
    QFont font("Roboto Condensed", 15, QFont::Bold);
#else
    QFont font("Roboto Condensed", 11, QFont::Bold);
#endif

    tabDashboard = new QToolButton();
    tabDashboard->setFixedSize(160,50);
    tabDashboard->setText(tr("DASHBOARD"));
    tabDashboard->setCheckable(true);
    tabDashboard->setAutoRaise(true);
    tabDashboard->setChecked(true);
    tabDashboard->setFont(font);
    tabDashboard->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabDashboard->setIcon(QIcon(":/icons/overview"));
    tabDashboard->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabDashboard);
    leftPanelLayout->addWidget(tabDashboard);

    tabReceive = new QToolButton();
    tabReceive->setFixedSize(160,50);
    tabReceive->setText(tr("RECEIVE"));
    tabReceive->setCheckable(true);
    tabReceive->setAutoRaise(true);
    tabReceive->setFont(font);
    tabReceive->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabReceive->setIcon(QIcon(":/icons/receiving_addresses"));
    tabReceive->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabReceive);
    leftPanelLayout->addWidget(tabReceive);

    tabSend = new QToolButton();
    tabSend->setFixedSize(160,50);
    tabSend->setText(tr("SEND"));
    tabSend->setCheckable(true);
    tabSend->setAutoRaise(true);
    tabSend->setFont(font);
    tabSend->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabSend->setIcon(QIcon(":/icons/send"));
    tabSend->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabSend);
    leftPanelLayout->addWidget(tabSend);

    tabTransactions = new QToolButton();
    tabTransactions->setFixedSize(160,50);
    tabTransactions->setText(tr("TRANSACTIONS"));
    tabTransactions->setCheckable(true);
    tabTransactions->setAutoRaise(true);
    tabTransactions->setFont(font);
    tabTransactions->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabTransactions->setIcon(QIcon(":/icons/history"));
    tabTransactions->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabTransactions);
    leftPanelLayout->addWidget(tabTransactions);

    tabAddresses = new QToolButton();
    tabAddresses->setFixedSize(160,50);
    tabAddresses->setText(tr("ADDRESS BOOK"));
    tabAddresses->setCheckable(true);
    tabAddresses->setAutoRaise(true);
    tabAddresses->setFont(font);
    tabAddresses->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabAddresses->setIcon(QIcon(":/icons/address-book"));
    tabAddresses->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabAddresses);
    leftPanelLayout->addWidget(tabAddresses);

    tabSign = new QToolButton();
    tabSign->setFixedSize(160,50);
    tabSign->setText(tr("SIGN"));
    tabSign->setCheckable(true);
    tabSign->setAutoRaise(true);
    tabSign->setFont(font);
    tabSign->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabSign->setIcon(QIcon(":/icons/sign"));
    tabSign->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabSign);
    leftPanelLayout->addWidget(tabSign);

    tabVerify = new QToolButton();
    tabVerify->setFixedSize(160,50);
    tabVerify->setText(tr("VERIFY"));
    tabVerify->setCheckable(true);
    tabVerify->setAutoRaise(true);
    tabVerify->setFont(font);
    tabVerify->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabVerify->setIcon(QIcon(":/icons/verify"));
    tabVerify->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabVerify);
    leftPanelLayout->addWidget(tabVerify);

    tabStaking = new QToolButton();
    tabStaking->setFixedSize(160,50);
    tabStaking->setText(tr("STAKING"));
    tabStaking->setCheckable(true);
    tabStaking->setAutoRaise(true);
    tabStaking->setFont(font);
    tabStaking->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabStaking->setIcon(QIcon(":/icons/mining"));
    tabStaking->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabStaking);
    leftPanelLayout->addWidget(tabStaking);
    
#if defined(ENABLE_EXCHANGE)
    tabStaking->setEnabled(false);
#endif

    tabDynamicPeg = new QToolButton();
    tabDynamicPeg->setFixedSize(160,50);
    tabDynamicPeg->setText(tr("DYNAMIC PEG"));
    tabDynamicPeg->setCheckable(true);
    tabDynamicPeg->setAutoRaise(true);
    tabDynamicPeg->setFont(font);
    tabDynamicPeg->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabDynamicPeg->setIcon(QIcon(":/icons/dynpeg"));
    tabDynamicPeg->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabDynamicPeg);
    leftPanelLayout->addWidget(tabDynamicPeg);
    
#if defined(ENABLE_EXCHANGE)
    tabDynamicPeg->setEnabled(false);
#endif
    
    tabInfo = new QToolButton();
    tabInfo->setFixedSize(160,50);
    tabInfo->setText(tr("BLOCKCHAIN"));
    tabInfo->setCheckable(true);
    tabInfo->setAutoRaise(true);
    tabInfo->setFont(font);
    tabInfo->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tabInfo->setIcon(QIcon(":/icons/info"));
    tabInfo->setIconSize(QSize(16,16));
    tabsGroup->addButton(tabInfo);
    leftPanelLayout->addWidget(tabInfo);
    
    connect(tabDashboard, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabDashboard, SIGNAL(clicked()), this, SLOT(gotoOverviewPage()));
    connect(tabReceive, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabReceive, SIGNAL(clicked()), this, SLOT(gotoReceiveCoinsPage()));
    connect(tabSend, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabSend, SIGNAL(clicked()), this, SLOT(gotoSendCoinsPage()));
    connect(tabTransactions, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabTransactions, SIGNAL(clicked()), this, SLOT(gotoHistoryPage()));
    connect(tabAddresses, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabAddresses, SIGNAL(clicked()), this, SLOT(gotoAddressBookPage()));
    connect(tabSign, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabSign, SIGNAL(clicked()), this, SLOT(gotoSignMessagePage()));
    connect(tabVerify, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabVerify, SIGNAL(clicked()), this, SLOT(gotoVerifyMessagePage()));
    connect(tabStaking, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabStaking, SIGNAL(clicked()), this, SLOT(gotoStakingPage()));
    connect(tabDynamicPeg, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabDynamicPeg, SIGNAL(clicked()), this, SLOT(gotoDynamicPegPage()));
    connect(tabInfo, SIGNAL(clicked()), this, SLOT(showNormalIfMinimized()));
    connect(tabInfo, SIGNAL(clicked()), this, SLOT(gotoInfoPage()));

    QWidget * space2 = new QWidget;
    space2->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding);

    leftPanelLayout->addWidget(space2);

    // Status bar notification icons
    QWidget *frameBlocks = new QWidget();
    frameBlocks->setContentsMargins(0,0,0,0);
    frameBlocks->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    frameBlocks->setStyleSheet("QWidget { background: none; margin-bottom: 5px; }");
    QHBoxLayout *frameBlocksLayout = new QHBoxLayout(frameBlocks);
    frameBlocksLayout->setContentsMargins(3,0,3,0);
    frameBlocksLayout->setSpacing(3);
    frameBlocksLayout->setAlignment(Qt::AlignHCenter);
    labelEncryptionIcon = new QLabel();
    labelStakingIcon = new GUIUtil::ClickableLabel();
    labelConnectionsIcon = new GUIUtil::ClickableLabel();
    labelBlocksIcon = new GUIUtil::ClickableLabel();
    
    connect(labelStakingIcon, SIGNAL(clicked()),
            this, SLOT(gotoStakingPage()));
    connect(labelConnectionsIcon, SIGNAL(clicked()),
            this, SLOT(gotoInfoPageNet()));
    connect(labelBlocksIcon, SIGNAL(clicked()),
            this, SLOT(gotoInfoPageBlocks()));
    
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelEncryptionIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelStakingIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelConnectionsIcon);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();
    leftPanelLayout->addWidget(frameBlocks);

#if !defined(ENABLE_EXCHANGE)
    if (GetBoolArg("-staking", true))
    {
        QTimer *timerStakingIcon = new QTimer(labelStakingIcon);
        connect(timerStakingIcon, SIGNAL(timeout()), this, SLOT(updateStakingIcon()));
        timerStakingIcon->start(30 * 1000);
        updateStakingIcon();
    }
#endif
    
    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setStyleSheet("QLabel { padding-left: 10px; }");
    progressBarLabel->setVisible(false);
    progressBar = new QProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    progressBar->setStyleSheet(R"(

        QProgressBar {
            max-height: 18px;
            background-color: #e4e5f1;
            border: 0px solid grey;
            border-radius: 0px;
            padding: 0px;
            margin: 4px;
            color: #666666;
            text-align: center;
        }
        QProgressBar::chunk {
            background: #FFD402;
            border-radius: 0px;
            margin: 0px;
        }

    )");

    statusBar()->setStyleSheet("QWidget { background-color: rgb(75,78,162); color: rgb(193,193,193); }");
    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);

    //progressBarLabel->parentWidget()->setStyleSheet("QWidget{border: 0px solid grey; }");

    syncIconMovie = new QMovie(":/movies/update_spinner", "mng", this);

    // Clicking on a transaction on the overview page simply sends you to transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), this, SLOT(gotoHistoryPage()));
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    rpcConsole = new RPCConsole(this);
    connect(openRPCConsoleAction, SIGNAL(triggered()), rpcConsole, SLOT(show()));
    // prevents an oben debug window from becoming stuck/unusable on client shutdown
    connect(quitAction, SIGNAL(triggered()), rpcConsole, SLOT(hide()));

    // Clicking on "Verify Message" in the address book sends you to the verify message tab
    connect(addressBookPage, SIGNAL(verifyMessage(QString)), this, SLOT(gotoVerifyMessageTab(QString)));
    // Clicking on "Sign Message" in the receive coins page sends you to the sign message tab
    connect(receiveCoinsPage, SIGNAL(signMessage(QString)), this, SLOT(gotoSignMessageTab(QString)));

    gotoOverviewPage();
    
    // Request rates
    netAccessManager = new QNetworkAccessManager(this);
    connect(netAccessManager, &QNetworkAccessManager::finished,
            this, &BitcoinGUI::netDataReplyFinished);
    
    QTimer* timer1 = new QTimer(this);
    connect(timer1, SIGNAL(timeout()), this, SLOT(ratesRequestInitiate()));
    timer1->start(1000*60*15); // updates rates every 15 minutes

    QTimer* timer2 = new QTimer(this);
    connect(timer2, SIGNAL(timeout()), this, SLOT(releaseRequestInitiate()));
    timer2->start(1000*60*60*24*2); // check the release info every 2 days
}

BitcoinGUI::~BitcoinGUI()
{
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
#endif
}

void BitcoinGUI::createActions()
{
    QActionGroup *tabGroup = new QActionGroup(this);

    overviewAction = new QAction(QIcon(":/icons/overview"), tr("&Dashboard"), this);
    overviewAction->setToolTip(tr("Show general overview of wallet"));
    overviewAction->setCheckable(true);
    overviewAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_1));
    tabGroup->addAction(overviewAction);

    receiveCoinsAction = new QAction(QIcon(":/icons/receiving_addresses"), tr("&Receive"), this);
    receiveCoinsAction->setToolTip(tr("Show the list of addresses for receiving payments"));
    receiveCoinsAction->setCheckable(true);
    receiveCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_2));
    tabGroup->addAction(receiveCoinsAction);

    sendCoinsAction = new QAction(QIcon(":/icons/send"), tr("&Send"), this);
    sendCoinsAction->setToolTip(tr("Send coins to a BitBay address"));
    sendCoinsAction->setCheckable(true);
    sendCoinsAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_3));
    tabGroup->addAction(sendCoinsAction);

    historyAction = new QAction(QIcon(":/icons/history"), tr("&Transactions"), this);
    historyAction->setToolTip(tr("Browse transaction history"));
    historyAction->setCheckable(true);
    historyAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_4));
    tabGroup->addAction(historyAction);

    addressBookAction = new QAction(QIcon(":/icons/address-book"), tr("&Address Book"), this);
    addressBookAction->setToolTip(tr("Edit the list of stored addresses and labels"));
    addressBookAction->setCheckable(true);
    addressBookAction->setShortcut(QKeySequence(Qt::ALT + Qt::Key_5));
    tabGroup->addAction(addressBookAction);

    connect(overviewAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(overviewAction, SIGNAL(triggered()), this, SLOT(gotoOverviewPage()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(historyAction, SIGNAL(triggered()), this, SLOT(gotoHistoryPage()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(addressBookAction, SIGNAL(triggered()), this, SLOT(gotoAddressBookPage()));

    quitAction = new QAction(tr("E&xit"), this);
    quitAction->setToolTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(tr("&About BitBay"), this);
    aboutAction->setToolTip(tr("Show information about BitBay"));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutQtAction = new QAction(tr("About &Qt"), this);
    aboutQtAction->setToolTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(tr("&Options..."), this);
    optionsAction->setToolTip(tr("Modify configuration options for BitBay"));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    toggleHideAction = new QAction(QIcon(":/icons/bitcoin"), tr("&Show / Hide"), this);
    encryptWalletAction = new QAction(tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setToolTip(tr("Encrypt or decrypt wallet"));
    backupWalletAction = new QAction(tr("&Backup Wallet..."), this);
    backupWalletAction->setToolTip(tr("Backup wallet to another location"));
    changePassphraseAction = new QAction(tr("&Change Passphrase..."), this);
    changePassphraseAction->setToolTip(tr("Change the passphrase used for wallet encryption"));
    unlockWalletAction = new QAction(tr("&Unlock Wallet..."), this);
    unlockWalletAction->setToolTip(tr("Unlock wallet"));
    lockWalletAction = new QAction(tr("&Lock Wallet"), this);
    lockWalletAction->setToolTip(tr("Lock wallet"));
    signMessageAction = new QAction(tr("Sign &message..."), this);
    verifyMessageAction = new QAction(tr("&Verify message..."), this);

    exportAction = new QAction(tr("&Export..."), this);
    exportAction->setToolTip(tr("Export the data in the current tab to a file"));
    openRPCConsoleAction = new QAction(tr("&Debug window"), this);
    openRPCConsoleAction->setToolTip(tr("Open debugging and diagnostic console"));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(encryptWalletAction, SIGNAL(triggered()), this, SLOT(encryptWallet()));
    connect(backupWalletAction, SIGNAL(triggered()), this, SLOT(backupWallet()));
    connect(changePassphraseAction, SIGNAL(triggered()), this, SLOT(changePassphrase()));
    connect(unlockWalletAction, SIGNAL(triggered()), this, SLOT(unlockWallet()));
    connect(lockWalletAction, SIGNAL(triggered()), this, SLOT(lockWallet()));
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
}

void BitcoinGUI::createMenuBar()
{
    // workaround for unity's global menu
    //if (qgetenv("QT_QPA_PLATFORMTHEME") == "appmenu-qt5")
    //    appMenuBar = menuBar();
    //else

    //appMenuBar = new QMenuBar();
    appMenuBar = menuBar();
    //appMenuBar->setStyleSheet("QWidget { background: rgb(255,255,255); }");
    appMenuBar->setStyleSheet("QWidget { background: none; }");

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    file->addAction(backupWalletAction);
    file->addAction(exportAction);
    file->addAction(signMessageAction);
    file->addAction(verifyMessageAction);
    file->addSeparator();
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    settings->addAction(encryptWalletAction);
    settings->addAction(changePassphraseAction);
    settings->addAction(unlockWalletAction);
    settings->addAction(lockWalletAction);
    settings->addSeparator();
    settings->addAction(optionsAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    help->addAction(openRPCConsoleAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void BitcoinGUI::setClientModel(ClientModel *clientModel)
{
    this->clientModel = clientModel;
    if(clientModel)
    {
        // Replace some strings and icons, when using the testnet
        if(clientModel->isTestNet())
        {
            setWindowTitle(windowTitle() + QString(" ") + tr("[testnet]"));
#ifndef Q_OS_MAC
            qApp->setWindowIcon(QIcon(":icons/bitcoin_testnet"));
            setWindowIcon(QIcon(":icons/bitcoin_testnet"));
#else
            MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
#endif
            if(trayIcon)
            {
                trayIcon->setToolTip(tr("BitBay client") + QString(" ") + tr("[testnet]"));
#ifdef Q_WS_WIN
                trayIcon->setIcon(QIcon(":/icons/toolbar_testnet"));
#else
                trayIcon->setIcon(QIcon(":/icons/trayicon32_testnet"));
#endif
                toggleHideAction->setIcon(QIcon(":/icons/toolbar_testnet"));
            }
        }

        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(setNumConnections(int)));

        setNumBlocks(clientModel->getNumBlocks());
        connect(clientModel, SIGNAL(numBlocksChanged(int)), this, SLOT(setNumBlocks(int)));
        connect(clientModel, SIGNAL(numBlocksChanged(int)), infoPage->blockchainModel(), SLOT(setNumBlocks(int)));
        QTimer * numBlocksLabelTimer = new QTimer(this);
        connect(numBlocksLabelTimer, SIGNAL(timeout()), this, SLOT(updateNumBlocksLabel()));
        numBlocksLabelTimer->setInterval(1000);
        numBlocksLabelTimer->start();

        // Receive and report messages from network/worker thread
        connect(clientModel, SIGNAL(message(QString,QString,bool,unsigned int)), this, SLOT(message(QString,QString,bool,unsigned int)));

        infoPage->setClientModel(clientModel);
        overviewPage->setClientModel(clientModel);
        rpcConsole->setClientModel(clientModel);
        addressBookPage->setOptionsModel(clientModel->getOptionsModel());
        receiveCoinsPage->setOptionsModel(clientModel->getOptionsModel());
    }
}

void BitcoinGUI::setWalletModel(WalletModel *walletModel)
{
    this->walletModel = walletModel;
    if(walletModel)
    {
        // Receive and report messages from wallet thread
        connect(walletModel, SIGNAL(message(QString,QString,bool,unsigned int)), this, SLOT(message(QString,QString,bool,unsigned int)));

        // Put transaction list in tabs
        transactionView->setModel(walletModel);
        overviewPage->setWalletModel(walletModel);
        addressBookPage->setModel(walletModel->getAddressTableModel());
        receiveCoinsPage->setModel(walletModel->getAddressTableModel());
        sendCoinsPage->setModel(walletModel);
        signMessagePage->setModel(walletModel);
        verifyMessagePage->setModel(walletModel);
        stakingPage->setWalletModel(walletModel);
        dynamicPegPage->setWalletModel(walletModel);

        setEncryptionStatus(walletModel->getEncryptionStatus());
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex,int,int)),
                this, SLOT(incomingTransaction(QModelIndex,int,int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock()), this, SLOT(unlockWallet()));
        
        if (!vFirstRetrievedBayRates.empty()) {
            walletModel->setBayRates(vFirstRetrievedBayRates);
            vFirstRetrievedBayRates.clear();
        }
        if (!vFirstRetrievedBtcRates.empty()) {
            walletModel->setBtcRates(vFirstRetrievedBtcRates);
            vFirstRetrievedBtcRates.clear();
        }
    }
}

void BitcoinGUI::createTrayIcon()
{
    QMenu *trayIconMenu;
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    trayIconMenu = new QMenu(this);
    trayIconMenu->setStyleSheet("QWidget { background: none; }");
    trayIcon->setContextMenu(trayIconMenu);
    trayIcon->setToolTip(tr("BitBay client"));
#ifdef Q_WS_WIN
    trayIcon->setIcon(QIcon(":/icons/toolbar"));
#else
    trayIcon->setIcon(QIcon(":/icons/trayicon32"));
#endif
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    trayIcon->show();
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(receiveCoinsAction);
    trayIconMenu->addAction(sendCoinsAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif

    notificator = new Notificator(qApp->applicationName(), trayIcon);
}

#ifndef Q_OS_MAC
void BitcoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHideAction->trigger();
    }
}
#endif

void BitcoinGUI::optionsClicked()
{
    if(!clientModel || !clientModel->getOptionsModel())
        return;
    OptionsDialog dlg;
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void BitcoinGUI::aboutClicked()
{
    AboutDialog dlg;
    dlg.setModel(clientModel);
    dlg.exec();
}

void BitcoinGUI::setNumConnections(int count)
{
    QString icon;
    switch(count)
    {
    case 0: icon = ":/icons/connect_0"; break;
    case 1: case 2: case 3: icon = ":/icons/connect_1"; break;
    case 4: case 5: case 6: icon = ":/icons/connect_2"; break;
    case 7: case 8: case 9: icon = ":/icons/connect_3"; break;
    default: icon = ":/icons/connect_4"; break;
    }
    labelConnectionsIcon->setPixmap(QIcon(icon).pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
    labelConnectionsIcon->setToolTip(tr("%n active connection(s) to BitBay network", "", count));
}

void BitcoinGUI::setNumBlocks(int count)
{
    // don't show / hide progress bar and its label if we have no connection to the network
    if (!clientModel /* || (clientModel->getNumConnections() == 0 && !clientModel->isImporting()) */)
    {
        statusBar()->setVisible(false);

        return;
    }

    QString tooltip;

    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    QDateTime currentDate = QDateTime::currentDateTime();
    int totalSecs = GetTime() - Params().GenesisBlock().GetBlockTime();
    int secs = lastBlockDate.secsTo(currentDate);

    tooltip = tr("Processed %1 blocks of transaction history.").arg(count);

    // Set icon state: spinning if catching up, tick otherwise
    if(secs < 90*60)
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(QIcon(":/icons/synced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));

        overviewPage->showOutOfSyncWarning(false);

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
    }
    else
    {
        // Represent time from last generated block in human readable text
        QString time_behind_text = timeBehindText(secs);

        progressBarLabel->setText(tr(clientModel->isImporting() ? "Importing blocks..." : "Synchronizing with network..."));
        progressBarLabel->setVisible(true);
        progressBar->setFormat(tr("%1 behind").arg(time_behind_text));
        progressBar->setMaximum(totalSecs);
        progressBar->setValue(totalSecs - secs);
        progressBar->setVisible(true);

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        //labelBlocksIcon->setMovie(syncIconMovie);
        labelBlocksIcon->setPixmap(QIcon(":/icons/unsynced").pixmap(STATUSBAR_ICONSIZE, STATUSBAR_ICONSIZE));
        if(count != prevBlocks)
            syncIconMovie->jumpToNextFrame();
        prevBlocks = count;

        overviewPage->showOutOfSyncWarning(true);

        tooltip += QString("<br>");
        tooltip += tr("Last received block was generated %1 ago.").arg(time_behind_text);
        tooltip += QString("<br>");
        tooltip += tr("Transactions after this will not yet be visible.");
    }

    // Don't word-wrap this (fixed-width) tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
    lastBlockLabel->setText(tr("Last block: %1, %2 ago").arg(count).arg(timeBehindText(secs)));
    updatePegInfo1Label();
}

void BitcoinGUI::updateNumBlocksLabel() 
{
    int count = clientModel->getNumBlocks();
    QDateTime lastBlockDate = clientModel->getLastBlockDate();
    QDateTime currentDate = QDateTime::currentDateTime();
    int secs = lastBlockDate.secsTo(currentDate);
    
    lastBlockLabel->setText(tr("Last block: %1, %2 ago").arg(count).arg(timeBehindText(secs)));
    updatePegInfo1Label();
}

void BitcoinGUI::updatePegInfo1Label() 
{
    int last_block_num = clientModel->getNumBlocks();
    int peg_supply = clientModel->getPegSupplyIndex();
    int peg_next_supply = clientModel->getPegNextSupplyIndex();
    int peg_next_next_supply = clientModel->getPegNextNextSupplyIndex();
    int peg_start = clientModel->getPegStartBlockNum();
    Q_UNUSED(peg_start);
    int votes_inflate, votes_deflate, votes_nochange;
    boost::tie(votes_inflate, votes_deflate, votes_nochange) = clientModel->getPegVotes();
    int peg_interval = Params().PegInterval(last_block_num);
    int interval_num = last_block_num / peg_interval;
    pegNowTextLabel->setText(tr("Peg index now - %1: ")
                             .arg((interval_num +1)*peg_interval-1));
    pegNextTextLabel->setText(tr("%1 - %2: ")
                              .arg((interval_num +1)*peg_interval)
                              .arg((interval_num +2)*peg_interval-1));
    pegNextNextTextLabel->setText(tr("%1 - %2: ")
                                  .arg((interval_num +2)*peg_interval)
                                  .arg((interval_num +3)*peg_interval-1));
    if (last_block_num < nPegStartHeight) {
        pegNowLabel->setText("off");
        pegNextLabel->setText("off");
        pegNextNextLabel->setText("off");
        inflateLabel->setText("off");
        deflateLabel->setText("off");
        nochangeLabel->setText("off");
    } else {
        pegNowLabel->setText(QString::number(peg_supply));
        pegNextLabel->setText(QString::number(peg_next_supply));
        pegNextNextLabel->setText(QString::number(peg_next_next_supply));
        inflateLabel->setText(tr("%1").arg(votes_inflate));
        deflateLabel->setText(tr("%1").arg(votes_deflate));
        nochangeLabel->setText(tr("%1").arg(votes_nochange));
    }
    double liquid = 100.0;
    for(int i=0; i< peg_supply; i++) {
        double f = liquid/100.0;
        liquid -= f;
    }
    liquidMeter->setValue(liquid);
}

QString BitcoinGUI::timeBehindText(int secs) 
{
    QString time_behind_text;
    const int MINUTE_IN_SECONDS = 60;
    const int HOUR_IN_SECONDS = 60*60;
    const int DAY_IN_SECONDS = 24*60*60;
    const int WEEK_IN_SECONDS = 7*24*60*60;
    const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar
   
    if(secs < 2*MINUTE_IN_SECONDS) 
    {
        time_behind_text = tr("%n second(s)","",secs);
    }
    else if(secs < 2*HOUR_IN_SECONDS) 
    {
        time_behind_text = tr("%n minute(s)","",secs/MINUTE_IN_SECONDS);
    }
    else if(secs < 2*DAY_IN_SECONDS)
    {
        time_behind_text = tr("%n hour(s)","",secs/HOUR_IN_SECONDS);
    }
    else if(secs < 2*WEEK_IN_SECONDS)
    {
        time_behind_text = tr("%n day(s)","",secs/DAY_IN_SECONDS);
    }
    else if(secs < YEAR_IN_SECONDS)
    {
        time_behind_text = tr("%n week(s)","",secs/WEEK_IN_SECONDS);
    }
    else
    {
        int years = secs / YEAR_IN_SECONDS;
        int remainder = secs % YEAR_IN_SECONDS;
        time_behind_text = tr("%1 and %2").arg(tr("%n year(s)", "", years)).arg(tr("%n week(s)","", remainder/WEEK_IN_SECONDS));
    }
    return time_behind_text;
}

void BitcoinGUI::message(const QString &title, const QString &message, bool modal, unsigned int style)
{
    QString strTitle = tr("BitBay") + " - ";
    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    // Check for usage of predefined title
    switch (style) {
    case CClientUIInterface::MSG_ERROR:
        strTitle += tr("Error");
        break;
    case CClientUIInterface::MSG_WARNING:
        strTitle += tr("Warning");
        break;
    case CClientUIInterface::MSG_INFORMATION:
        strTitle += tr("Information");
        break;
    default:
        strTitle += title; // Use supplied title
    }

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (modal) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons);
        mBox.exec();
    }
    else
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}

void BitcoinGUI::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    if (firstTimeRequest) {
        firstTimeRequest = false;
        ratesRequestInitiate();
        releaseRequestInitiate();
    }
}

void BitcoinGUI::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
#ifndef Q_OS_MAC // Ignored on Mac
    if(e->type() == QEvent::WindowStateChange)
    {
        if(clientModel && clientModel->getOptionsModel()->getMinimizeToTray())
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void BitcoinGUI::closeEvent(QCloseEvent *event)
{
    if(clientModel)
    {
#ifndef Q_OS_MAC // Ignored on Mac
        if(!clientModel->getOptionsModel()->getMinimizeToTray() &&
           !clientModel->getOptionsModel()->getMinimizeOnClose())
        {
            qApp->quit();
        }
#endif
    }
    QMainWindow::closeEvent(event);
}

void BitcoinGUI::askFee(qint64 nFeeRequired, bool *payFee)
{
    if (!clientModel || !clientModel->getOptionsModel())
        return;

    QString strMessage = tr("This transaction is over the size limit. You can still send it for a fee of %1, "
        "which goes to the nodes that process your transaction and helps to support the network. "
        "Do you want to pay the fee?").arg(BitcoinUnits::formatWithUnit(clientModel->getOptionsModel()->getDisplayUnit(), nFeeRequired));
    QMessageBox::StandardButton retval = QMessageBox::question(
          this, tr("Confirm transaction fee"), strMessage,
          QMessageBox::Yes|QMessageBox::Cancel, QMessageBox::Yes);
    *payFee = (retval == QMessageBox::Yes);
}

void BitcoinGUI::incomingTransaction(const QModelIndex & parent, int start, int end)
{
    if(!walletModel || !clientModel)
        return;
    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent)
                    .data(Qt::EditRole).toULongLong();
    if(!clientModel->inInitialBlockDownload())
    {
        // On new transaction, make an info balloon
        // Unless the initial block download is in progress, to prevent balloon-spam
        QString date = ttm->index(start, TransactionTableModel::Date, parent)
                        .data().toString();
        QString type = ttm->index(start, TransactionTableModel::Type, parent)
                        .data().toString();
        QString address = ttm->index(start, TransactionTableModel::ToAddress, parent)
                        .data().toString();
        QIcon icon = qvariant_cast<QIcon>(ttm->index(start,
                            TransactionTableModel::ToAddress, parent)
                        .data(Qt::DecorationRole));

        notificator->notify(Notificator::Information,
                            (amount)<0 ? tr("Sent transaction") :
                                         tr("Incoming transaction"),
                              tr("Date: %1\n"
                                 "Amount: %2\n"
                                 "Type: %3\n"
                                 "Address: %4\n")
                              .arg(date)
                              .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), amount, true))
                              .arg(type)
                              .arg(address), icon);
    }
}

void BitcoinGUI::gotoOverviewPage()
{
    overviewAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(overviewPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoHistoryPage()
{
    historyAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(transactionsPage);
    if (!tabTransactions->isChecked()) tabTransactions->setChecked(true);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), transactionView, SLOT(exportClicked()));
}

void BitcoinGUI::gotoAddressBookPage()
{
    addressBookAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(addressBookPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), addressBookPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoReceiveCoinsPage()
{
    receiveCoinsAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(receiveCoinsPage);

    exportAction->setEnabled(true);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
    connect(exportAction, SIGNAL(triggered()), receiveCoinsPage, SLOT(exportClicked()));
}

void BitcoinGUI::gotoSendCoinsPage()
{
    sendCoinsAction->setChecked(true);
    centralStackedWidget->setCurrentWidget(sendCoinsPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoSignMessagePage()
{
    centralStackedWidget->setCurrentWidget(signMessagePage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoVerifyMessagePage()
{
    centralStackedWidget->setCurrentWidget(verifyMessagePage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoStakingPage()
{
    tabStaking->setChecked(true);
    centralStackedWidget->setCurrentWidget(stakingPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoDynamicPegPage()
{
    tabDynamicPeg->setChecked(true);
    centralStackedWidget->setCurrentWidget(dynamicPegPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoInfoPage()
{
    tabInfo->setChecked(true);
    centralStackedWidget->setCurrentWidget(infoPage);

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoInfoPageBlocks()
{
    tabInfo->setChecked(true);
    centralStackedWidget->setCurrentWidget(infoPage);
    infoPage->showChainPage();
    infoPage->jumpToTop();

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoInfoPageNet()
{
    tabInfo->setChecked(true);
    centralStackedWidget->setCurrentWidget(infoPage);
    infoPage->showNetPage();

    exportAction->setEnabled(false);
    disconnect(exportAction, SIGNAL(triggered()), 0, 0);
}

void BitcoinGUI::gotoSignMessageTab(QString addr)
{
    tabSign->setChecked(true);
    centralStackedWidget->setCurrentWidget(signMessagePage);

    if(!addr.isEmpty())
        signMessagePage->setAddress_SM(addr);
}

void BitcoinGUI::gotoVerifyMessageTab(QString addr)
{
    tabVerify->setChecked(true);
    centralStackedWidget->setCurrentWidget(verifyMessagePage);

    if(!addr.isEmpty())
        verifyMessagePage->setAddress_VM(addr);
}

void BitcoinGUI::onTabChanged(int)
{
    if (centralStackedWidget->currentWidget() == sendCoinsPage) {
        fAboutToSendGUI = true;
    } else {
        fAboutToSendGUI = false;
    }
}

void BitcoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void BitcoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        int nValidUrisFound = 0;
        QList<QUrl> uris = event->mimeData()->urls();
        foreach(const QUrl &uri, uris)
        {
            if (sendCoinsPage->handleURI(uri.toString()))
                nValidUrisFound++;
        }

        // if valid URIs were found
        if (nValidUrisFound)
            gotoSendCoinsPage();
        else
            notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid BitBay address or malformed URI parameters."));
    }

    event->acceptProposedAction();
}

void BitcoinGUI::handleURI(QString strURI)
{
    // URI has to be valid
    if (sendCoinsPage->handleURI(strURI))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
    }
    else
        notificator->notify(Notificator::Warning, tr("URI handling"), tr("URI can not be parsed! This can be caused by an invalid BitBay address or malformed URI parameters."));
}

void BitcoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>not encrypted</b>"));
        changePassphraseAction->setEnabled(false);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_open").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(false);
        lockWalletAction->setVisible(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelEncryptionIcon->setPixmap(QIcon(":/icons/lock_closed").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        changePassphraseAction->setEnabled(true);
        unlockWalletAction->setVisible(true);
        lockWalletAction->setVisible(false);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}

void BitcoinGUI::encryptWallet()
{
    if(!walletModel)
        return;

    AskPassphraseDialog dlg(AskPassphraseDialog::Encrypt, this);
    dlg.setModel(walletModel);
    dlg.exec();

    setEncryptionStatus(walletModel->getEncryptionStatus());
}

void BitcoinGUI::backupWallet()
{
    QString saveDir = QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
    QString filename = QFileDialog::getSaveFileName(this, tr("Backup Wallet"), saveDir, tr("Wallet Data (*.dat)"));
    if(!filename.isEmpty()) {
        if(!walletModel->backupWallet(filename)) {
            QMessageBox::warning(this, tr("Backup Failed"), tr("There was an error trying to save the wallet data to the new location."));
        }
    }
}

void BitcoinGUI::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::ChangePass, this);
    dlg.setModel(walletModel);
    dlg.exec();
}

void BitcoinGUI::unlockWallet()
{
    if(!walletModel)
        return;
    // Unlock wallet when requested by wallet model
    if(walletModel->getEncryptionStatus() == WalletModel::Locked)
    {
        AskPassphraseDialog::Mode mode = sender() == unlockWalletAction ?
              AskPassphraseDialog::UnlockStaking : AskPassphraseDialog::Unlock;
        AskPassphraseDialog dlg(mode, this);
        dlg.setModel(walletModel);
        dlg.exec();
    }
}

void BitcoinGUI::lockWallet()
{
    if(!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void BitcoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void BitcoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void BitcoinGUI::updateWeight()
{
    if (!pwalletMain)
        return;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain)
        return;

    TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
    if (!lockWallet)
        return;

    nWeight = pwalletMain->GetStakeWeight();
}

void BitcoinGUI::updateStakingIcon()
{
    updateWeight();

    if (nLastCoinStakeSearchInterval && nWeight)
    {
        uint64_t nWeight = this->nWeight;
        uint64_t nNetworkWeight = GetPoSKernelPS();
        unsigned nEstimateTime = GetTargetSpacing(nBestHeight) * nNetworkWeight / nWeight;

        QString text;
        if (nEstimateTime < 60)
        {
            text = tr("%n second(s)", "", nEstimateTime);
        }
        else if (nEstimateTime < 60*60)
        {
            text = tr("%n minute(s)", "", nEstimateTime/60);
        }
        else if (nEstimateTime < 24*60*60)
        {
            text = tr("%n hour(s)", "", nEstimateTime/(60*60));
        }
        else
        {
            text = tr("%n day(s)", "", nEstimateTime/(60*60*24));
        }

        nWeight /= COIN;
        nNetworkWeight /= COIN;

        labelStakingIcon->setPixmap(QIcon(":/icons/staking_on").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        labelStakingIcon->setToolTip(tr("Staking.<br>Your weight is %1<br>Network weight is %2<br>Expected time to earn reward is %3").arg(nWeight).arg(nNetworkWeight).arg(text));
    }
    else
    {
        labelStakingIcon->setPixmap(QIcon(":/icons/staking_off").pixmap(STATUSBAR_ICONSIZE,STATUSBAR_ICONSIZE));
        if (pwalletMain && pwalletMain->IsLocked())
            labelStakingIcon->setToolTip(tr("Not staking because wallet is locked"));
        else if (vNodes.empty())
            labelStakingIcon->setToolTip(tr("Not staking because wallet is offline"));
        else if (IsInitialBlockDownload())
            labelStakingIcon->setToolTip(tr("Not staking because wallet is syncing"));
        else if (!nWeight)
            labelStakingIcon->setToolTip(tr("Not staking because you don't have mature coins"));
        else
            labelStakingIcon->setToolTip(tr("Not staking"));
    }
}

void BitcoinGUI::detectShutdown()
{
    if (ShutdownRequested())
        QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
}

void BitcoinGUI::ratesRequestInitiate()
{
    QUrl ratedb_url = QUrl("https://bitbaymarket.github.io/ratedb/rates.json");
    QUrl ratedb_1k_url = QUrl("https://bitbaymarket.github.io/ratedb/rates1k.json");
    if (TestNet()) {
        ratedb_url = QUrl("https://bitbaymarket.github.io/ratedb-testnet/rates.json");
        ratedb_1k_url = QUrl("https://bitbaymarket.github.io/ratedb-testnet/rates1k.json");
    } 

    QNetworkRequest req_rates(ratedb_url);
    req_rates.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    netAccessManager->get(req_rates);
    
    QNetworkRequest req_rates_1k(ratedb_1k_url);
    req_rates_1k.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    netAccessManager->get(req_rates_1k);
    
    dynamicPegPage->setAlgorithmInfo("BitBay", ratedb_url.toString(), ratedb_1k_url.toString());
}

void BitcoinGUI::netDataReplyFinished(QNetworkReply *reply)
{
    // #NOTE18
    if (!reply) {
        return;
    }
    reply->deleteLater();
    if (reply->error()) {
        if (reply->url().path().endsWith("rates1k.json")) {
            dynamicPegPage->setStatusMessage("Algorithm Chart error, "+
                                             reply->errorString());
            dynamicPegPage->setAlgorithmVote("disabled", 0.2);
            if (walletModel) {
                walletModel->setTrackerVote(PEG_VOTE_NONE, 0.2);
            }
        }
        else if (reply->url().path().endsWith("rates.json")) {
            dynamicPegPage->setStatusMessage("Algorithm URL error, "+
                                             reply->errorString());
            dynamicPegPage->setAlgorithmVote("disabled", 0.2);
            if (walletModel) {
                walletModel->setTrackerVote(PEG_VOTE_NONE, 0.2);
            }
        }
        return;
    }
    QByteArray data = reply->readAll();
    auto json_doc = QJsonDocument::fromJson(data);
    
    if (reply->url().path().endsWith("rates1k.json")) {
        
        auto records = json_doc.array();
        
        bool have_rate = false;
        double btc_in_usd = 1.;
        double bay_in_usd = 1.;
        double usd_in_bay = 1.;
        deque<double> btc_rates;
        deque<double> bay_rates;
        
        QVector<double> xs_price;
        QVector<double> ys_price;
        QVector<double> xs_floor;
        QVector<double> ys_floor;
        QVector<double> xs_floormin;
        QVector<double> ys_floormin;
        QVector<double> xs_floormax;
        QVector<double> ys_floormax;
        uint64_t nTimeMin = 0;
        uint64_t nTimeMax = 0;
        
        for(int i=0; i<records.size(); i++) {
            auto record = records.at(i);
            if (!record.isObject()) continue;
            auto record_obj = record.toObject();
            auto record_bay = record_obj["BAY"];
            auto record_btc = record_obj["BTC"];
            if (!record_bay.isObject()) continue;
            if (!record_btc.isObject()) continue;
            auto record_btc_obj = record_btc.toObject();
            auto record_bay_obj = record_bay.toObject();
            auto record_day_price = record_btc_obj["last_updated"];
            auto record_btc_price = record_btc_obj["price"];
            auto record_bay_price = record_bay_obj["price"];
            auto record_bay_floor = record_bay_obj["floor"];
            if (!record_bay_price.isDouble()) continue;
            if (!record_btc_price.isDouble()) continue;
            bay_in_usd = record_bay_price.toDouble();
            if (bay_in_usd >0) {
                have_rate = true;
                usd_in_bay = 1. / bay_in_usd;
            }
            btc_in_usd = record_btc_price.toDouble();
            
            btc_rates.push_back(btc_in_usd);
            bay_rates.push_back(bay_in_usd);
            
            while (btc_rates.size() > 1000) {
                btc_rates.pop_front();
            }
            while (bay_rates.size() > 1000) {
                bay_rates.pop_front();
            }
            
            if (!record_bay_floor.isDouble()) continue;
            auto floor_in_usd = record_bay_floor.toDouble();
            auto dt = QDateTime::fromString(record_day_price.toString(), Qt::ISODate);
            xs_price.push_back(dt.toMSecsSinceEpoch());
            ys_price.push_back(bay_in_usd);
            xs_floor.push_back(dt.toMSecsSinceEpoch());
            ys_floor.push_back(floor_in_usd);
            xs_floormin.push_back(dt.toMSecsSinceEpoch());
            ys_floormin.push_back(floor_in_usd * 0.95);
            xs_floormax.push_back(dt.toMSecsSinceEpoch());
            ys_floormax.push_back(floor_in_usd * 1.05);
            
            uint64_t nTime = dt.toSecsSinceEpoch();
            if (nTimeMin == 0) nTimeMin = nTime;
            if (nTimeMax == 0) nTimeMax = nTime;
            if (nTime < nTimeMin) nTimeMin = nTime;
            if (nTime > nTimeMax) nTimeMax = nTime;
        }
        
        {
            LOCK(cs_main);
            
            if ((pindexBest->nTime > nTimeMax) && ((pindexBest->nTime - nTimeMax) > 12*3600)) {
                dynamicPegPage->setStatusMessage("Algorithm Chart is not live, last record of "+
                                                 QString::fromStdString(DateTimeStrFormat(nTimeMax)));
                dynamicPegPage->setAlgorithmVote("disabled", 0.2);
                if (walletModel) {
                    walletModel->setTrackerVote(PEG_VOTE_NONE, 0.2);
                }
                return;
            }

            dynamicPegPage->setStatusMessage("LIVE, last record of "+
                                             QString::fromStdString(DateTimeStrFormat(nTimeMax)));
            
            QVector<double> xs_peg;
            QVector<double> ys_peg;
            CBlockIndex * pindex = pindexBest;
            xs_peg.push_back(uint64_t(pindex->nTime)*1000.);
            ys_peg.push_back(pindex->nPegSupplyIndex);
            while(pindex && pindex->nTime > nTimeMin) {
                if ((pindex->nHeight % 200) == 0) {
                    xs_peg.push_back(uint64_t(pindex->nTime)*1000.);
                    ys_peg.push_back(pindex->nPegSupplyIndex);
                }
                pindex = pindex->pprev;
            }
            dynamicPegPage->curvePeg->setSamples(xs_peg, ys_peg);
        }

        dynamicPegPage->curvePrice->setSamples(xs_price, ys_price);
        dynamicPegPage->curveFloor->setSamples(xs_floor, ys_floor);
        dynamicPegPage->curveFloorMin->setSamples(xs_floormin, ys_floormin);
        dynamicPegPage->curveFloorMax->setSamples(xs_floormax, ys_floormax);
        dynamicPegPage->fplot->replot();
        
        if (have_rate) {
            oneBayRateLabel->setText(tr("1 BAY = %1 USD").arg(bay_in_usd));
            oneUsdRateLabel->setText(tr("1 USD = %1 BAY").arg(usd_in_bay));
            
            vector<double> v_btc_rates;
            vector<double> v_bay_rates;
            for(size_t i=0; i<btc_rates.size(); ++i) v_btc_rates.push_back(btc_rates[i]);
            for(size_t i=0; i<bay_rates.size(); ++i) v_bay_rates.push_back(bay_rates[i]);
            
            if (walletModel) {
                walletModel->setBtcRates(v_btc_rates);
                walletModel->setBayRates(v_bay_rates);
            } else {
                vFirstRetrievedBtcRates = v_btc_rates;
                vFirstRetrievedBayRates = v_bay_rates;
            }
        }
    }
    else if (reply->url().path().endsWith("rates.json")) {
        bool have_rate = false;
        double bay_in_usd = 1.;
        double usd_in_bay = 1.;
        double bay_floor_in_usd = 1.;
        
        auto record_obj = json_doc.object();
        auto record_bay = record_obj["BAY"];
        auto record_btc = record_obj["BTC"];
        if (!record_bay.isObject()) return;
        if (!record_btc.isObject()) return;
        auto record_btc_obj = record_btc.toObject();
        auto record_bay_obj = record_bay.toObject();
        auto record_day_price = record_btc_obj["last_updated"];
        auto record_btc_price = record_btc_obj["price"];
        auto record_bay_price = record_bay_obj["price"];
        auto record_bay_floor = record_bay_obj["floor"];
        if (!record_bay_price.isDouble()) return;
        if (!record_bay_floor.isDouble()) return;
        if (!record_btc_price.isDouble()) return;
        bay_floor_in_usd = record_bay_floor.toDouble();
        bay_in_usd = record_bay_price.toDouble();
        if (bay_in_usd >0) {
            have_rate = true;
            usd_in_bay = 1. / bay_in_usd;
        }
        
        if (have_rate) {
            oneBayRateLabel->setText(tr("1 BAY = %1 USD").arg(bay_in_usd));
            oneUsdRateLabel->setText(tr("1 USD = %1 BAY").arg(usd_in_bay));

            {
                LOCK(cs_main);
                auto dt = QDateTime::fromString(record_day_price.toString(), Qt::ISODate);
                uint64_t nTime = dt.toSecsSinceEpoch();
                
                if ((pindexBest->nTime > nTime) && ((pindexBest->nTime - nTime) > 12*3600)) {
                    dynamicPegPage->setStatusMessage("Algorithm URL is not live, last record of "+
                                                     QString::fromStdString(DateTimeStrFormat(nTime)));
                    dynamicPegPage->setAlgorithmVote("disabled", 0.2);
                    if (walletModel) {
                        walletModel->setTrackerVote(PEG_VOTE_NONE, 0.2);
                    }
                    return;
                }
            }
            
            auto record_bay_vote = record_bay_obj["pegvote"];
            if (!record_bay_vote.isString()) {
                dynamicPegPage->setStatusMessage("Algorithm URL record has no pegvote");
                dynamicPegPage->setAlgorithmVote("disabled", 0.2);
                if (walletModel) {
                    walletModel->setTrackerVote(PEG_VOTE_NONE, 0.2);
                }
                return;
            }
            
            QString pegvote = record_bay_vote.toString();
            dynamicPegPage->setAlgorithmVote(pegvote, bay_floor_in_usd);
            if (walletModel) {
                if (pegvote == "inflate") {
                    walletModel->setTrackerVote(PEG_VOTE_INFLATE, bay_floor_in_usd);
                } else if (pegvote == "deflate") {
                    walletModel->setTrackerVote(PEG_VOTE_DEFLATE, bay_floor_in_usd);
                } else if (pegvote == "nochange") {
                    walletModel->setTrackerVote(PEG_VOTE_NOCHANGE, bay_floor_in_usd);
                } else {
                    walletModel->setTrackerVote(PEG_VOTE_NONE, bay_floor_in_usd);
                }
            }
        } 
        else {
            dynamicPegPage->setStatusMessage("Algorithm URL record has no rate");
            dynamicPegPage->setAlgorithmVote("disabled", 0.2);
            if (walletModel) {
                walletModel->setTrackerVote(PEG_VOTE_NONE, 0.2);
            }
            return;
        }
    }
    else if (reply->url().path().endsWith("release.json")) {
        auto release_obj = json_doc.object();
        auto record_ver_maj = release_obj["version_major"];
        auto record_ver_min = release_obj["version_minor"];
        auto record_ver_rev = release_obj["version_revision"];
        if (!record_ver_maj.isDouble()) return;
        if (!record_ver_min.isDouble()) return;
        if (!record_ver_rev.isDouble()) return;
        int ver_maj = record_ver_maj.toInt();
        int ver_min = record_ver_min.toInt();
        int ver_rev = record_ver_rev.toInt();
        if (ver_maj < CLIENT_VERSION_MAJOR) return;
        if (ver_maj == CLIENT_VERSION_MAJOR && 
            ver_min < CLIENT_VERSION_MINOR) return;
        if (ver_maj == CLIENT_VERSION_MAJOR && 
            ver_min == CLIENT_VERSION_MINOR && 
            ver_rev <=CLIENT_VERSION_REVISION) return;
        if (!walletModel) {
            return;
        }
        QMessageBox::warning(this, 
                             tr("New Version Warning"), 
                             tr("New Version of BitBay wallet is available %1.%2.%3.\n"
                                "Current running has version %4.%5.%6.")
                             .arg(ver_maj)
                             .arg(ver_min)
                             .arg(ver_rev)
                             .arg(CLIENT_VERSION_MAJOR)
                             .arg(CLIENT_VERSION_MINOR)
                             .arg(CLIENT_VERSION_REVISION), 
                             QMessageBox::Ok);
    }
}

void BitcoinGUI::releaseRequestInitiate()
{
    QUrl release_url = QUrl("https://bitbaymarket.github.io/bitbay-core-peg/release.json");
    if (TestNet()) {
        // todo
        return;
    } 

    QNetworkRequest req_release(release_url);
    req_release.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    netAccessManager->get(req_release);
}

