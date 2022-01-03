
macx {

qt {
    TARGET = "BitBay-Wallet-Qt"
    QMAKE_INFO_PLIST = share/qt/Info.plist
}

LEVELDB_CXXFLAGS = -std=gnu++11 -isysroot $$QMAKE_MAC_SDK_PATH -Wno-unused-parameter -mmacosx-version-min=10.9

# mac: default path to brew packages
INCLUDEPATH += /usr/local/opt/boost/include
INCLUDEPATH += /usr/local/opt/openssl/include
INCLUDEPATH += /usr/local/opt/miniupnpc/include
INCLUDEPATH += /usr/local/opt/qrencode/include

LIBS += -L/usr/local/opt/boost/lib
LIBS += -L/usr/local/opt/openssl/lib
LIBS += -L/usr/local/opt/miniupnpc/lib
LIBS += -L/usr/local/opt/qrencode/lib

#QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.9
#QMAKE_MAC_SDK = macosx10.13

LIBS += -framework Foundation -framework ApplicationServices -framework AppKit
DEFINES += MAC_OSX MSG_NOSIGNAL=0

isEmpty(BDB_LIB_SUFFIX) {
    BDB_LIB_SUFFIX = -4.8
}

isEmpty(BDB_LIB_PATH) {
    BDB_LIB_PATH = $$PWD/db/build_unix/inst/lib
}

isEmpty(BDB_INCLUDE_PATH) {
    BDB_INCLUDE_PATH = $$PWD/db/build_unix/inst/include
}

isEmpty(BOOST_LIB_PATH) {
    BOOST_LIB_PATH = /usr/local/opt/boost/lib
}

isEmpty(BOOST_INCLUDE_PATH) {
    BOOST_INCLUDE_PATH = /usr/local/opt/boost/include
}

isEmpty(BOOST_LIB_SUFFIX) {
    macx:BOOST_LIB_SUFFIX = -mt
}

}
