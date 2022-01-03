
DEPENDPATH += $$PWD

HEADERS += \
    $$PWD/bitcoingui.h \
    $$PWD/transactiontablemodel.h \
    $$PWD/addresstablemodel.h \
    $$PWD/optionsdialog.h \
    $$PWD/coincontroldialog.h \
    $$PWD/coincontroltreewidget.h \
    $$PWD/sendcoinsdialog.h \
    $$PWD/addressbookpage.h \
    $$PWD/signmessagepage.h \
    $$PWD/verifymessagepage.h \
    $$PWD/stakingpage.h \
    $$PWD/dynamicpegpage.h \
    $$PWD/blockchainpage.h \
    $$PWD/txdetailswidget.h \
    $$PWD/itemdelegates.h \
    $$PWD/aboutdialog.h \
    $$PWD/editaddressdialog.h \
    $$PWD/bitcoinaddressvalidator.h \
    $$PWD/clientmodel.h \
    $$PWD/blockchainmodel.h \
    $$PWD/guiutil.h \
    $$PWD/transactionrecord.h \
    $$PWD/guiconstants.h \
    $$PWD/optionsmodel.h \
    $$PWD/monitoreddatamapper.h \
    $$PWD/trafficgraphwidget.h \
    $$PWD/transactiondesc.h \
    $$PWD/transactiondescdialog.h \
    $$PWD/bitcoinamountfield.h \
    $$PWD/transactionfilterproxy.h \
    $$PWD/transactionview.h \
    $$PWD/walletmodel.h \
    $$PWD/overviewpage.h \
    $$PWD/csvmodelwriter.h \
    $$PWD/sendcoinsentry.h \
    $$PWD/qvalidatedlineedit.h \
    $$PWD/bitcoinunits.h \
    $$PWD/qvaluecombobox.h \
    $$PWD/askpassphrasedialog.h \
    $$PWD/notificator.h \
    $$PWD/paymentserver.h \
    $$PWD/rpcconsole.h \
    $$PWD/metatypes.h \

SOURCES += \
    $$PWD/bitcoin.cpp \
    $$PWD/bitcoingui.cpp \
    $$PWD/transactiontablemodel.cpp \
    $$PWD/addresstablemodel.cpp \
    $$PWD/optionsdialog.cpp \
    $$PWD/sendcoinsdialog.cpp \
    $$PWD/coincontroldialog.cpp \
    $$PWD/coincontroltreewidget.cpp \
    $$PWD/addressbookpage.cpp \
    $$PWD/signmessagepage.cpp \
    $$PWD/verifymessagepage.cpp \
    $$PWD/stakingpage.cpp \
    $$PWD/dynamicpegpage.cpp \
    $$PWD/blockchainpage.cpp \
    $$PWD/txdetailswidget.cpp \
    $$PWD/itemdelegates.cpp \
    $$PWD/aboutdialog.cpp \
    $$PWD/editaddressdialog.cpp \
    $$PWD/bitcoinaddressvalidator.cpp \
    $$PWD/clientmodel.cpp \
    $$PWD/blockchainmodel.cpp \
    $$PWD/guiutil.cpp \
    $$PWD/transactionrecord.cpp \
    $$PWD/optionsmodel.cpp \
    $$PWD/monitoreddatamapper.cpp \
    $$PWD/trafficgraphwidget.cpp \
    $$PWD/transactiondesc.cpp \
    $$PWD/transactiondescdialog.cpp \
    $$PWD/bitcoinstrings.cpp \
    $$PWD/bitcoinamountfield.cpp \
    $$PWD/transactionfilterproxy.cpp \
    $$PWD/transactionview.cpp \
    $$PWD/walletmodel.cpp \
    $$PWD/overviewpage.cpp \
    $$PWD/csvmodelwriter.cpp \
    $$PWD/sendcoinsentry.cpp \
    $$PWD/qvalidatedlineedit.cpp \
    $$PWD/bitcoinunits.cpp \
    $$PWD/qvaluecombobox.cpp \
    $$PWD/askpassphrasedialog.cpp \
    $$PWD/notificator.cpp \
    $$PWD/paymentserver.cpp \
    $$PWD/rpcconsole.cpp \

RESOURCES += \
    $$PWD/bitbay.qrc \
    $$PWD/bitbayfonts.qrc \

FORMS += \
    $$PWD/forms/coincontroldialog.ui \
    $$PWD/forms/sendcoinsdialog.ui \
    $$PWD/forms/addressbookpage.ui \
    $$PWD/forms/signmessagepage.ui \
    $$PWD/forms/verifymessagepage.ui \
    $$PWD/forms/stakingpage.ui \
    $$PWD/forms/dynamicpegpage.ui \
    $$PWD/forms/blockchainpage.ui \
    $$PWD/forms/txdetails.ui \
    $$PWD/forms/aboutdialog.ui \
    $$PWD/forms/editaddressdialog.ui \
    $$PWD/forms/transactiondescdialog.ui \
    $$PWD/forms/overviewpage.ui \
    $$PWD/forms/sendcoinsentry.ui \
    $$PWD/forms/askpassphrasedialog.ui \
    $$PWD/forms/rpcconsole.ui \
    $$PWD/forms/optionsdialog.ui \
    $$PWD/forms/fractionsdialog.ui \
    $$PWD/forms/frozeninfodialog.ui \
    $$PWD/forms/pegvotesdialog.ui \

contains(USE_QRCODE, 1) {
    HEADERS += $$PWD/qrcodedialog.h
    SOURCES += $$PWD/qrcodedialog.cpp
    FORMS   += $$PWD/forms/qrcodedialog.ui
}

# for lrelease/lupdate
# also add new translations to src/qt/bitcoin.qrc under translations/
TRANSLATIONS = $$files($$PWD/locale/bitcoin_*.ts)

macx {
    HEADERS += \
        src/qt/macdockiconhandler.h \
        src/qt/macnotificationhandler.h \
    
    OBJECTIVE_SOURCES += \
        src/qt/macdockiconhandler.mm \
        src/qt/macnotificationhandler.mm \
    
    ICON = $$PWD/res/icons/wallet.icns
}

