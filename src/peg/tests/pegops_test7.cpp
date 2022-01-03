// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include "pegops.h"
#include "pegdata.h"
#include "pegops_tests.h"

#include <string>

using namespace std;
using namespace pegops;

void TestPegOps::test7()
{
    int buffer = 3;
    
    CFractions user1(0,CFractions::STD);
    for(int i=0;i<PEG_SIZE;i++) {
        user1.f[i] = 23;
    }
    
    CFractions user2(0,CFractions::STD);
    for(int i=100;i<PEG_SIZE-100;i++) {
        user2.f[i] = 79;
    }
    
    CFractions exchange1 = user1 + user2;
    
    CFractions pegshift1(0,CFractions::STD);
    for(int i=0;i<PEG_SIZE;i++) {
        if (i<PEG_SIZE/2) {
            pegshift1.f[i] = 13;
        } else {
            pegshift1.f[i] = -13;
        }
    }
    
    QCOMPARE(pegshift1.Total(), 0);
    
    CPegLevel level1(1,0,
                     0,
                     200+buffer,
                     200+buffer,
                     200+buffer,
                     exchange1, pegshift1);
    
    qDebug() << level1.nSupply << level1.nShift << level1.nShiftLastPart << level1.nShiftLastTotal;

    CPegData pdUser1;
    CPegData pdUser2;
    CPegData pdExchange;
    CPegData pdPegShift;

    pdUser1.fractions = user1;
    pdUser2.fractions = user2;
    pdExchange.fractions = exchange1;
    pdPegShift.fractions = pegshift1;
    
    pdUser1.peglevel = level1;
    pdUser2.peglevel = level1;
    pdExchange.peglevel = level1;
    pdPegShift.peglevel = level1;
    
    pdUser1.nLiquid = user1.High(level1);
    pdUser1.nReserve = user1.Low(level1);
    pdUser2.nLiquid = user2.High(level1);
    pdUser2.nReserve = user2.Low(level1);

    pdExchange.nLiquid = exchange1.High(level1);
    pdExchange.nReserve = exchange1.Low(level1);
    pdPegShift.nLiquid = pegshift1.High(level1);
    pdPegShift.nReserve = pegshift1.Low(level1);
    
    string user1_r1_b64 = pdUser1.ToString();
    string user2_r1_b64 = pdUser2.ToString();
    string exchange1_b64 = pdExchange.ToString();
    string pegshift1_b64 = pdPegShift.ToString();
    
    CPegLevel level2(2,1,0,
                     205+buffer,
                     205+buffer,
                     205+buffer,
                     exchange1, pegshift1);
    
    string peglevel2_hex;
    string pegpool2_b64;
    string out_err;
       
    int64_t out_exchange_liquid;
    int64_t out_exchange_reserve;
    int64_t out_pegpool_value;
 
    bool ok1 = getpeglevel(
                2,
                1,
                0,
                205+buffer,
                205+buffer,
                205+buffer,
                exchange1_b64,
                pegshift1_b64,
                
                peglevel2_hex,
                out_exchange_liquid,
                out_exchange_reserve,
                pegpool2_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << peglevel2_hex.c_str();
    qDebug() << pegpool2_b64.c_str();
    qDebug() << out_err.c_str();
    
    QVERIFY(ok1 == true);
    
    // update balances
    
    string pegpool2_r1_b64;
    string user1_r2_b64;
    int64_t user1_r2_liquid;
    int64_t user1_r2_reserve;
    
    bool ok8 = updatepegbalances(
                user1_r1_b64,
                pegpool2_b64,
                peglevel2_hex,
                
                user1_r2_b64,
                user1_r2_liquid,
                user1_r2_reserve,
                pegpool2_r1_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << user1_r2_b64.c_str();
    qDebug() << pegpool2_r1_b64.c_str();
    qDebug() << out_err.c_str();
    qDebug() << "ok8" << ok8;
    QVERIFY(ok8 == true);
    
    // trade to fail (not update balance

    string user1_f1_b64;
    string user2_f1_b64;
    int64_t user1_f1_liquid;
    int64_t user1_f1_reserve;
    int64_t user2_f1_liquid;
    int64_t user2_f1_reserve;
    
    bool ok9 = moveliquid(
                100,
                user1_r2_b64,
                user2_r1_b64,
                peglevel2_hex,
            
                user1_f1_b64,
                user1_f1_liquid,
                user1_f1_reserve,
                user2_f1_b64,
                user2_f1_liquid,
                user2_f1_reserve,
                out_err);
    
    qDebug() << out_err.c_str();
    QVERIFY(ok9 == false);
    
    bool ok10 = moveliquid(
                100,
                user2_r1_b64,
                user1_r2_b64,
                peglevel2_hex,
                
                user2_f1_b64,
                user2_f1_liquid,
                user2_f1_reserve,
                user1_f1_b64,
                user1_f1_liquid,
                user1_f1_reserve,
                out_err);
    
    qDebug() << out_err.c_str();
    QVERIFY(ok10 == false);
    
    string pegpool2_r2_b64;
    string user2_r2_b64;
    int64_t user2_r2_liquid;
    int64_t user2_r2_reserve;
    
    bool ok11 = updatepegbalances(
                user2_r1_b64,
                pegpool2_r1_b64,
                peglevel2_hex,
                
                user2_r2_b64,
                user2_r2_liquid,
                user2_r2_reserve,
                pegpool2_r2_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << user2_r2_b64.c_str();
    qDebug() << pegpool2_r2_b64.c_str();
    qDebug() << out_err.c_str();
    QVERIFY(ok11 == true);
    
    bool ok12 = moveliquid(
                100000,
                user1_r2_b64,
                user2_r2_b64,
                peglevel2_hex,
                
                user1_f1_b64,
                user1_f1_liquid,
                user1_f1_reserve,
                user2_f1_b64,
                user2_f1_liquid,
                user2_f1_reserve,
                out_err);
    
    qDebug() << out_err.c_str();
    QVERIFY(ok12 == false);
    
    string user1_r3_b64;
    string user2_r3_b64;
    
    bool ok13 = moveliquid(
                22205,
                user1_r2_b64,
                user2_r2_b64,
                peglevel2_hex,
                
                user1_r3_b64,
                user1_f1_liquid,
                user1_f1_reserve,
                user2_r3_b64,
                user2_f1_liquid,
                user2_f1_reserve,
                out_err);
    
    qDebug() << out_err.c_str();
    qDebug() << user1_r3_b64.c_str();
    qDebug() << user2_r3_b64.c_str();
    QVERIFY(ok13 == true);
    
}
