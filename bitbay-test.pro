TEMPLATE = app
TARGET = bitbay-test
VERSION = 3.0.0

count(USE_WALLET, 0) {
    USE_WALLET=1
}
contains(USE_WALLET, 1) {
    message(Building with WALLET support)
    CONFIG += wallet
}

count(USE_TESTNET, 1) {
    contains(USE_TESTNET, 1) {
        message(Building with TESTNET enabled)
        DEFINES += USE_TESTNET
    }
}

count(USE_FAUCET, 1) {
    contains(USE_FAUCET, 1) {
        message(Building with FAUCET support)
        CONFIG += faucet
    }
}

count(USE_EXCHANGE, 1) {
    contains(USE_EXCHANGE, 1) {
        message(Building with EXCHANGE support)
        CONFIG += exchange
    }
}

count(USE_EXPLORER, 1) {
    contains(USE_EXPLORER, 1) {
        message(Building with USE_EXPLORER support)
        CONFIG += explorer
    }
}

exists(bitbayd-local.pri) {
    include(bitbayd-local.pri)
}

CONFIG -= qt
INCLUDEPATH += build

# mac builds
include(bitbay-mac.pri)

INCLUDEPATH += src src/json src/qt $$PWD
DEFINES += BOOST_THREAD_USE_LIB
DEFINES += BOOST_SPIRIT_THREADSAFE
DEFINES += BOOST_NO_CXX11_SCOPED_ENUMS
CONFIG += console
CONFIG -= app_bundle
CONFIG += no_include_pwd
CONFIG += thread
CONFIG += c++11

greaterThan(QT_MAJOR_VERSION, 4) {
    DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0
}

# for boost 1.37, add -mt to the boost libraries
# use: qmake BOOST_LIB_SUFFIX=-mt
# for boost thread win32 with _win32 sufix
# use: BOOST_THREAD_LIB_SUFFIX=_win32-...
# or when linking against a specific BerkelyDB version: BDB_LIB_SUFFIX=-4.8

# Dependency library locations can be customized with:
#    BOOST_INCLUDE_PATH, BOOST_LIB_PATH, BDB_INCLUDE_PATH,
#    BDB_LIB_PATH, OPENSSL_INCLUDE_PATH and OPENSSL_LIB_PATH respectively

OBJECTS_DIR = build
MOC_DIR = build
UI_DIR = build

!win32 {
	# for extra security against potential buffer overflows: enable GCCs Stack Smashing Protection
	QMAKE_CXXFLAGS *= -fstack-protector-all --param ssp-buffer-size=1
	QMAKE_LFLAGS *= -fstack-protector-all --param ssp-buffer-size=1
	# We need to exclude this for Windows cross compile with MinGW 4.2.x, as it will result in a non-working executable!
	# This can be enabled for Windows, when we switch to MinGW >= 4.4.x.
}
# for extra security on Windows: enable ASLR and DEP via GCC linker flags
#win32:QMAKE_LFLAGS *= -Wl,--dynamicbase -Wl,--nxcompat
#win32:QMAKE_LFLAGS += -static-libgcc -static-libstdc++

# use: qmake "USE_UPNP=1" ( enabled by default; default)
#  or: qmake "USE_UPNP=0" (disabled by default)
#  or: qmake "USE_UPNP=-" (not supported)
# miniupnpc (http://miniupnp.free.fr/files/) must be installed for support
contains(USE_UPNP, -) {
    message(Building without UPNP support)
} else {
    message(Building with UPNP support)
    count(USE_UPNP, 0) {
        USE_UPNP=1
    }
    DEFINES += USE_UPNP=$$USE_UPNP MINIUPNP_STATICLIB STATICLIB
    INCLUDEPATH += $$MINIUPNPC_INCLUDE_PATH
    LIBS += $$join(MINIUPNPC_LIB_PATH,,-L,) -lminiupnpc
    win32:LIBS += -liphlpapi
}

INCLUDEPATH += src/leveldb/include src/leveldb/helpers
LIBS += $$PWD/src/leveldb/out-static/libleveldb.a $$PWD/src/leveldb/out-static/libmemenv.a
HEADERS += src/txdb-leveldb.h
SOURCES += src/txdb-leveldb.cpp
!win32 {
    # we use QMAKE_CXXFLAGS_RELEASE even without RELEASE=1 because we use RELEASE to indicate linking preferences not -O preferences
    macx:LEVELDB_CXXFLAGS=-mmacosx-version-min=10.9
    genleveldb.commands = cd $$PWD/src/leveldb && CC=$$QMAKE_CC CXX=$$QMAKE_CXX $(MAKE) OPT=\"$$QMAKE_CXXFLAGS $$LEVELDB_CXXFLAGS $$QMAKE_CXXFLAGS_RELEASE\" out-static/libleveldb.a out-static/libmemenv.a
} else {
    # make an educated guess about what the ranlib command is called
    isEmpty(QMAKE_RANLIB) {
    #	QMAKE_RANLIB = $$replace(QMAKE_STRIP, strip, ranlib)
        QMAKE_RANLIB = echo
    }
    LIBS += -lshlwapi
    genleveldb.commands = cd $$PWD/src/leveldb && CC=$$QMAKE_CC CXX=$$QMAKE_CXX TARGET_OS=OS_WINDOWS_CROSSCOMPILE $(MAKE) OPT=\"$$QMAKE_CXXFLAGS $$QMAKE_CXXFLAGS_RELEASE\" out-static/libleveldb.a out-static/libmemenv.a && $$QMAKE_RANLIB $$PWD/src/leveldb/out-static/libleveldb.a && $$QMAKE_RANLIB $$PWD/src/leveldb/out-static/libmemenv.a
}
genleveldb.target = $$PWD/src/leveldb/out-static/libleveldb.a
genleveldb.depends = FORCE
PRE_TARGETDEPS += $$PWD/src/leveldb/out-static/libleveldb.a
QMAKE_EXTRA_TARGETS += genleveldb
# Gross ugly hack that depends on qmake internals, unfortunately there is no other way to do it.
QMAKE_CLEAN += $$PWD/src/leveldb/out-static/libleveldb.a; cd $$PWD/src/leveldb ; $(MAKE) clean

QMAKE_CXXFLAGS_WARN_ON = -fdiagnostics-show-option -Wall -Wextra -Wno-ignored-qualifiers -Wformat -Wformat-security -Wno-unused-parameter -Wstack-protector

#json lib
include(src/json/json.pri)

#core
include(src/core.pri)

SOURCES += \
	src/test/test_bitcoin.cpp \
	\
	src/test/allocator_tests.cpp \
	src/test/base32_tests.cpp \
	src/test/base64_tests.cpp \
	src/test/bignum_tests.cpp \
	src/test/getarg_tests.cpp \
	src/test/hmac_tests.cpp \
	src/test/mruset_tests.cpp \
	src/test/netbase_tests.cpp \
	src/test/serialize_tests.cpp \
	src/test/sigopcount_tests.cpp \
	src/test/uint160_tests.cpp \
	src/test/uint256_tests.cpp \

# disabled tests
#SOURCES += \
#	src/test/accounting_tests.cpp \
#	src/test/bip32_tests.cpp \
#	src/test/base58_tests.cpp \
#	src/test/Checkpoints_tests.cpp \
#	src/test/key_tests.cpp \
#	src/test/script_tests.cpp \
#	src/test/wallet_tests.cpp \


CODECFORTR = UTF-8

# platform specific defaults, if not overridden on command line
isEmpty(BOOST_LIB_SUFFIX) {
    macx:BOOST_LIB_SUFFIX = -mt
    windows:BOOST_LIB_SUFFIX = -mt
}

isEmpty(BOOST_THREAD_LIB_SUFFIX) {
    win32:BOOST_THREAD_LIB_SUFFIX = $$BOOST_LIB_SUFFIX
    else:BOOST_THREAD_LIB_SUFFIX = $$BOOST_LIB_SUFFIX
}

windows:DEFINES += WIN32
windows:RC_FILE = src/qt/res/bitcoin-qt.rc

# Set libraries and includes at end, to use platform-defined defaults if not overridden
INCLUDEPATH += $$BDB_INCLUDE_PATH 
INCLUDEPATH += $$BOOST_INCLUDE_PATH 
INCLUDEPATH += $$OPENSSL_INCLUDE_PATH

LIBS += $$join(BDB_LIB_PATH,,-L,) 
LIBS += $$join(BOOST_LIB_PATH,,-L,) 
LIBS += $$join(OPENSSL_LIB_PATH,,-L,)
LIBS += -lssl -lcrypto 
LIBS += -ldb$$BDB_LIB_SUFFIX 
LIBS += -ldb_cxx$$BDB_LIB_SUFFIX
LIBS += -lz

# -lgdi32 has to happen after -lcrypto (see  #681)
windows:LIBS += -lws2_32 -lshlwapi -lmswsock -lole32 -loleaut32 -luuid -lgdi32

LIBS += -lboost_system$$BOOST_LIB_SUFFIX 
LIBS += -lboost_filesystem$$BOOST_LIB_SUFFIX 
LIBS += -lboost_program_options$$BOOST_LIB_SUFFIX 
LIBS += -lboost_thread$$BOOST_THREAD_LIB_SUFFIX
LIBS += -lboost_chrono$$BOOST_LIB_SUFFIX
LIBS += -lboost_unit_test_framework$$BOOST_LIB_SUFFIX

!contains(LIBS, -static) {
    DEFINES += BOOST_TEST_DYN_LINK
}

DISTFILES += \
    src/makefile.osx \
    src/makefile.unix \
    .travis.yml \
    .appveyor.yml

