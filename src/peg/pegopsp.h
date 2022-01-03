// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#ifndef BITBAY_PEGOPSP_H
#define BITBAY_PEGOPSP_H

/**
  * Internal API
  * The use of the API requires includes for classes
  * CFractions, CPegLevel, CPegData for declarations.
  */

#include <string>
#include <tuple>
#include <vector>

class CPegData;
class CPegLevel;

namespace pegops {

extern bool getpeglevel(
        int                 nCycleNow,
        int                 nCyclePrev,
        int                 nBuffer,
        int                 nPegNow,
        int                 nPegNext,
        int                 nPegNextNext,
        const CPegData &    pdExchange,
        const CPegData &    pdPegShift,

        CPegLevel &     peglevel,
        CPegData &      pdPegPool,
        std::string &   sErr);

extern bool updatepegbalances(
        CPegData &          pdBalance,
        CPegData &          pdPegPool,
        const CPegLevel &   peglevelNew,

        std::string &   sErr);

extern bool movecoins(
        int64_t             nMoveAmount,
        CPegData &          pdSrc,
        CPegData &          pdDst,
        const CPegLevel &   peglevel,
        bool                fCrossCycles,

        std::string &   sErr);

extern bool moveliquid(
        int64_t             nMoveAmount,
        CPegData &          pdSrc,
        CPegData &          pdDst,
        const CPegLevel &   peglevel,

        std::string &   sErr);

extern bool movereserve(
        int64_t             nMoveAmount,
        CPegData &          pdSrc,
        CPegData &          pdDst,
        const CPegLevel &   peglevel,

        std::string &   sErr);

extern bool prepareliquidwithdraw(
        const std::vector<
            std::tuple<
                std::string,
                CPegData,
                std::string>> & txins,
        CPegData &              pdBalance,
        CPegData &              pdExchange,
        CPegData &              pdPegShift,
        int64_t                 nAmountWithFee,
        std::string             sAddress,
        const CPegLevel &       peglevel,

        CPegData &              pdRequested,
        CPegData &              pdProcessed,
        std::string &           withdrawIdXch,
        std::string &           withdrawTxout,
        std::string &           rawtx,
        std::vector<
            std::tuple<
                std::string,
                CPegData,
                std::string>> & txouts,

        std::string &   sErr);

extern bool preparereservewithdraw(
        const std::vector<
            std::tuple<
                std::string,
                CPegData,
                std::string>> & txins,
        CPegData &              pdBalance,
        CPegData &              pdExchange,
        CPegData &              pdPegShift,
        int64_t                 nAmountWithFee,
        std::string             sAddress,
        const CPegLevel &       peglevel,

        CPegData &              pdRequested,
        CPegData &              pdProcessed,
        std::string &           withdrawIdXch,
        std::string &           withdrawTxout,
        std::string &           rawtx,
        std::vector<
            std::tuple<
                std::string,
                CPegData,
                std::string>> & txouts,

        std::string &   sErr);

}

#endif // BITBAY_PEGOPSP_H
