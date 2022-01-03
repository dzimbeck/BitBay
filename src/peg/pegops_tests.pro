
TEMPLATE = app
QT += testlib

CONFIG += console
CONFIG -= app_bundle
CONFIG += no_include_pwd
CONFIG += thread
CONFIG += c++11

INCLUDEPATH += $$PWD/.. $$PWD

include($$PWD/pegops.pri)

HEADERS += \
    $$PWD/tests/pegops_tests.h \

SOURCES += \
    $$PWD/tests/pegops_tests.cpp \
    $$PWD/tests/pegops_test5.cpp \
    $$PWD/tests/pegops_test6.cpp \
    $$PWD/tests/pegops_test7.cpp \
    $$PWD/tests/pegops_test8.cpp \
    $$PWD/tests/pegops_test1k.cpp \
    $$PWD/tests/pegops_withdraws.cpp \

LIBS += -lz
LIBS += -lboost_system
LIBS += -lboost_thread
LIBS += -lssl -lcrypto 

DEFINES += BOOST_ALL_NO_LIB

exists(pegops_tests-local.pri) {
    include(pegops_tests-local.pri)
}

