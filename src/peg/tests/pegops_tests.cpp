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

TestPegOps::TestPegOps(QObject * p) :QObject(p) {
    qputenv("QTEST_FUNCTION_TIMEOUT", "36000000");
}

void TestPegOps::test1()
{
    string inp_peglevel_hex = "202d0000000000001f2d0000000000000d0010001300000008adf56d0000000095c564c1890a0000";
    
    int out_version;
    int out_cycle_now;
    int out_cycle_prev;
    int out_buffer;
    int out_peg_now;
    int out_peg_next;
    int out_peg_next_next;
    int out_shift;
    int64_t out_shiftlastpart;
    int64_t out_shiftlasttotal;
    string out_err;
    
    bool ok = getpeglevelinfo(
                inp_peglevel_hex,
                    
                out_version,
                out_cycle_now,    
                out_cycle_prev,   
                out_buffer,
                out_peg_now,      
                out_peg_next,     
                out_peg_next_next,
                out_shift,        
                out_shiftlastpart,
                out_shiftlasttotal,
                out_err);
    
    QVERIFY(ok                  == true);
    QVERIFY(out_cycle_now       == 11552);
    QVERIFY(out_cycle_prev      == 11551);
    QVERIFY(out_peg_now         == 13);
    QVERIFY(out_peg_next        == 16);
    QVERIFY(out_peg_next_next   == 19);
    QVERIFY(out_shift           == 0);
    QVERIFY(out_shiftlastpart   == 1844817160);
    QVERIFY(out_shiftlasttotal  == 11586771404181);
    
    qDebug() << out_cycle_now
        << out_cycle_prev
        << out_peg_now     
        << out_peg_next      
        << out_peg_next_next
        << out_shift
        << out_shiftlastpart
        << out_shiftlasttotal;
}

void TestPegOps::test2()
{
    string inp_peglevel_hex = "202d000snhgajhg0000d0010001304c1890a0000";
    
    int out_version;
    int out_cycle_now;
    int out_cycle_prev;
    int out_buffer;
    int out_peg_now;
    int out_peg_next;
    int out_peg_next_next;
    int out_shift;
    int64_t out_shiftlastpart;
    int64_t out_shiftlasttotal;
    string out_err;
    
    bool ok = getpeglevelinfo(
                inp_peglevel_hex,
                    
                out_version,
                out_cycle_now,    
                out_cycle_prev,   
                out_buffer,
                out_peg_now,      
                out_peg_next,     
                out_peg_next_next,
                out_shift,        
                out_shiftlastpart,
                out_shiftlasttotal,
                out_err);
    
    QVERIFY(ok == false);
}

void TestPegOps::test3()
{
    string pegpool_base64 = "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";
    string balance_base64 = "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";
    string peglevel_hex1 = "6e2d0000000000006d2d000000000000f700fa00fd00000000000000000000000000000000000000";
    
    string out_pegpool_pegdata64;
    string out_balance_pegdata64;
    int64_t out_balance_liquid;
    int64_t out_balance_reserve;
    int64_t out_pegpool_value;
    string out_err;
    
    bool ok = updatepegbalances(
                balance_base64,
                pegpool_base64,
                peglevel_hex1,
                
                out_balance_pegdata64,
                out_balance_liquid,
                out_balance_reserve,
                out_pegpool_pegdata64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << out_err.c_str();
    QVERIFY(ok == true);
    QVERIFY(out_err == "Already up-to-dated");
}

void TestPegOps::test4()
{
    string pegpool_base64 = "AgACAAAAAAAAAAAAZAgAAAAAAAB42u1Z93NVRRh9Ly+FF4IhAUKLICDVQYowEFTQgEMACdIkUhxDyQTpMgIBdAREmqgUpQsK0qvUgFJEkNCLotIEISAtlBBKIPfpu/cc5u2Z4Q9wJu+Xk6/s3t1vv7Yblyv/l//L/+X/8n/5v/zf/+VXOXrhqKD/sFv3mj7/LxL8+z7ntz3IoXeBjoL88PSZTf30RcgBroL443j2sQg/BoNf6LgzPiajd7ifzji52ebXiLlu+fk7MH9B6Cd7HbTAfwScW8bh54Ees3STjXO2ZNj8EIy/Cflf02LtoYXA9wDPZTW2VVfUXpPrl2/MdfQ57uWDdw17uIEXIS8A+kbX+1l+2lP9gM3Hsl3nDwYZdqbdKpzxRAbaNxx87q9zgx72ErEcH2dJCalnL+H4zlMX/Hzus3apq6F+rLvsqaKB59AuuqI9z6VWq+2pczDfxLc62UdStnGXMD/GXQo27LVoaB97voTM6bbgFvj9Yrbb85US/xiwe6Un8JxH1T7rDlw/7bR19jZbD2zfHeChhOIP/ZgFmvaujg1eEjs9AB192qE5zhJ7cT0PwXdjQRwfInFwXfyvgPhRKOjbsp5gsce2sZfsJSxJzLbtTjtEQ+8uaI98/yr4hUHHnW5mb4nrp160rNcr/kM/5XmHybpjQF8Teye23WNvKVz2TXv2PlDWCrQb7Z0nyJ/69z1Zn1v43GdG7WqewP1w3qegnw06paZDh8t5XRR/oJznsMnrNvKEW+zHdUe26RgceK5cR0KSHWauCLEr/eP9NsmewDikPRq2uWecL9c5sltbwx/PtXewRd/rIYH7YhxWPLXDpjNBt4RBcmSdQ+rE3Q88X8ojxocY/vY4bwPf6dTZ5vNcisp6ue88OTf6YWjKOMOuLonba1JHbovf35D49Mr6Lj/B76gfJnm+/sBY43y84v/kR0pchcp8tEchiXeut4ic9y3JJ7R/iU5p7kB/43zMh8wPpcG/IOspIPohsh637MfzhHygcRcpduZ8uZKvNC/w++EST5qPvXLeGj9Bsj+P7JN5tpj4U56Mvyf+dFPq0GXx6wJybrlip2DZj+qpHbIkX5eW/HJZ7H1H1hcm9vfK/LdE3yV17qHIfWKnwhK/audI+U6o2NsrdtHvZ4tekOgFy/d9Ege35Xx4flGVpgYF5nH6WYT47Z0n+DX3mSnnGybjomT/hUWu47JkPV6Jt0JSt89LXaLe06D/eUJd5zm8O+iRYTfmnQEPHxn1i+d1aGOesT7W0dM55ndo18nNU+f56T2gT0G+lP0pcCAmjEdgLIAe9YcCm/M8genSh3QDtsZ8RSDfApzNfQM/x/cOQ/47sJ7b9J8Z0OvHfAj5YshbsN9lvoWjLsC44eDvx/iSwNHAK5BXAZ2GcRNlvbRHb8i7gF4OrAXshPl2QO8R+J2lnoxxm9+vA5wDPIBxzzHuJX/tZHxYpn2vSD6YALoc6DdBNwJdGOuYAHoR5osFvzX7XmBbqRP9gT+I/78KbAmsAvnbwA3gz8X3WnE+yXNjwP8MOAK4CvJ4qVcngPswbxQEtVh/gDWg9yfos6Bb0e/Yl4E/C9gB/MXML/jOM/jOds9Ax18kPzCPR/nMdbFPaMC4AL2P8Y/5G0N/r2Xa8QOMew/0auitAJ06ym3024yvekl5Rj5ivzTotY7Ou4Bl5sNg/LEJer3AjwH9DeQnLNPP1wKToD9E8l1x4ALeuzB+veSRrxifoHNAHwN9VOKSeaEc5kmHfC79E/IlkJ+0zLjyyHk1hV4l7h/4G/jZcNhq9D/2wTWcDNlH7BZ50rH885IXxgJ7QO9b7q+K86HG4HNfEcwfvG+CfwzrSQMfzy0unn8xjGM+WAh+c9ihBuhUyJeLPZjvllvme0t5jFvJegea9vqD9YL3P+B01jXQR1zmOqeAXgl8Fvx7zA8Stx2Ba4C7eY7Qp33LAz8GNuK9U+r8COyTfQfr4jTQG6Xfy4D+Hex/JgIjxW2+E+RA7wuMPyp9QgLvs3KvYny+hHrXkHmI+4Fee3xvLeS/SD9XSvLH+mRHcZL0WZOYD5hPWactR3BE+irW6VWsJ8B1zFPsk3jf5Duf9E0X66EPBr8dsAPkY0A3A/038KBl9h9xfCd1m/7zNft/7YdAFxO/2495b7CfBt2NeQZxNwd0feZhiWfW+8rgL6M/AV/BvKx3kzVupH/dIX0Hnml9FdymHwfJeSdL/h0N3Mx6Jv0V6zTf83pKnY4FzmM+l77iNPiJvEfhO8Mg/wn8T6VfY19Bew6T82iDeWjX0XLPPMh3HyD7iVkYnyVxcwz0XtCsU6xn3dn3W6bfsg+YzToO+Xzw2e9+yHoL/AjzDRa/Zb4eLv3PFOiVAD2e/gx6J3A+9HphngTaGfImGNeE9w9guvTlVYEz2SdJnzXcba6jJft/8esy4ic/+sw+nXX5uMzP+K3EexyQ+YT+7JG+jf3RXJ9pL/rhGr73gf6EedYy3x+vyvsl94lnyMfjJ8i9KQ0OXUryN+8v7M/pF+WAW9kPir3HWeZ99pDkrS+lrsfLfaIC3/eBXYHdLbMvY/1nXmA8z2efAr0X+P8l9oNybozT7yWOWCebsi8AnoQ8SfyZ+0mQuPsV69wJmnluGPh9wY8nDfnPwBeBezHvILmv0n/Ky/drSf3cYJn1j+fbE99dL/3OA4mTW8xzcv7ZmJd+MBX4OnAz5HVBV5V3lJKSx+h/6+Q8EyRvvQEcDLyJeWryviJ9D+OOdao+78PAkS6zD+d9tLXYq4T4B9/LmF95X62Ieb5j/wi8Iv0H43Soz3xH6Cp1iPfBGcBdmH8r5MxfmeCnZ4ca/fikw05lTUl0G35XDAWoP/0TyL69zxn0d9tS7Sv5v8XopVBvLQAAAAAAAG4tAAAAAAAA9wD6AP0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEedIfDgEAAA==";
    string balance_base64 = "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";
    string peglevel_hex2 = "6f2d0000000000006e2d000000000000f700fa00fd00000000000000000000000000000000000000";
    
    string out_pegpool_pegdata64;
    string out_balance_pegdata64;
    int64_t out_balance_liquid;
    int64_t out_balance_reserve;
    int64_t out_pegpool_value;
    string out_err;
    
    bool ok = updatepegbalances(
                balance_base64,
                pegpool_base64,
                peglevel_hex2,
                
                out_balance_pegdata64,
                out_balance_liquid,
                out_balance_reserve,
                out_pegpool_pegdata64,
                out_pegpool_value,
                out_err
                );
    
    qDebug() << out_err.c_str();
    qDebug() << "out_pegpool_pegdata64" << out_pegpool_pegdata64.c_str();
    QVERIFY(ok == true);
//    QVERIFY(out_pegpool_pegdata64 == "AgACAAAAAAAAAgACAAAAAAAAAAAAb+1Z93NVRRh9Ly+FF4IhAUKLICDVQYowEFTQgEMACdIkUhxDyQTpMgIBdAREmqgUpQsK0qvUgFJEkNCLotIEISAtlBBKIPfpu/cc5u2Z4Q9wJu+Xk6/s3t1vv7Yblyv/l//L/+X/8n/5v/zf/+VXOSAAAAAAAAAAeNrtwTEBAAAAwqD1T20Hb6AAAAAAAAAAAAB4DCWAAAH3by0AAAAAAABuLQAAAAAAAAMA9wD6AP0AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAHzur3M=");
    
//    CPegData pdSrc("AgACAAAAAAABAgACAAAAAAAAAAAAb+1Z93NVRRh9Ly+FF4IhAUKLICDVQYowEFTQgEMACdIkUhxDyQTpMgIBdAREmqgUpQsK0qvUgFJEkNCLotIEISAtlBBKIPfpu/cc5u2Z4Q9wJu+Xk6/s3t1vv7Yblyv/l//L/+X/8n/5v/zf/+VXOSAAAAAAAAAAeNrtwTEBAAAAwqD1T20Hb6AAAAAAAAAAAAB4DCWAAAH3by0AAAAAAABuLQAAAAAAAPcA+gD9AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA3HHzu");
//    if (!pdSrc.IsValid()) {
//        QVERIFY(false);
//    }
}

void TestPegOps::test4a()
{
    int64_t amount = 283;
    string user_src = "AgACAAAAAAAAAAAAUgAAAAAAAAB42u3WMQ4AIQgEQOX/j/YDWhgKhcw0G4srLi6RMQAAAIDfTdk6Md+7PmT79LrP+k1mHrLz0f39jKL/GZf3F4fz7fdV9wJ7kpT2ZwCgjgWk0QEZAgAAAAAAAAABAAAAAAAAAPcB9wH3AQAAtAAAAAAAAABLAQAAAAAAAAAAAAAAAAAAOwEAAAAAAAA=";
    string user_dst = "AgACAAAAAAAAAAAAIAAAAAAAAAB42u3BMQEAAADCoPVPbQdvoAAAAAAAAAAAAHgMJYAAAQIAAAAAAAAAAQAAAAAAAAD3AfcB9wEAALQAAAAAAAAASwEAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    string peglevel_hex = "02000000000000000100000000000000f701f701f7010000b4000000000000004b01000000000000";
        
    string src_out_b64;
    string dst_out_b64;
    string out_err;
    
    int64_t src_out_liquid;
    int64_t src_out_reserve;
    int64_t dst_out_liquid;
    int64_t dst_out_reserve;
    
    CPegData pdSrc(user_src);
    if (!pdSrc.IsValid()) {
        QVERIFY(false);
    }

    CPegData pdDst(user_dst);
    if (!pdDst.IsValid()) {
        QVERIFY(false);
    }

    CFractions inp = pdSrc.fractions+pdDst.fractions;
    
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
    
    CPegData pdSrcOut(src_out_b64);
    if (!pdSrcOut.IsValid()) {
        QVERIFY(false);
    }

    CPegData pdDstOut(dst_out_b64);
    if (!pdSrcOut.IsValid()) {
        QVERIFY(false);
    }
    
    CFractions inp_out = pdSrcOut.fractions+pdDstOut.fractions;
    
    qDebug() << out_err.c_str();
    QVERIFY(ok11 == true);
    
    QVERIFY(inp.Total() == inp_out.Total());
    
    for(int j=0;j<PEG_SIZE; j++) {
        QVERIFY(inp.f[j] == inp_out.f[j]);
    }
    
    qDebug() << "src_out_b64" << src_out_b64.c_str();
    qDebug() << "dst_out_b64" << dst_out_b64.c_str();
    
    QVERIFY(src_out_b64 == "AgACAAAAAAAAAgACAAAAAAAAAAAAAgAAJQAAAAAAAAB42u3FQREAAAgDoNm/tDXmCR8SAAAAAAAA4Ip5PgBAmwVKDwAhAgIAAAAAAAAAAQAAAAAAAAADAPcB9wH3AQAAtAAAAAAAAABLAQAAAAAAAAAAAAAAAAAAIAAAAAAAAAAAAAAA");
    QVERIFY(dst_out_b64 == "AgACAAAAAAAAAgACAAAAAAAAAAAAAgAAUAAAAAAAAAB42u3WsQ0AIAgEQGX/oV1AC0OhkLuGWFgYn8AYAAAAwO+m2rqiv3d5yObpdZ7lm0w/ZPuj+/yMou+My/+Lw/n2ftW9wJ6k2h8BAOpYgEIA+QICAAAAAAAAAAEAAAAAAAAAAwD3AfcB9wEAALQAAAAAAAAASwEAAAAAAAAAAAAAAAAAABsBAAAAAAAAAAAAAA==");
}

QTEST_MAIN(TestPegOps)
