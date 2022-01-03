// Copyright (c) 2019 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>

#include "base58.h"
#include "rpcserver.h"
#include "txdb.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "keystore.h"
#include "wallet.h"

#include "pegops.h"
#include "pegopsp.h"
#include "pegdata.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

void printpegshift(const CFractions & frPegShift,
                   const CPegLevel & peglevel,
                   Object & result);

void printpeglevel(const CPegLevel & peglevel,
                   Object & result);

void printpegbalance(const CPegData & pegdata,
                     Object & result,
                     string prefix);

// API calls

Value getpeglevel(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "getpeglevel "
                "<exchange_pegdata_base64> "
                "<pegshift_pegdata_base64> "
                "<previous_cycle>\n"
            );
    
    string exchange_pegdata64 = params[0].get_str();
    string pegshift_pegdata64 = params[1].get_str();
    int nCyclePrev = params[2].get_int();
    
    CPegData pdExchange(exchange_pegdata64);
    if (!pdExchange.IsValid()) {
        string err = "Can not unpack 'exchange' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdPegShift(pegshift_pegdata64);
    if (!pdPegShift.IsValid()) {
        string err = "Can not unpack 'pegshift' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    int nSupplyNow = pindexBest ? pindexBest->nPegSupplyIndex : 0;
    int nSupplyNext = pindexBest ? pindexBest->GetNextIntervalPegSupplyIndex() : 0;
    int nSupplyNextNext = pindexBest ? pindexBest->GetNextNextIntervalPegSupplyIndex() : 0;
    
    int nPegInterval = Params().PegInterval(nBestHeight);
    int nCycleNow = nBestHeight / nPegInterval;
    int nBuffer = 3;
    
    if (nCycleNow <= nCyclePrev) {
        std::stringstream ss;
        ss << "Current cycle "
           << nCycleNow
           << " should be greater than previous "
           << nCyclePrev;
        string err = ss.str();
        throw JSONRPCError(RPC_MISC_ERROR, err);
    }
    
    if (nBestHeight < nPegStartHeight) { // run on zero levels before the peg
        nBuffer = 0;
        nSupplyNow = 0;
        nSupplyNext = 0;
        nSupplyNextNext = 0;
    }
    
    string err;
    CPegLevel peglevel("");
    CPegData pdPegPool;
    
    bool ok = pegops::getpeglevel(
                nCycleNow,
                nCyclePrev,
                nBuffer,
                nSupplyNow,
                nSupplyNext,
                nSupplyNextNext,
                pdExchange,
                pdPegShift,
                
                peglevel,
                pdPegPool,
                err
    );
    if (!ok) {
        throw JSONRPCError(RPC_MISC_ERROR, err);
    }
    
    // update exchange datas 
    pdExchange.peglevel = peglevel;
    pdExchange.nLiquid = pdExchange.fractions.High(peglevel);
    pdExchange.nReserve = pdExchange.fractions.Low(peglevel);
    
    Object result;
    result.push_back(Pair("cycle", peglevel.nCycle));

    printpeglevel(peglevel, result);
    printpegbalance(pdPegPool, result, "pegpool_");
    printpegbalance(pdExchange, result, "exchange_");
    printpegshift(pdPegShift.fractions, peglevel, result);
    
    return result;
}

Value makepeglevel(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 8)
        throw runtime_error(
            "makepeglevel "
                "<current_cycle> "
                "<previous_cycle> "
                "<pegbuffer> "
                "<pegsupplyindex> "
                "<pegsupplyindex_next> "
                "<pegsupplyindex_nextnext> "
                "<exchange_pegdata_base64> "
                "<pegshift_pegdata_base64>\n"
            );
    
    int cycle_now = params[0].get_int();
    int cycle_prev = params[1].get_int();
    int buffer = params[2].get_int();
    int supply_now = params[3].get_int();
    int supply_next = params[4].get_int();
    int supply_next_next = params[5].get_int();
    string exchange_pegdata64 = params[6].get_str();
    string pegshift_pegdata64 = params[7].get_str();
    
    CPegData pdExchange(exchange_pegdata64);
    if (!pdExchange.IsValid()) {
        string err = "Can not unpack 'exchange' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdPegShift(pegshift_pegdata64);
    if (!pdPegShift.IsValid()) {
        string err = "Can not unpack 'pegshift' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    string err;
    CPegLevel peglevel("");
    CPegData pdPegPool;

    bool ok = pegops::getpeglevel(
                cycle_now,
                cycle_prev,
                buffer,
                supply_now,
                supply_next,
                supply_next_next,
                pdExchange,
                pdPegShift,
                
                peglevel,
                pdPegPool,
                err
    );
    if (!ok) {
        throw JSONRPCError(RPC_MISC_ERROR, err);
    }
    
    // update exchange datas 
    pdExchange.peglevel = peglevel;
    pdExchange.nLiquid = pdExchange.fractions.High(peglevel);
    pdExchange.nReserve = pdExchange.fractions.Low(peglevel);
    
    Object result;
    result.push_back(Pair("cycle", peglevel.nCycle));

    printpeglevel(peglevel, result);
    printpegbalance(pdPegPool, result, "pegpool_");
    printpegbalance(pdExchange, result, "exchange_");
    printpegshift(pdPegShift.fractions, peglevel, result);
    
    return result;
}

Value updatepegbalances(const Array& params, bool fHelp)
{
    if (fHelp || (params.size() != 3))
        throw runtime_error(
            "updatepegbalances "
                "<balance_pegdata_base64> "
                "<pegpool_pegdata_base64> "
                "<peglevel_hex>\n"
            );
    
    string inp_balance_pegdata64 = params[0].get_str();
    string inp_pegpool_pegdata64 = params[1].get_str();
    string inp_peglevel_hex = params[2].get_str();
        
    CPegData pdBalance(inp_balance_pegdata64);
    if (!pdBalance.IsValid()) {
        string err = "Can not unpack 'balance' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdPegPool(inp_pegpool_pegdata64);
    if (!pdPegPool.IsValid()) {
        string err = "Can not unpack 'pegpool' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    CPegLevel peglevelNew(inp_peglevel_hex);
    if (!peglevelNew.IsValid()) {
        string err = "Can not unpack peglevel";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    string err;
    
    bool ok = pegops::updatepegbalances(
            pdBalance,
            pdPegPool,
            peglevelNew,

            err);
    
    if (!ok) {
        throw JSONRPCError(RPC_MISC_ERROR, err);
    }

    Object result;

    result.push_back(Pair("completed", true));
    result.push_back(Pair("cycle", peglevelNew.nCycle));
    
    printpeglevel(peglevelNew, result);
    printpegbalance(pdBalance, result, "balance_");
    printpegbalance(pdPegPool, result, "pegpool_");
    
    return result;
}

Value movecoins(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 4)
        throw runtime_error(
            "movecoins "
                "<amount> "
                "<src_pegdata_base64> "
                "<dst_pegdata_base64> "
                "<peglevel_hex>\n"
            );
    
    int64_t inp_move_amount = params[0].get_int64();
    string inp_src_pegdata64 = params[1].get_str();
    string inp_dst_pegdata64 = params[2].get_str();
    string inp_peglevel_hex = params[3].get_str();
    
    CPegData pdSrc(inp_src_pegdata64);
    if (!pdSrc.IsValid()) {
        string err = "Can not unpack 'src' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdDst(inp_dst_pegdata64);
    if (!pdDst.IsValid()) {
        string err = "Can not unpack 'dst' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        string err = "Can not unpack peglevel";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    string err;
    
    bool ok = pegops::movecoins(
                inp_move_amount,
                pdSrc,
                pdDst,
                peglevel,
                true,
            
                err);
    
    if (!ok) {
        throw JSONRPCError(RPC_MISC_ERROR, err);
    }
    
    Object result;
    
    result.push_back(Pair("cycle", peglevel.nCycle));
    
    printpeglevel(peglevel, result);
    printpegbalance(pdSrc, result, "src_");
    printpegbalance(pdDst, result, "dst_");
    
    return result;
}

Value moveliquid(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 4)
        throw runtime_error(
            "moveliquid "
                "<liquid> "
                "<src_pegdata_base64> "
                "<dst_pegdata_base64> "
                "<peglevel_hex>\n"
            );
    
    int64_t inp_move_liquid = params[0].get_int64();
    string inp_src_pegdata64 = params[1].get_str();
    string inp_dst_pegdata64 = params[2].get_str();
    string inp_peglevel_hex = params[3].get_str();
    
    CPegData pdSrc(inp_src_pegdata64);
    if (!pdSrc.IsValid()) {
        string err = "Can not unpack 'src' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdDst(inp_dst_pegdata64);
    if (!pdDst.IsValid()) {
        string err = "Can not unpack 'dst' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        string err = "Can not unpack peglevel";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    string err;
    
    bool ok = pegops::moveliquid(
            inp_move_liquid,
            pdSrc,
            pdDst,
            peglevel,
            
            err);
    
    if (!ok) {
        throw JSONRPCError(RPC_MISC_ERROR, err);
    }
    
    Object result;
    
    result.push_back(Pair("cycle", peglevel.nCycle));
    
    printpeglevel(peglevel, result);
    printpegbalance(pdSrc, result, "src_");
    printpegbalance(pdDst, result, "dst_");
    
    return result;
}

Value movereserve(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 4)
        throw runtime_error(
            "movereserve "
                "<reserve> "
                "<src_pegdata_base64> "
                "<dst_pegdata_base64> "
                "<peglevel_hex>\n"
            );
    
    int64_t inp_move_reserve = params[0].get_int64();
    string inp_src_pegdata64 = params[1].get_str();
    string inp_dst_pegdata64 = params[2].get_str();
    string inp_peglevel_hex = params[3].get_str();
    
    CPegData pdSrc(inp_src_pegdata64);
    if (!pdSrc.IsValid()) {
        string err = "Can not unpack 'src' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdDst(inp_dst_pegdata64);
    if (!pdDst.IsValid()) {
        string err = "Can not unpack 'dst' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        string err = "Can not unpack peglevel";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    string err;
    
    bool ok = pegops::movereserve(
            inp_move_reserve,
            pdSrc,
            pdDst,
            peglevel,
            
            err);
    
    if (!ok) {
        throw JSONRPCError(RPC_MISC_ERROR, err);
    }
    
    Object result;
    
    result.push_back(Pair("cycle", peglevel.nCycle));
    
    printpeglevel(peglevel, result);
    printpegbalance(pdSrc, result, "src_");
    printpegbalance(pdDst, result, "dst_");
    
    return result;
}

Value removecoins(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "removecoins "
                "<from_pegdata_base64> "
                "<remove_pegdata_base64>\n"
            );
    
    string inp_from_pegdata64 = params[0].get_str();
    string inp_remove_pegdata64 = params[1].get_str();
    
    CPegData pdFrom(inp_from_pegdata64);
    if (!pdFrom.IsValid()) {
        string err = "Can not unpack 'from' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }

    CPegData pdRemove(inp_remove_pegdata64);
    if (!pdRemove.IsValid()) {
        string err = "Can not unpack 'remove' pegdata";
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
    }
    
    pdFrom.fractions    -= pdRemove.fractions;
    pdFrom.nLiquid      -= pdRemove.nLiquid;
    pdFrom.nReserve     -= pdRemove.nReserve;
    
    Object result;
    
    printpegbalance(pdFrom, result, "out_");
    
    return result;
}
