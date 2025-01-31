
INCLUDEPATH += $$PWD

include($$PWD/pegops.pri)

HEADERS += $$PWD/peg.h
SOURCES += $$PWD/peg.cpp
SOURCES += $$PWD/peg_bridge.cpp
HEADERS += $$PWD/pegdb-leveldb.h
SOURCES += $$PWD/pegdb-leveldb.cpp
