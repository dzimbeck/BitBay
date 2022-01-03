// Copyright (c) 2018 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include "pegops.h"
#include "pegdata.h"
#include "pegops_tests.h"

#include <string>
#include <vector>

using namespace std;
using namespace pegops;

void TestPegOps::test1k()
{
    std::default_random_engine generator;
    std::uniform_int_distribution<int> distribution(0,1000);
    
    CPegLevel level1(1,0,0,500,500,500);
    qDebug() << level1.nSupply << level1.nShift << level1.nShiftLastPart << level1.nShiftLastTotal;
    
    vector<CFractions> users;
    vector<string> user_balances;
    // init users
    for(int i=0; i< 1000; i++) {
        CFractions user(0,CFractions::STD);
        int start = distribution(generator);
        for(int i=start;i<PEG_SIZE;i++) {
            user.f[i] = distribution(generator) / (i*5/6+1);
        }
        users.push_back(user);
        CPegData pdUser;
        pdUser.fractions = user;
        pdUser.peglevel = level1;
        pdUser.nLiquid = user.High(level1);
        pdUser.nReserve = user.Low(level1);
        string user_b64 = pdUser.ToString();
        user_balances.push_back(user_b64);
    }
    
    // calc exchange
    CFractions exchange(0,CFractions::STD);
    for(int i=0; i< 1000; i++) {
        CFractions & user = users[i];
        exchange += user;
    }
    
    CFractions pegshift(0,CFractions::STD);
    for(int i=0;i<PEG_SIZE;i++) {
        if (i<PEG_SIZE/2) {
            pegshift.f[i] = distribution(generator) /10 / (i+1);
        } else {
            pegshift.f[i] = -pegshift.f[PEG_SIZE-i-1];
        }
    }
    
    QCOMPARE(pegshift.Total(), 0);
    
    for(int i=0; i<10; i++) {
        
        CPegData pdExchange;
        pdExchange.fractions = exchange;
        pdExchange.peglevel = level1;
        pdExchange.nReserve = exchange.Low(level1);
        pdExchange.nLiquid = exchange.High(level1);

        CPegData pdPegShift;
        pdPegShift.fractions = pegshift;
        pdPegShift.peglevel = level1;
        pdPegShift.nReserve = pegshift.Low(level1);
        pdPegShift.nLiquid = pegshift.High(level1);
        
        string exchange1_b64 = pdExchange.ToString();
        string pegshift1_b64 = pdPegShift.ToString();
        
        string peglevel_hex;
        string pegpool_b64;
        string out_err;
        
        int64_t out_exchange_liquid;
        int64_t out_exchange_reserve;
        int64_t out_pegpool_value;
        
        int peg = 0;
        int buffer = 3;
        if (i<500) {
            peg = 500+i;
        } else {
            peg = 500+500 - (i-500)*2;
        }
        
        bool ok1 = getpeglevel(
                    i+2,
                    i+1,
                    0,
                    peg+buffer,
                    peg+buffer,
                    peg+buffer,
                    exchange1_b64,
                    pegshift1_b64,
                    
                    peglevel_hex,
                    out_exchange_liquid,
                    out_exchange_reserve,
                    pegpool_b64,
                    out_pegpool_value,
                    out_err
                    );
        
        //qDebug() << peglevel_hex.c_str();
        //qDebug() << pegpool_b64.c_str();
        //qDebug() << out_err.c_str();
        qDebug() << i;
        
        QVERIFY(ok1 == true);
    
        // update balances
        for(int j=0; j< 1000; j++) {
            string & user_balance = user_balances[j];
            
            string pegpool_out_b64;
            string user_balance_out_b64;
            int64_t user_balance_out_liquid;
            int64_t user_balance_out_reserve;
            
            bool ok8 = updatepegbalances(
                        user_balance,
                        pegpool_b64,
                        peglevel_hex,
                        
                        user_balance_out_b64,
                        user_balance_out_liquid,
                        user_balance_out_reserve,
                        pegpool_out_b64,
                        out_pegpool_value,
                        out_err
                        );
            
            if (!ok8) {
                qDebug() << user_balance_out_b64.c_str();
                qDebug() << pegpool_out_b64.c_str();
                qDebug() << out_err.c_str();
            }
            QVERIFY(ok8 == true);
            
            pegpool_b64 = pegpool_out_b64;
            user_balance = user_balance_out_b64;
        }
        
        // pool should be empty
        CPegData pdPegPool(pegpool_b64);
        if (!pdPegPool.IsValid()) {
            QVERIFY(false);
        }
        
        if (pdPegPool.fractions.Total() != 0) {
            qDebug() << pegpool_b64.c_str();
        }
        QVERIFY(pdPegPool.fractions.Total() == 0);
        
        // some trades
        for(int j=0; j<1000; j++) {
            
            int src_idx = std::min(distribution(generator), 999);
            int dst_idx = std::min(distribution(generator), 999);
            
            if (src_idx == dst_idx) continue;
            
            string & user_src = user_balances[src_idx];
            string & user_dst = user_balances[dst_idx];
            
            string src_out_b64;
            string dst_out_b64;
            
            int64_t src_out_liquid;
            int64_t src_out_reserve;
            int64_t dst_out_liquid;
            int64_t dst_out_reserve;
            
            CPegData pdSrc(user_src);
            if (!pdSrc.IsValid()) {
                QVERIFY(false);
            }
            
            string user_src_copy = user_src;
            string user_dst_copy = user_dst;
            
            if (src_idx % 2 == 0) {
                
                int64_t amount = (pdSrc.nLiquid * (distribution(generator)/100+1)) /10;
                amount = std::min(amount, pdSrc.nLiquid);
                bool ok11 = moveliquid(
                            amount,
                            user_src,
                            user_dst,
                            peglevel_hex,
                            
                            src_out_b64,
                            src_out_liquid,
                            src_out_reserve,
                            dst_out_b64,
                            dst_out_liquid,
                            dst_out_reserve,
                            out_err);
                
                if (!ok11) {
                    qDebug() << "problem trade:";
                    qDebug() << out_err.c_str();
                    qDebug() << amount;
                    qDebug() << user_src_copy.c_str();
                    qDebug() << user_dst_copy.c_str();
                    qDebug() << src_out_b64.c_str();
                    qDebug() << dst_out_b64.c_str();
                    qDebug() << peglevel_hex.c_str(); 
                }
                QVERIFY(ok11 == true);
                
                user_src = src_out_b64;
                user_dst = dst_out_b64;
                
                // extra checks compare all
                // calc exchange
                /*
                CFractions exchange1(0,CFractions::STD);
                for(int k=0; k< 1000; k++) {
                    //CFractions & user = users[i];
                    CPegLevel user_skip("");
                    CFractions user(0, CFractions::STD);
                    bool ok = unpackbalance(user,
                                              user_skip, 
                                              user_balances[k], 
                                              "user", out_err);
                    if (!ok) {
                        qDebug() << out_err.c_str();
                    }
                    QVERIFY(ok == true);
                    
                    exchange1 += user;
                }
                
                for(int k=0;k<PEG_SIZE; k++) {
                    if (exchange.f[k] != exchange1.f[k]) {
                        qDebug() << "problem trade:";
                        qDebug() << amount;
                        qDebug() << user_src_copy.c_str();
                        qDebug() << user_dst_copy.c_str();
                        qDebug() << src_out_b64.c_str();
                        qDebug() << dst_out_b64.c_str();
                        qDebug() << peglevel_hex.c_str();
                        qDebug() << "exchange orig";
                        qDebug() << packpegdata(exchange, level1).c_str();
                        qDebug() << "exchange new";
                        qDebug() << packpegdata(exchange1, level1).c_str();
                        qDebug() << "exchange diff";
                        CFractions diff = exchange1 - exchange;
                        qDebug() << packpegdata(diff, level1).c_str();
                    }
                    QVERIFY(exchange.f[k] == exchange1.f[k]);
                }
                */
            }
            else {
                int64_t amount = (pdSrc.nReserve * (distribution(generator)/100+1)) /10;
                amount = std::min(amount, pdSrc.nReserve);
                bool ok12 = movereserve(
                            amount,
                            user_src,
                            user_dst,
                            peglevel_hex,
                            
                            src_out_b64,
                            src_out_liquid,
                            src_out_reserve,
                            dst_out_b64,
                            dst_out_liquid,
                            dst_out_reserve,
                            out_err);
                
                if (!ok12) {
                    qDebug() << "problem trade:";
                    qDebug() << out_err.c_str();
                    qDebug() << amount;
                    qDebug() << user_src_copy.c_str();
                    qDebug() << user_dst_copy.c_str();
                    qDebug() << src_out_b64.c_str();
                    qDebug() << dst_out_b64.c_str();
                    qDebug() << peglevel_hex.c_str(); 
                }
                QVERIFY(ok12 == true);
                
                user_src = src_out_b64;
                user_dst = dst_out_b64;
            }
        }
    
        // extra checks compare all
        // calc exchange
        
        CFractions exchange1(0,CFractions::STD);
        for(int k=0; k< 1000; k++) {
            
            CPegData pdUser(user_balances[k]);
            if (!pdUser.IsValid()) {
                QVERIFY(false);
            }
            
            exchange1 += pdUser.fractions;
        }
        
        for(int k=0;k<PEG_SIZE; k++) {
            if (exchange.f[k] != exchange1.f[k]) {
                
//                int64_t nLiquidExchange = exchange.High(level1);
//                int64_t nReserveExchange = exchange.Low(level1);
//                int64_t nLiquidExchange1 = exchange1.High(level1);
//                int64_t nReserveExchange1 = exchange1.Low(level1);
                
//                qDebug() << "exchange orig";
//                qDebug() << packpegdata(exchange, level1, nReserveExchange, nLiquidExchange).c_str();
//                qDebug() << "exchange new";
//                qDebug() << packpegdata(exchange1, level1, nReserveExchange1, nLiquidExchange1).c_str();
                qDebug() << "exchange diff";
                CFractions diff = exchange1 - exchange;
                
                CPegData pdDiff;
                pdDiff.fractions = diff;
                pdDiff.peglevel = level1;
                pdDiff.nLiquid = diff.High(level1);
                pdDiff.nReserve = diff.Low(level1);
                
                qDebug() << pdDiff.ToString().c_str();
            }
            QVERIFY(exchange.f[k] == exchange1.f[k]);
        }

    }

}
