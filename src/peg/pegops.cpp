// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#include "pegops.h"
#include "pegopsp.h"
#include "pegdata.h"

#include <map>
#include <set>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <type_traits>
#include <iostream>

#include <boost/multiprecision/cpp_int.hpp>

#include <zconf.h>
#include <zlib.h>

using namespace std;
using namespace boost;

namespace pegops {

// API calls

bool getpeglevel(
        int                 inp_cycle_now,
        int                 inp_cycle_prev,
        int                 inp_buffer,
        int                 inp_peg_now,
        int                 inp_peg_next,
        int                 inp_peg_next_next,
        const std::string & inp_exchange_pegdata64,
        const std::string & inp_pegshift_pegdata64,

        std::string & out_peglevel_hex,
        int64_t     & out_exchange_liquid,
        int64_t     & out_exchange_reserve,
        std::string & out_pegpool_pegdata64,
        int64_t     & out_pegpool_amount,
        std::string & out_err)
{
    out_err.clear();

    CPegData pdExchange(inp_exchange_pegdata64);
    if (!pdExchange.IsValid()) {
        out_err = "Can not unpack 'exchange' pegdata";
        return false;
    }

    CPegData pdPegShift(inp_pegshift_pegdata64);
    if (!pdPegShift.IsValid()) {
        out_err = "Can not unpack 'pegshift' pegdata";
        return false;
    }

    CPegData pdPegPool;
    CPegLevel peglevel;

    getpeglevel(inp_cycle_now,
                inp_cycle_prev,
                inp_buffer,
                inp_peg_now,
                inp_peg_next,
                inp_peg_next_next,
                pdExchange,
                pdPegShift,

                peglevel,
                pdPegPool,
                out_err);

    if (!pdPegPool.IsValid()) {
        out_err = "Returned invalid 'pegpool' pegdata";
        return false;
    }

    out_peglevel_hex = peglevel.ToString();
    out_pegpool_pegdata64 = pdPegPool.ToString();
    out_pegpool_amount    = pdPegPool.nLiquid+pdPegPool.nReserve;

    out_exchange_liquid   = out_pegpool_amount;
    out_exchange_reserve  = pdExchange.fractions.Total()-out_pegpool_amount;

    return true;
}

bool getpeglevelinfo(
        const std::string & inp_peglevel_hex,

        int &           out_version,
        int &           out_cycle_now,
        int &           out_cycle_prev,
        int &           out_buffer,
        int &           out_peg_now,
        int &           out_peg_next,
        int &           out_peg_next_next,
        int &           out_shift,
        int64_t &       out_shiftlastpart,
        int64_t &       out_shiftlasttotal,
        std::string &   out_err)
{
    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        out_err = "Can not unpack peglevel";
        return false;
    }

    out_version         = peglevel.nVersion;
    out_cycle_now       = peglevel.nCycle;
    out_cycle_prev      = peglevel.nCyclePrev;
    out_buffer          = peglevel.nBuffer;
    out_peg_now         = peglevel.nSupply;
    out_peg_next        = peglevel.nSupplyNext;
    out_peg_next_next   = peglevel.nSupplyNextNext;
    out_shift           = peglevel.nShift;
    out_shiftlastpart   = peglevel.nShiftLastPart;
    out_shiftlasttotal  = peglevel.nShiftLastTotal;

    return true;
}

bool getpegdatainfo(
        const std::string & inp_pegdata64,

        int &           out_version,
        int64_t &       out_value,
        int64_t &       out_liquid,
        int64_t &       out_reserve,
        int16_t &       out_value_hli,
        int16_t &       out_liquid_hli,
        int16_t &       out_reserve_hli,
        int32_t &       out_id,
        int &           out_level_version,
        int &           out_cycle_now,
        int &           out_cycle_prev,
        int &           out_buffer,
        int &           out_peg_now,
        int &           out_peg_next,
        int &           out_peg_next_next,
        int &           out_shift,
        int64_t &       out_shiftlastpart,
        int64_t &       out_shiftlasttotal,
        std::string &   out_err)
{
    CPegData pd(inp_pegdata64);
    if (!pd.IsValid()) {
        out_err = "Can not unpack pegdata";
        return false;
    }

    out_version     = pd.nVersion;
    out_value       = pd.fractions.Total();
    out_liquid      = pd.nLiquid;
    out_reserve     = pd.nReserve;
    out_value_hli   = pd.fractions.HLI();
    out_liquid_hli  = pd.fractions.HighPart(pd.peglevel, nullptr).HLI();
    out_reserve_hli = pd.fractions.LowPart(pd.peglevel, nullptr).HLI();
    out_id          = pd.nId;

    out_level_version   = pd.peglevel.nVersion;
    out_cycle_now       = pd.peglevel.nCycle;
    out_cycle_prev      = pd.peglevel.nCyclePrev;
    out_buffer          = pd.peglevel.nBuffer;
    out_peg_now         = pd.peglevel.nSupply;
    out_peg_next        = pd.peglevel.nSupplyNext;
    out_peg_next_next   = pd.peglevel.nSupplyNextNext;
    out_shift           = pd.peglevel.nShift;
    out_shiftlastpart   = pd.peglevel.nShiftLastPart;
    out_shiftlasttotal  = pd.peglevel.nShiftLastTotal;

    return true;
}

bool updatepegbalances(
        const std::string & inp_balance_pegdata64,
        const std::string & inp_pegpool_pegdata64,
        const std::string & inp_peglevel_hex,

        std::string &   out_balance_pegdata64,
        int64_t     &   out_balance_liquid,
        int64_t     &   out_balance_reserve,
        std::string &   out_pegpool_pegdata64,
        int64_t     &   out_pegpool_amount,
        std::string &   out_err)
{
    out_err.clear();

    CPegData pdBalance(inp_balance_pegdata64);
    if (!pdBalance.IsValid()) {
        out_err = "Can not unpack 'balance' pegdata";
        return false;
    }

    CPegData pdPegPool(inp_pegpool_pegdata64);
    if (!pdPegPool.IsValid()) {
        out_err = "Can not unpack 'pegpool' pegdata";
        return false;
    }

    CPegLevel peglevelNew(inp_peglevel_hex);
    if (!peglevelNew.IsValid()) {
        out_err = "Can not unpack peglevel";
        return false;
    }

    if (pdBalance.peglevel.nCycle == peglevelNew.nCycle) { // already up-to-dated
        out_pegpool_pegdata64 = inp_pegpool_pegdata64;
        out_balance_pegdata64 = inp_balance_pegdata64;
        out_balance_liquid    = pdBalance.nLiquid;
        out_balance_reserve   = pdBalance.nReserve;
        out_pegpool_amount    = pdPegPool.nLiquid+pdPegPool.nReserve;
        out_err = "Already up-to-dated";
        return true;
    }

    bool ok = updatepegbalances(pdBalance,
                                pdPegPool,
                                peglevelNew,
                                out_err);
    if (!ok) {
        return false;
    }

    if (!pdPegPool.IsValid()) {
        out_err = "Returned invalid 'pegpool' pegdata";
        return false;
    }
    if (!pdBalance.IsValid()) {
        out_err = "Returned invalid 'balance' pegdata";
        return false;
    }

    out_pegpool_pegdata64   = pdPegPool.ToString();
    out_pegpool_amount      = pdPegPool.nLiquid+pdPegPool.nReserve;
    out_balance_pegdata64   = pdBalance.ToString();
    out_balance_liquid      = pdBalance.nLiquid;
    out_balance_reserve     = pdBalance.nReserve;

    return true;
}

bool movecoins(
        int64_t             inp_move_amount,
        const std::string & inp_src_pegdata64,
        const std::string & inp_dst_pegdata64,
        const std::string & inp_peglevel_hex,
        bool                inp_cross_cycles,

        std::string &   out_src_pegdata64,
        int64_t     &   out_src_liquid,
        int64_t     &   out_src_reserve,
        std::string &   out_dst_pegdata64,
        int64_t     &   out_dst_liquid,
        int64_t     &   out_dst_reserve,
        std::string &   out_err)
{
    out_err.clear();

    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        out_err = "Can not unpack peglevel";
        return false;
    }

    CPegData pdSrc(inp_src_pegdata64);
    if (!pdSrc.IsValid()) {
        out_err = "Can not unpack 'src' pegdata";
        return false;
    }

    CPegData pdDst(inp_dst_pegdata64);
    if (!pdDst.IsValid()) {
        out_err = "Can not unpack 'dst' pegdata";
        return false;
    }

    bool ok = movecoins(inp_move_amount,
                        pdSrc,
                        pdDst,
                        peglevel,
                        inp_cross_cycles,
                        out_err);
    if (!ok) {
        return false;
    }

    if (!pdSrc.IsValid()) {
        out_err = "Returned invalid 'src' pegdata";
        return false;
    }
    if (!pdDst.IsValid()) {
        out_err = "Returned invalid 'dst' pegdata";
        return false;
    }

    out_src_pegdata64   = pdSrc.ToString();
    out_src_liquid      = pdSrc.nLiquid;
    out_src_reserve     = pdSrc.nReserve;

    out_dst_pegdata64   = pdDst.ToString();
    out_dst_liquid      = pdDst.nLiquid;
    out_dst_reserve     = pdDst.nReserve;

    return true;
}

bool moveliquid(
        int64_t             inp_move_liquid,
        const std::string & inp_src_pegdata64,
        const std::string & inp_dst_pegdata64,
        const std::string & inp_peglevel_hex,

        std::string &   out_src_pegdata64,
        int64_t     &   out_src_liquid,
        int64_t     &   out_src_reserve,
        std::string &   out_dst_pegdata64,
        int64_t     &   out_dst_liquid,
        int64_t     &   out_dst_reserve,
        std::string &   out_err)
{
    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        out_err = "Can not unpack peglevel";
        return false;
    }

    CPegData pdSrc(inp_src_pegdata64);
    if (!pdSrc.IsValid()) {
        out_err = "Can not unpack 'src' pegdata";
        return false;
    }

    CPegData pdDst(inp_dst_pegdata64);
    if (!pdDst.IsValid()) {
        out_err = "Can not unpack 'dst' pegdata";
        return false;
    }

    bool ok = moveliquid(inp_move_liquid,
                         pdSrc,
                         pdDst,
                         peglevel,
                         out_err);
    if (!ok) {
        return false;
    }

    if (!pdSrc.IsValid()) {
        out_err = "Returned invalid 'src' pegdata";
        return false;
    }
    if (!pdDst.IsValid()) {
        out_err = "Returned invalid 'dst' pegdata";
        return false;
    }

    out_src_pegdata64   = pdSrc.ToString();
    out_src_liquid      = pdSrc.nLiquid;
    out_src_reserve     = pdSrc.nReserve;

    out_dst_pegdata64   = pdDst.ToString();
    out_dst_liquid      = pdDst.nLiquid;
    out_dst_reserve     = pdDst.nReserve;

    return true;
}

bool movereserve(
        int64_t             inp_move_reserve,
        const std::string & inp_src_pegdata64,
        const std::string & inp_dst_pegdata64,
        const std::string & inp_peglevel_hex,

        std::string &   out_src_pegdata64,
        int64_t     &   out_src_liquid,
        int64_t     &   out_src_reserve,
        std::string &   out_dst_pegdata64,
        int64_t     &   out_dst_liquid,
        int64_t     &   out_dst_reserve,
        std::string &   out_err)
{
    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        out_err = "Can not unpack peglevel";
        return false;
    }

    CPegData pdSrc(inp_src_pegdata64);
    if (!pdSrc.IsValid()) {
        out_err = "Can not unpack 'src' pegdata";
        return false;
    }

    CPegData pdDst(inp_dst_pegdata64);
    if (!pdDst.IsValid()) {
        out_err = "Can not unpack 'dst' pegdata";
        return false;
    }

    bool ok = movereserve(inp_move_reserve,
                          pdSrc,
                          pdDst,
                          peglevel,
                          out_err);
    if (!ok) {
        return false;
    }

    if (!pdSrc.IsValid()) {
        out_err = "Returned invalid 'src' pegdata";
        return false;
    }
    if (!pdDst.IsValid()) {
        out_err = "Returned invalid 'dst' pegdata";
        return false;
    }

    out_src_pegdata64   = pdSrc.ToString();
    out_src_liquid      = pdSrc.nLiquid;
    out_src_reserve     = pdSrc.nReserve;

    out_dst_pegdata64   = pdDst.ToString();
    out_dst_liquid      = pdDst.nLiquid;
    out_dst_reserve     = pdDst.nReserve;

    return true;
}

bool removecoins(
        const std::string & inp_from_pegdata64,
        const std::string & inp_remove_pegdata64,

        std::string &   out_from_pegdata64,
        int64_t     &   out_from_liquid,
        int64_t     &   out_from_reserve,
        std::string &   out_err)
{
    CPegData pdFrom(inp_from_pegdata64);
    if (!pdFrom.IsValid()) {
        out_err = "Can not unpack 'from' pegdata";
        return false;
    }

    CPegData pdRemove(inp_remove_pegdata64);
    if (!pdRemove.IsValid()) {
        out_err = "Can not unpack 'remove' pegdata";
        return false;
    }

    pdFrom.fractions    -= pdRemove.fractions;
    pdFrom.nLiquid      -= pdRemove.nLiquid;
    pdFrom.nReserve     -= pdRemove.nReserve;

    if (!pdFrom.IsValid()) {
        out_err = "Returned invalid 'from' pegdata";
        return false;
    }

    out_from_pegdata64  = pdFrom.ToString();
    out_from_liquid     = pdFrom.nLiquid;
    out_from_reserve    = pdFrom.nReserve;

    return true;
}

bool updatetxout(
        const std::string       inp_txout_pegdata64,
        const std::string &     inp_peglevel_hex,

        int64_t     &   out_txout_value,
        int64_t     &   out_txout_next_cycle_available_liquid,
        int64_t     &   out_txout_next_cycle_available_reserve,
        int16_t     &   out_txout_value_hli,
        int16_t     &   out_txout_next_cycle_available_liquid_hli,
        int16_t     &   out_txout_next_cycle_available_reserve_hli,
        std::string &   out_err)
{
    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        out_err = "Can not unpack peglevel";
        return false;
    }

    CPegData pdTxout(inp_txout_pegdata64);
    if (!pdTxout.IsValid()) {
        out_err = "Can not unpack 'from' pegdata";
        return false;
    }

    out_txout_value = pdTxout.fractions.Total();

    // network peg in next cycle (without buffer)
    int pegn_liquid = pdTxout.peglevel.nSupplyNext - pdTxout.peglevel.nBuffer;
    int pegn_reserve = pdTxout.peglevel.nSupplyNext;

    out_txout_next_cycle_available_liquid = pdTxout.fractions.High(pegn_liquid);
    out_txout_next_cycle_available_reserve = pdTxout.fractions.Low(pegn_reserve);

    out_txout_value_hli = pdTxout.fractions.HLI();
    out_txout_next_cycle_available_liquid_hli = pdTxout.fractions.HighPart(pegn_liquid, nullptr).HLI();
    out_txout_next_cycle_available_reserve_hli = pdTxout.fractions.LowPart(pegn_reserve, nullptr).HLI();

    return true;
}

bool prepareliquidwithdraw(
        const txinps &          inp_txinps,
        const std::string       inp_balance_pegdata64,
        const std::string       inp_exchange_pegdata64,
        const std::string       inp_pegshift_pegdata64,
        int64_t                 inp_amount_with_fee,
        std::string             inp_address,
        const std::string &     inp_peglevel_hex,

        std::string &   out_balance_pegdata64,
        int64_t     &   out_balance_liquid,
        int64_t     &   out_balance_reserve,
        std::string &   out_exchange_pegdata64,
        int64_t     &   out_exchange_liquid,
        int64_t     &   out_exchange_reserve,
        std::string &   out_pegshift_pegdata64,
        std::string &   out_requested_pegdata64,
        std::string &   out_processed_pegdata64,
        std::string &   out_withdraw_idxch,
        std::string &   out_withdraw_txout,
        std::string &   out_rawtx,
        txouts &        out_txouts,
        std::string &   out_err)
{
    out_err.clear();

    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        out_err = "Can not unpack peglevel";
        return false;
    }

    CPegData pdBalance(inp_balance_pegdata64);
    if (!pdBalance.IsValid()) {
        out_err = "Can not unpack 'exchange' pegdata";
        return false;
    }

    CPegData pdExchange(inp_exchange_pegdata64);
    if (!pdExchange.IsValid()) {
        out_err = "Can not unpack 'exchange' pegdata";
        return false;
    }

    CPegData pdPegShift(inp_pegshift_pegdata64);
    if (!pdPegShift.IsValid()) {
        out_err = "Can not unpack 'pegshift' pegdata";
        return false;
    }

    CPegData pdRequested;
    CPegData pdProcessed;

    std::vector<
        std::tuple<
            std::string,
            CPegData,
            std::string>> txIns;

    for(const std::tuple<string,string,string> & txinp : inp_txinps) {
        string txout_id;
        string txout_pegdata64;
        string txout_privkey_bip32;
        std::tie(txout_id,txout_pegdata64,txout_privkey_bip32) = txinp;
        CPegData pdTxout(txout_pegdata64);
        if (!pdTxout.IsValid()) {
            out_err = "Can not unpack 'txout' pegdata";
            return false;
        }
        txIns.push_back(make_tuple(txout_id, pdTxout, txout_privkey_bip32));
    }

    std::vector<
        std::tuple<
            std::string,
            CPegData,
            std::string>> txOuts;

    bool ok = prepareliquidwithdraw(
                txIns,
                pdBalance,
                pdExchange,
                pdPegShift,
                inp_amount_with_fee,
                inp_address,
                peglevel,
                pdRequested,
                pdProcessed,
                out_withdraw_idxch,
                out_withdraw_txout,
                out_rawtx,
                txOuts,
                out_err);

    if (!ok) {
        return false;
    }

    if (!pdBalance.IsValid()) {
        out_err = "Returned invalid 'balance' pegdata";
        return false;
    }
    if (!pdExchange.IsValid()) {
        out_err = "Returned invalid 'exchange' pegdata";
        return false;
    }
    if (!pdPegShift.IsValid()) {
        out_err = "Returned invalid 'pegshift' pegdata";
        return false;
    }
    if (!pdRequested.IsValid()) {
        out_err = "Returned invalid 'requested' pegdata";
        return false;
    }
    if (!pdProcessed.IsValid()) {
        out_err = "Returned invalid 'processed' pegdata";
        return false;
    }

    out_balance_pegdata64   = pdBalance.ToString();
    out_balance_liquid      = pdBalance.nLiquid;
    out_balance_reserve     = pdBalance.nReserve;
    out_exchange_pegdata64  = pdExchange.ToString();
    out_exchange_liquid     = pdExchange.nLiquid;
    out_exchange_reserve    = pdExchange.nReserve;
    out_pegshift_pegdata64  = pdPegShift.ToString();
    out_requested_pegdata64 = pdRequested.ToString();
    out_processed_pegdata64 = pdProcessed.ToString();

    for(const std::tuple<string,CPegData,string> & txOut : txOuts) {
        CPegData pdTxout = std::get<1>(txOut);
        if (!pdTxout.IsValid()) {
            out_err = "Returned invalid 'txout' pegdata";
            return false;
        }
        auto txout = make_tuple(std::get<0>(txOut),
                                pdTxout.ToString(),
                                std::get<2>(txOut));
        out_txouts.push_back(txout);
    }

    return true;
}

bool preparereservewithdraw(
        const txinps &          inp_txinps,
        const std::string       inp_balance_pegdata64,
        const std::string       inp_exchange_pegdata64,
        const std::string       inp_pegshift_pegdata64,
        int64_t                 inp_amount_with_fee,
        std::string             inp_address,
        const std::string &     inp_peglevel_hex,

        std::string &   out_balance_pegdata64,
        int64_t     &   out_balance_liquid,
        int64_t     &   out_balance_reserve,
        std::string &   out_exchange_pegdata64,
        int64_t     &   out_exchange_liquid,
        int64_t     &   out_exchange_reserve,
        std::string &   out_pegshift_pegdata64,
        std::string &   out_requested_pegdata64,
        std::string &   out_processed_pegdata64,
        std::string &   out_withdraw_idxch,
        std::string &   out_withdraw_txout,
        std::string &   out_rawtx,
        txouts &        out_txouts,
        std::string &   out_err)
{
    out_err.clear();

    CPegLevel peglevel(inp_peglevel_hex);
    if (!peglevel.IsValid()) {
        out_err = "Can not unpack peglevel";
        return false;
    }

    CPegData pdBalance(inp_balance_pegdata64);
    if (!pdBalance.IsValid()) {
        out_err = "Can not unpack 'exchange' pegdata";
        return false;
    }

    CPegData pdExchange(inp_exchange_pegdata64);
    if (!pdExchange.IsValid()) {
        out_err = "Can not unpack 'exchange' pegdata";
        return false;
    }

    CPegData pdPegShift(inp_pegshift_pegdata64);
    if (!pdPegShift.IsValid()) {
        out_err = "Can not unpack 'pegshift' pegdata";
        return false;
    }

    CPegData pdRequested;
    CPegData pdProcessed;

    std::vector<
        std::tuple<
            std::string,
            CPegData,
            std::string>> txIns;

    for(const std::tuple<string,string,string> & txinp : inp_txinps) {
        string txout_id;
        string txout_pegdata64;
        string txout_privkey_bip32;
        std::tie(txout_id,txout_pegdata64,txout_privkey_bip32) = txinp;
        CPegData pdTxout(txout_pegdata64);
        if (!pdTxout.IsValid()) {
            out_err = "Can not unpack 'txout' pegdata";
            return false;
        }
        txIns.push_back(make_tuple(txout_id, pdTxout, txout_privkey_bip32));
    }

    std::vector<
        std::tuple<
            std::string,
            CPegData,
            std::string>> txOuts;

    bool ok = preparereservewithdraw(
                txIns,
                pdBalance,
                pdExchange,
                pdPegShift,
                inp_amount_with_fee,
                inp_address,
                peglevel,
                pdRequested,
                pdProcessed,
                out_withdraw_idxch,
                out_withdraw_txout,
                out_rawtx,
                txOuts,
                out_err);

    if (!ok) {
        return false;
    }

    if (!pdBalance.IsValid()) {
        out_err = "Returned invalid 'balance' pegdata";
        return false;
    }
    if (!pdExchange.IsValid()) {
        out_err = "Returned invalid 'exchange' pegdata";
        return false;
    }
    if (!pdPegShift.IsValid()) {
        out_err = "Returned invalid 'pegshift' pegdata";
        return false;
    }
    if (!pdRequested.IsValid()) {
        out_err = "Returned invalid 'requested' pegdata";
        return false;
    }
    if (!pdProcessed.IsValid()) {
        out_err = "Returned invalid 'processed' pegdata";
        return false;
    }

    out_balance_pegdata64   = pdBalance.ToString();
    out_balance_liquid      = pdBalance.nLiquid;
    out_balance_reserve     = pdBalance.nReserve;
    out_exchange_pegdata64  = pdExchange.ToString();
    out_exchange_liquid     = pdExchange.nLiquid;
    out_exchange_reserve    = pdExchange.nReserve;
    out_pegshift_pegdata64  = pdPegShift.ToString();
    out_requested_pegdata64 = pdRequested.ToString();
    out_processed_pegdata64 = pdProcessed.ToString();

    for(const std::tuple<string,CPegData,string> & txOut : txOuts) {
        CPegData pdTxout = std::get<1>(txOut);
        if (!pdTxout.IsValid()) {
            out_err = "Returned invalid 'txout' pegdata";
            return false;
        }
        auto txout = make_tuple(std::get<0>(txOut),
                                pdTxout.ToString(),
                                std::get<2>(txOut));
        out_txouts.push_back(txout);
    }

    return true;
}

} // namespace
