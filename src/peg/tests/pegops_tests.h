// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITBAY_PEGOPS_TESTS_H
#define BITBAY_PEGOPS_TESTS_H

#include <QObject>

class TestPegOps: public QObject {
    Q_OBJECT
public:
    TestPegOps(QObject * p = nullptr);
private slots:
    void test1();
    void test2();
    void test3();
    void test4();
    void test4a();
    void test5();
    void test6();
    void test7();
    void test8();
    void test1k();
    void test1w();
};

#endif // BITBAY_PEGOPS_TESTS_H
