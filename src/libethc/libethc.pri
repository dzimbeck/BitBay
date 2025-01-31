
DEPENDPATH += $$PWD
INCLUDEPATH += $$PWD/include

INCLUDEPATH += $$PWD/mini-gmp
HEADERS += \
    $$PWD/mini-gmp/*.h \

SOURCES += \
    $$PWD/mini-gmp/*.c \

DEFINES += XKCP_has_Sponge_Keccak
DEFINES += XKCP_has_FIPS202
DEFINES += XKCP_has_KeccakP1600

INCLUDEPATH += $$PWD/libkeccak
HEADERS += \
    $$PWD/libkeccak/*.h \

SOURCES += \
    $$PWD/libkeccak/*.c \

HEADERS += \
    $$PWD/include/ethc/*.h \

DEFINES += ENABLE_MODULE_RECOVERY

INCLUDEPATH += $$PWD/secp256k1/include
HEADERS += \
    $$PWD/secp256k1/include/*.h \

SOURCES += \
    $$PWD/secp256k1/src/*.c \

SOURCES += \
    $$PWD/src/abi.c \
	$$PWD/src/address.c \
	$$PWD/src/account.c \
	$$PWD/src/ecdsa.c \
	$$PWD/src/hex.c \
	$$PWD/src/rlp.c \
	$$PWD/src/internals.c \
    $$PWD/src/keccak256.c \
