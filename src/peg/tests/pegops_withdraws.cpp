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

void TestPegOps::test1w()
{
    const txinps            inp_txinps = {
        {
            "4790bcc0947d5861b13f2503a9b784493ecfa1969a4332d0131d2fe055450556:0",
            "AQAAAAAAAAABAgACAAAAAAAAAAAAMjc2YTkxNDgwYWNmZjE1NzJjZTcwMjg5NDVhMDVhYmFjNDc1ZGEwNGU2MzRmZTQ4OGFjzgIAAAAAAAB42u1ZMXIVMQy15CTkALlAhhuEhqEiRY7AAdLAIeACKXIMLkBDSQE1B6ChTsp0yfy/y/CzzszXrPLe86ZcNzvr9ciy9PQka0tZxzrWsY51LBlnH1/7/+c4jYNp3qZnmy/JvCVyT+/GvXEI5JmodyZXlWfi+0HY7/TV5d45s/3b95uvPqvH5Y/t7vu7MP/t77Cbr9P7sG/WJzu278eJ/kdn97v1HuRsE3+cf/o+u34zbdTm2/Pk9zi7/iHYpen9Z/P4bOuiVbL5EvZt4/bqeTy29R9O5uWj/Zrcw8S/8bwe1mXyDZwrjg2In5rIseSZ4TbO1wTPcb2H918JT6jxGf1kpP1inCD/VrA/e/5CntdJfyC+RLxnZBwVEj+s/Mw+2zd952Dt0ParSfxluBjIPHIE4noE+yG9o14Zvn5ec/oinGZ4riS+/WKY1QPxA9rPQJwO5HnUPM/qbSTPDuA8anxmvIj0yHgG5R12PcIrm3/QeZzEOarHKvATsh+bty3hezX+jfSLi3YxgMdsfwf+j/WpWqc7yadO2hHFPRvfKt8jO6j3KxN5wUFeRPIM+CXj8QLs9OUzh3c2j2Z86J11J8s3hYwjte5n8xAbf0XEXxVxx+Y7FE9sfafeK1SeXRqPKB+q9Qdbv6h5Q+U59T7J2gPxp5G8FPUZk3s5wlHGn1Eem2+dxFPmB/YewfKxijs2L1tnHCM/sfpYZ1yzuHip/qKaN9g6QuWR3vsOy0u9/IDkobgwsh6LfVU2v7D1LBtnCH8OeKqS/fsi2qcAXmXvhUNnPsv6Ayyu1b6Seq9k8yHb31HzIJpneXXpfxQDfIX8i/q4yM5V/H+0tK7yzrqXjXvWnmq+YterfS71P56KI7V/rdbbBfCb2pdm+0TeGd+sv1E/gO3PqnUoWy++1Duqm52837J1unovHJP/jibiaen/GDYeop7v3z6+/wOHt3R1ApBTAAAAAAAAj1MAAAAAAAADADAAMwA2AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAJQ1dwAAAAAAAAAA",
            "tprv8ezDmewmXkNvKU8dZbpfRigkWSPLaATumcZwMZ4vufnthHo7LmZGuTBtMUL28iynLqVfQH3EkbMz4KxD3Bv7zYRtxgedbp2YdwtNPeVqjBj"
        }
    };
    const std::string       inp_balance_pegdata64 = "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";
    const std::string       inp_exchange_pegdata64 = "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";
    const std::string       inp_pegshift_pegdata64;
    int64_t                 inp_amount_with_fee = 101000000;
    std::string             inp_address = "mv6EfYbC4RzrQ86Qa6mhdz2iifv6AiBWhp";
    const std::string       inp_peglevel_hex = "0190530000000000008f53000000000000300033003600000000000000000000000000000000000000";
            
    std::string    out_balance_pegdata64;
    int64_t        out_balance_liquid;
    int64_t        out_balance_reserve;
    std::string    out_exchange_pegdata64;
    int64_t        out_exchange_liquid;
    int64_t        out_exchange_reserve;
    std::string    out_pegshift_pegdata64;
    std::string    out_requested_pegdata64;
    std::string    out_processed_pegdata64;
    std::string    out_withdraw_txout;
    std::string    out_withdraw_xchid;
    std::string    out_rawtx;
    txouts         out_txouts;
    std::string    out_err;
    
    bool ok = prepareliquidwithdraw(
            inp_txinps,
            inp_balance_pegdata64,
            inp_exchange_pegdata64,
            inp_pegshift_pegdata64,
            inp_amount_with_fee,
            inp_address,
            inp_peglevel_hex,
            
            out_balance_pegdata64,
            out_balance_liquid,
            out_balance_reserve,
            out_exchange_pegdata64,
            out_exchange_liquid,
            out_exchange_reserve,
            out_pegshift_pegdata64,
            out_requested_pegdata64,
            out_processed_pegdata64,
            out_withdraw_txout,
            out_withdraw_xchid,
            out_rawtx,
            out_txouts,
            out_err);
    
    qDebug() << out_err.c_str();
    qDebug() << out_rawtx.c_str();
    
    qDebug() << "balance:" << out_balance_pegdata64.c_str();
    
    QVERIFY(ok);

}
