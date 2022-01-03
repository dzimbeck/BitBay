// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#ifndef BITBAY_PEGOPS_H
#define BITBAY_PEGOPS_H

/**
  * External API
  * The use of the API requires only std::string.
  * No internal classes declarations to be exposed.
  */

#include <string>
#include <vector>
#include <tuple>

namespace pegops {

extern bool getpeglevel(
        int                 inp_cycle_now,
        int                 inp_cycle_prev,
        int                 inp_buffer,
        int                 inp_peg_now,
        int                 inp_peg_next,
        int                 inp_peg_next_next,
        const std::string & inp_exchange_pegdata64,
        const std::string & inp_pegshift_pegdata64,

        std::string &   out_peglevel_hex,
        int64_t     &   out_exchange_liquid,
        int64_t     &   out_exchange_reserve,
        std::string &   out_pegpool_pegdata64,
        int64_t     &   out_pegpool_amount,
        std::string &   out_err);

extern bool getpeglevelinfo(
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
        std::string &   out_err);

extern bool getpegdatainfo(
        const std::string & inp_pegdata64,

        int &           out_version,
        int64_t &       out_value,
        int64_t &       out_liquid,
        int64_t &       out_reserve,
        int16_t &       out_value_hli,
        int16_t &       out_liquid_hli,
        int16_t &       out_reserve_hli,
        int32_t &       out_id,
        // peglevel
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
        std::string &   out_err);

extern bool updatepegbalances(
        const std::string & inp_balance_pegdata64,
        const std::string & inp_pegpool_pegdata64,
        const std::string & inp_peglevel_hex,

        std::string &   out_balance_pegdata64,
        int64_t     &   out_balance_liquid,
        int64_t     &   out_balance_reserve,
        std::string &   out_pegpool_pegdata64,
        int64_t     &   out_pegpool_amount,
        std::string &   out_err);

extern bool movecoins(
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
        std::string &   out_err);

extern bool moveliquid(
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
        std::string &   out_err);

extern bool movereserve(
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
        std::string &   out_err);

extern bool removecoins(
        const std::string & inp_from_pegdata64,
        const std::string & inp_remove_pegdata64,

        std::string &   out_from_pegdata64,
        int64_t     &   out_from_liquid,
        int64_t     &   out_from_reserve,
        std::string &   out_err);

extern bool updatetxout(
        const std::string       inp_txout_pegdata64,
        const std::string &     inp_peglevel_hex,

        int64_t     &   out_txout_value,
        int64_t     &   out_txout_next_cycle_available_liquid,
        int64_t     &   out_txout_next_cycle_available_reserve,
        int16_t     &   out_txout_value_hli,
        int16_t     &   out_txout_next_cycle_available_liquid_hli,
        int16_t     &   out_txout_next_cycle_available_reserve_hli,
        std::string &   out_err);

typedef std::vector<std::tuple<std::string,std::string,std::string>> txinps;
typedef std::vector<std::tuple<std::string,std::string,std::string>> txouts;

extern bool prepareliquidwithdraw(
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
        std::string &   out_err);

extern bool preparereservewithdraw(
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
        std::string &   out_err);

}

#endif // BITBAY_PEGOPS_H
