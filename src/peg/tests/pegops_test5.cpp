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

void TestPegOps::test5()
{
    CFractions user1(0, CFractions::STD);
    for(int i=0;i<PEG_SIZE;i++) {
        user1.f[i] = 23;
    }
    
    CPegData pdPegShift;
    
    CPegLevel level1(1,0,0,3,3,3);
    
    pdPegShift.peglevel = level1;
    
    CPegData pdUser1;
    pdUser1.fractions = user1;
    pdUser1.peglevel = level1;
    pdUser1.nLiquid = pdUser1.fractions.High(level1);
    pdUser1.nReserve = pdUser1.fractions.Low(level1);

    string user1_b64 = pdUser1.ToString();
    string shift_b64 = pdPegShift.ToString();
    
    string exchange1_b64 = user1_b64;
    string pegshift1_b64 = shift_b64;
    
    qDebug() << "exchange1_b64" << exchange1_b64.c_str();
    qDebug() << "pegshift1_b64" << pegshift1_b64.c_str();
    
    string peglevel1_hex;
    string pegpool1_b64;
    string out_err;
    
    int64_t out_exchange_liquid;
    int64_t out_exchange_reserve;
    int64_t out_pegpool_value;

    bool ok1 = getpeglevel(
                1,
                0,
                
                0,
                3,
                3,
                3,
                exchange1_b64,
                pegshift1_b64,
                
                peglevel1_hex,
                out_exchange_liquid,
                out_exchange_reserve,
                pegpool1_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << peglevel1_hex.c_str();
    qDebug() << pegpool1_b64.c_str();
    qDebug() << out_err.c_str();
    
    QVERIFY(ok1 == true);
    QVERIFY(peglevel1_hex == "02010000000000000000000000000000000000030003000300000000000000000000000000000000000000");
    QVERIFY(pegpool1_b64 == "AQAAAAAAAAABAgACAAAAAAAAAAAAACsAAAAAAAAAeNrtxSEBAAAIA7CTgv5NMchH2MySbv+xbdu2bdu2bdu2bdu2bdcP6A4ExAIBAAAAAAAAAAAAAAAAAAAAAAADAAMAAwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAItrAAAAAAAAAAAAAA==");
    
    // only 1 user, calculate balance:
    
    string pegpool1_r2_b64;
    string user1_r2_b64;
    int64_t user1_r2_liquid;
    int64_t user1_r2_reserve;
    
    bool ok2 = updatepegbalances(
                user1_b64,
                pegpool1_b64,
                peglevel1_hex,
                
                user1_r2_b64,
                user1_r2_liquid,
                user1_r2_reserve,
                pegpool1_r2_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << "user1_r2_b64" << user1_r2_b64.c_str();
    qDebug() << "pegpool1_r2_b64" << pegpool1_r2_b64.c_str();
    qDebug() << out_err.c_str();
    QVERIFY(ok2 == true);
    
    // move to cycle2, peg +10
    
    string peglevel2_hex;
    string pegpool2_b64;
    
    bool ok3 = getpeglevel(
                2,
                1,
                0,
                13,
                13,
                13,
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
    
    QVERIFY(ok3 == true);
    
    string pegpool1_r3_b64;
    string user1_r3_b64;
    int64_t user1_r3_liquid;
    int64_t user1_r3_reserve;
    
    bool ok4 = updatepegbalances(
                user1_r2_b64,
                pegpool2_b64,
                peglevel2_hex,
                
                user1_r3_b64,
                user1_r3_liquid,
                user1_r3_reserve,
                pegpool1_r3_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << "user1_r3_b64" << user1_r3_b64.c_str();
    qDebug() << pegpool1_r3_b64.c_str();
    qDebug() << out_err.c_str();
    QVERIFY(ok4 == true);
    // pegpool is zero
    QVERIFY(pegpool1_r3_b64 == "AQAAAAAAAAABAgACAAAAAAAAAAAAACAAAAAAAAAAeNrtwTEBAAAAwqD1T20Hb6AAAAAAAAAAAAB4DCWAAAECAgAAAAAAAAABAAAAAAAAAAAADQANAA0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
    
    // move to cycle3, peg +10
    
    string peglevel3_hex;
    string pegpool3_b64;
    
    bool ok5 = getpeglevel(
                3,
                2,
                0,
                23,
                23,
                23,
                exchange1_b64,
                pegshift1_b64,
                
                peglevel3_hex,
                out_exchange_liquid,
                out_exchange_reserve,
                pegpool3_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << peglevel3_hex.c_str();
    qDebug() << pegpool3_b64.c_str();
    qDebug() << out_err.c_str();
    
    QVERIFY(ok5 == true);
    
    string pegpool3_r1_b64;
    string user1_r4_b64;
    int64_t user1_r4_liquid;
    int64_t user1_r4_reserve;
    
    bool ok6 = updatepegbalances(
                user1_r3_b64,
                pegpool3_b64,
                peglevel3_hex,
                
                user1_r4_b64,
                user1_r4_liquid,
                user1_r4_reserve,
                pegpool3_r1_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << user1_r4_b64.c_str();
    qDebug() << pegpool3_r1_b64.c_str();
    qDebug() << out_err.c_str();
    QVERIFY(ok6 == true);
    
    QVERIFY(pegpool3_r1_b64 == "AQAAAAAAAAABAgACAAAAAAAAAAAAACAAAAAAAAAAeNrtwTEBAAAAwqD1T20Hb6AAAAAAAAAAAAB4DCWAAAECAwAAAAAAAAACAAAAAAAAAAAAFwAXABcAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
    
    CFractions user2(0,CFractions::STD);
    for(int i=10;i<PEG_SIZE-100;i++) {
        user2.f[i] = 79;
    }
    
    CFractions exchange2 = pdUser1.fractions + user2;
    CPegLevel level3(3,0,0,0,0,0);
    
    CPegData pdUser2;
    pdUser2.fractions = user2;
    pdUser2.peglevel = level3;
    pdUser2.nLiquid = user2.High(level3);
    pdUser2.nReserve = user2.Low(level3);

    CPegData pdExchange2;
    pdExchange2.fractions = exchange2;
    pdExchange2.peglevel = level3;
    pdExchange2.nLiquid = exchange2.High(level3);
    pdExchange2.nReserve = exchange2.Low(level3);
    
    string user2_b64 = pdUser2.ToString();
    string exchange2_b64 = pdExchange2.ToString();

    qDebug() << "exchange2_b64" << exchange2_b64.c_str();
    
    // move to cycle4, peg +10
    
    string peglevel4_hex;
    string pegpool4_b64;
    
    bool ok7 = getpeglevel(
                4,
                3,
                0,
                33,
                33,
                33,
                exchange2_b64,
                pegshift1_b64,
                
                peglevel4_hex,
                out_exchange_liquid,
                out_exchange_reserve,
                pegpool4_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << peglevel4_hex.c_str();
    qDebug() << pegpool4_b64.c_str();
    qDebug() << out_err.c_str();
    
    QVERIFY(ok7 == true);
    
    string pegpool4_r1_b64;
    string user1_r5_b64;
    int64_t user1_r5_liquid;
    int64_t user1_r5_reserve;
    
    bool ok8 = updatepegbalances(
                user1_r4_b64,
                pegpool4_b64,
                peglevel4_hex,
                
                user1_r5_b64,
                user1_r5_liquid,
                user1_r5_reserve,
                pegpool4_r1_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << user1_r5_b64.c_str();
    qDebug() << pegpool4_r1_b64.c_str();
    qDebug() << out_err.c_str();
    QVERIFY(ok8 == true);
    
    string pegpool4_r2_b64;
    string user2_r1_b64;
    int64_t user2_r1_liquid;
    int64_t user2_r1_reserve;
    
    bool ok9 = updatepegbalances(
                user2_b64,
                pegpool4_r1_b64,
                peglevel4_hex,
                
                user2_r1_b64,
                user2_r1_liquid,
                user2_r1_reserve,
                pegpool4_r2_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << user2_r1_b64.c_str();
    qDebug() << pegpool4_r2_b64.c_str();
    qDebug() << out_err.c_str();
    QVERIFY(ok9 == true);
    
    // move to cycle5, peg -5
    
    string peglevel5_hex;
    string pegpool5_b64;
    
    bool ok10 = getpeglevel(
                5,
                4,
                0,
                28,
                28,
                28,
                exchange2_b64,
                pegshift1_b64,
                
                peglevel5_hex,
                out_exchange_liquid,
                out_exchange_reserve,
                pegpool5_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << peglevel5_hex.c_str();
    qDebug() << pegpool5_b64.c_str();
    qDebug() << out_err.c_str();
    
    QVERIFY(ok10 == true);
    
    string pegpool5_r1_b64;
    string user1_r6_b64;
    int64_t user1_r6_liquid;
    int64_t user1_r6_reserve;
    
    bool ok11 = updatepegbalances(
                user1_r5_b64,
                pegpool5_b64,
                peglevel5_hex,
                
                user1_r6_b64,
                user1_r6_liquid,
                user1_r6_reserve,
                pegpool5_r1_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << user1_r6_b64.c_str();
    qDebug() << pegpool5_r1_b64.c_str();
    qDebug() << out_err.c_str();
    QVERIFY(ok11 == true);
    
    string pegpool5_r2_b64;
    string user2_r2_b64;
    int64_t user2_r2_liquid;
    int64_t user2_r2_reserve;
    
    bool ok12 = updatepegbalances(
                user2_r1_b64,
                pegpool5_r1_b64,
                peglevel5_hex,
                
                user2_r2_b64,
                user2_r2_liquid,
                user2_r2_reserve,
                pegpool5_r2_b64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << user2_r2_b64.c_str();
    qDebug() << pegpool5_r2_b64.c_str();
    qDebug() << out_err.c_str();
    QVERIFY(ok12 == true);
    QVERIFY(pegpool5_r2_b64 == "AQAAAAAAAAABAgACAAAAAAAAAAAAACAAAAAAAAAAeNrtwTEBAAAAwqD1T20Hb6AAAAAAAAAAAAB4DCWAAAECBQAAAAAAAAAEAAAAAAAAAAAAHAAcABwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
}
