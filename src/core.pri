
DEPENDPATH += $$PWD $$PWD/rpc $$PWD/wallet

HEADERS += \
    $$PWD/alert.h \
    $$PWD/addrman.h \
    $$PWD/base58.h \
    $$PWD/bignum.h \
    $$PWD/chainparams.h \
    $$PWD/chainparamsseeds.h \
    $$PWD/checkpoints.h \
    $$PWD/compat.h \
    $$PWD/coincontrol.h \
    $$PWD/sync.h \
    $$PWD/util.h \
    $$PWD/utilstrencodings.h \
    $$PWD/hash.h \
    $$PWD/uint256.h \
    $$PWD/kernel.h \
    $$PWD/serialize.h \
    $$PWD/core.h \
    $$PWD/main.h \
    $$PWD/net.h \
    $$PWD/key.h \
    $$PWD/txdb.h \
    $$PWD/txmempool.h \
    $$PWD/script.h \
    $$PWD/init.h \
    $$PWD/mruset.h \
    $$PWD/keystore.h \
    $$PWD/timedata.h \
    $$PWD/crypter.h \
    $$PWD/protocol.h \
    $$PWD/allocators.h \
    $$PWD/ui_interface.h \
    $$PWD/version.h \
    $$PWD/netbase.h \
    $$PWD/clientversion.h \
    $$PWD/threadsafety.h \
    $$PWD/tinyformat.h \
    $$PWD/blockindexmap.h \

SOURCES += \
    $$PWD/alert.cpp \
    $$PWD/chainparams.cpp \
    $$PWD/version.cpp \
    $$PWD/sync.cpp \
    $$PWD/txmempool.cpp \
    $$PWD/util.cpp \
    $$PWD/utilstrencodings.cpp \
    $$PWD/hash.cpp \
    $$PWD/netbase.cpp \
    $$PWD/key.cpp \
    $$PWD/script.cpp \
    $$PWD/core.cpp \
    $$PWD/main.cpp \
    $$PWD/net.cpp \
    $$PWD/checkpoints.cpp \
    $$PWD/addrman.cpp \
    $$PWD/keystore.cpp \
    $$PWD/timedata.cpp \
    $$PWD/crypter.cpp \
    $$PWD/protocol.cpp \
    $$PWD/noui.cpp \
    $$PWD/kernel.cpp \
    $$PWD/blockindexmap.cpp \

HEADERS += \
    $$PWD/crypto/pbkdf2.h \
    $$PWD/crypto/scrypt.h \

SOURCES += \
    $$PWD/crypto/pbkdf2.cpp \
    $$PWD/crypto/scrypt.cpp \

INCLUDEPATH += $$PWD/rpc

HEADERS += \
    $$PWD/rpc/rpcclient.h \
    $$PWD/rpc/rpcprotocol.h \
    $$PWD/rpc/rpcserver.h \

SOURCES += \
    $$PWD/rpc/rpcclient.cpp \
    $$PWD/rpc/rpcprotocol.cpp \
    $$PWD/rpc/rpcserver.cpp \
    $$PWD/rpc/rpcmisc.cpp \
    $$PWD/rpc/rpcnet.cpp \
    $$PWD/rpc/rpcblockchain.cpp \
    $$PWD/rpc/rpcrawtransaction.cpp \

INCLUDEPATH += $$PWD/wallet

explorer {
    DEFINES += ENABLE_EXPLORER
}

wallet {

    DEFINES += ENABLE_WALLET
    
    faucet {
        DEFINES += ENABLE_FAUCET
        SOURCES += \
            $$PWD/exchange/rpcfaucet.cpp \
    }
    exchange {
        DEFINES += ENABLE_EXCHANGE
        SOURCES += \
            $$PWD/exchange/rpcdeposit.cpp \
            $$PWD/exchange/rpcexchange.cpp \
            $$PWD/exchange/rpcwithdraw.cpp \
            $$PWD/exchange/rpcexchange_util.cpp \
    }

    HEADERS += \
        $$PWD/wallet/db.h \
        $$PWD/wallet/miner.h \
        $$PWD/wallet/wallet.h \
        $$PWD/wallet/walletdb.h \
    
    SOURCES += \
        $$PWD/wallet/db.cpp \
        $$PWD/wallet/miner.cpp \
        $$PWD/wallet/rpcdump.cpp \
        $$PWD/wallet/rpcmining.cpp \
        $$PWD/wallet/rpcwallet.cpp \
        $$PWD/wallet/wallet.cpp \
        $$PWD/wallet/walletdb.cpp \

}

# peg system
include($$PWD/peg/peg.pri)

LIBS += -lz
