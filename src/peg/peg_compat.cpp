// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#include <map>
#include <set>
#include <cstdint>

#include "pegdata.h"
#include "main.h"
#include "base58.h"

using namespace std;

static string sBurnAddress =
    "bJnV8J5v74MGctMyVSVPfGu1mGQ9nMTiB3";

static string toAddress(const CScript& scriptPubKey,
                        bool* ptrIsNotary = nullptr,
                        string* ptrNotary = nullptr) {
    int nRequired;
    txnouttype type;
    vector<CTxDestination> addresses;
    if (ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        std::string str_addr_all;
        bool fNone = true;
        for(const CTxDestination& addr : addresses) {
            std::string str_addr = CBitcoinAddress(addr).ToString();
            if (!str_addr_all.empty())
                str_addr_all += "\n";
            str_addr_all += str_addr;
            fNone = false;
        }
        if (!fNone)
            return str_addr_all;
    }

    if (ptrNotary || ptrIsNotary) {
        if (ptrIsNotary) *ptrIsNotary = false;
        if (ptrNotary) *ptrNotary = "";

        opcodetype opcode1;
        vector<unsigned char> vch1;
        CScript::const_iterator pc1 = scriptPubKey.begin();
        if (scriptPubKey.GetOp(pc1, opcode1, vch1)) {
            if (opcode1 == OP_RETURN && scriptPubKey.size()>1) {
                if (ptrIsNotary) *ptrIsNotary = true;
                if (ptrNotary) {
                    unsigned long len_bytes = scriptPubKey[1];
                    if (len_bytes > scriptPubKey.size()-2)
                        len_bytes = scriptPubKey.size()-2;
                    for (uint32_t i=0; i< len_bytes; i++) {
                        ptrNotary->push_back(char(scriptPubKey[i+2]));
                    }
                }
            }
        }
    }

    string as_bytes;
    unsigned long len_bytes = scriptPubKey.size();
    for(unsigned int i=0; i< len_bytes; i++) {
        as_bytes += char(scriptPubKey[i]);
    }
    return as_bytes;
}

bool CalculateStakingFractions_testnet200k(const CTransaction & tx,
                                           const CBlockIndex* pindexBlock,
                                           MapPrevTx & inputs,
                                           MapFractions& fInputs,
                                           std::map<uint256, CTxIndex>& mapTestPool,
                                           MapFractions& mapTestFractionsPool,
                                           const CFractions& feesFractions,
                                           int64_t nCalculatedStakeRewardWithoutFees,
                                           std::string& sFailCause)
{
    size_t n_vin = tx.vin.size();
    size_t n_vout = tx.vout.size();

    if (n_vin != 1) {
        sFailCause = "More than one input";
        return false;
    }

    if (n_vout > 8) {
        sFailCause = "More than 8 outputs";
        return false;
    }

    int64_t nValueIn = 0;
    int64_t nReservesTotal =0;
    int64_t nLiquidityTotal =0;

    map<string, CFractions> poolReserves;
    map<string, CFractions> poolLiquidity;

    int nSupply = pindexBlock->nPegSupplyIndex;
    string sInputAddress;
    CFractions frInp(0, CFractions::STD);

    // only one input
    {
        unsigned int i = 0;
        const COutPoint & prevout = tx.vin[i].prevout;
        CTransaction& txPrev = inputs[prevout.hash].second;
        if (prevout.n >= txPrev.vout.size()) {
            sFailCause = "PI02: Refered output out of range";
            return false;
        }

        int64_t nValue = txPrev.vout[prevout.n].nValue;
        nValueIn += nValue;
        auto sAddress = toAddress(txPrev.vout[prevout.n].scriptPubKey);
        sInputAddress = sAddress;

        auto fkey = uint320(prevout.hash, prevout.n);
        if (fInputs.find(fkey) == fInputs.end()) {
            sFailCause = "PI03: No input fractions found";
            return false;
        }

        frInp = fInputs[fkey].Std();
        if (frInp.Total() != txPrev.vout[prevout.n].nValue) {
            sFailCause = "PI04: Input fraction total mismatches value";
            return false;
        }

        int64_t nReserveIn = 0;
        auto & frReserve = poolReserves[sAddress];
        frReserve += frInp.LowPart(nSupply, &nReserveIn);

        int64_t nLiquidityIn = 0;
        auto & frLiquidity = poolLiquidity[sAddress];
        frLiquidity += frInp.HighPart(nSupply, &nLiquidityIn);

        nReservesTotal += nReserveIn;
        nLiquidityTotal += nLiquidityIn;
    }

    // Check funds to be returned to same address
    int64_t nValueReturn = 0;
    for (unsigned int i = 0; i < n_vout; i++) {
        std::string sAddress = toAddress(tx.vout[i].scriptPubKey);
        if (sInputAddress == sAddress) {
            nValueReturn += tx.vout[i].nValue;
        }
    }
    if (nValueReturn < nValueIn) {
        sFailCause = "PI05: No enough funds returned to input address";
        return false;
    }

    CFractions frCommonLiquidity(0, CFractions::STD);
    for(const auto & item : poolLiquidity) {
        frCommonLiquidity += item.second;
    }

    CFractions fStakeReward(nCalculatedStakeRewardWithoutFees, CFractions::STD);

    frCommonLiquidity += fStakeReward.HighPart(nSupply, &nLiquidityTotal);
    frCommonLiquidity += feesFractions.HighPart(nSupply, &nLiquidityTotal);

    auto & frInputReserve = poolReserves[sInputAddress];
    frInputReserve += fStakeReward.LowPart(nSupply, &nReservesTotal);
    frInputReserve += feesFractions.LowPart(nSupply, &nReservesTotal);

    int64_t nValueOut = 0;
    int64_t nCommonLiquidity = nLiquidityTotal;

    bool fFailedPegOut = false;
    unsigned int nLatestPegOut = 0;

    // Calculation of outputs
    for (unsigned int i = 0; i < n_vout; i++)
    {
        nLatestPegOut = i;
        int64_t nValue = tx.vout[i].nValue;
        nValueOut += nValue;

        auto fkey = uint320(tx.GetHash(), i);
        auto & frOut = mapTestFractionsPool[fkey];

        string sNotary;
        bool fNotary = false;
        auto sAddress = toAddress(tx.vout[i].scriptPubKey, &fNotary, &sNotary);

        // for output returning on same address and greater or equal value
        if (nValue >= nValueIn && sInputAddress == sAddress) {
            if (frInp.nFlags & CFractions::NOTARY_F) {
                frOut.nFlags |= CFractions::NOTARY_F;
                frOut.nLockTime = frInp.nLockTime;
            }
            if (frInp.nFlags & CFractions::NOTARY_V) {
                frOut.nFlags |= CFractions::NOTARY_V;
                frOut.nLockTime = frInp.nLockTime;
            }
        }

        if (poolReserves.count(sAddress)) { // to reserve
            int64_t nValueLeft = nValue;
            auto & frReserve = poolReserves[sAddress];
            nValueLeft = frReserve.MoveRatioPartTo(nValueLeft, frOut);

            if (nValueLeft > 0) {
                if (nValueLeft > nCommonLiquidity) {
                    sFailCause = "PO01: No liquidity left";
                    fFailedPegOut = true;
                    break;
                }
                frCommonLiquidity.MoveRatioPartTo(nValueLeft, frOut);
                nCommonLiquidity -= nValueLeft;
            }
        }
        else { // move liquidity out
            if (sAddress == sBurnAddress || fNotary) {

                vector<string> vAddresses;
                for(auto it = poolReserves.begin(); it != poolReserves.end(); it++) {
                    vAddresses.push_back(it->first);
                }

                int64_t nValueLeft = nValue;
                for(const string & sAddress : vAddresses) {
                    auto & frReserve = poolReserves[sAddress];
                    nValueLeft = frReserve.MoveRatioPartTo(nValueLeft, frOut);
                    if (nValueLeft == 0) {
                        break;
                    }
                }

                if (nValueLeft > 0) {
                    if (nValueLeft > nCommonLiquidity) {
                        sFailCause = "PO02: No liquidity left";
                        fFailedPegOut = true;
                        break;
                    }
                    frCommonLiquidity.MoveRatioPartTo(nValueLeft, frOut);
                    nCommonLiquidity -= nValueLeft;
                }
            }
            else {
                if (nValue > nCommonLiquidity) {
                    sFailCause = "PO03: No liquidity left";
                    fFailedPegOut = true;
                    break;
                }
                frCommonLiquidity.MoveRatioPartTo(nValue, frOut);
                nCommonLiquidity -= nValue;
            }
        }
    }

    if (!fFailedPegOut) {
        // lets do some extra checks for totals
        for (unsigned int i = 0; i < n_vout; i++)
        {
            auto fkey = uint320(tx.GetHash(), i);
            auto f = mapTestFractionsPool[fkey];
            int64_t nValue = tx.vout[i].nValue;
            if (nValue != f.Total() || !f.IsPositive()) {
                sFailCause = "PO04: Total mismatch on output "+std::to_string(i);
                fFailedPegOut = true;
                break;
            }
        }
    }

    if (fFailedPegOut) {
        // for now remove failed fractions from pool so they
        // are not written to db
        auto fkey = uint320(tx.GetHash(), nLatestPegOut);
        if (mapTestFractionsPool.count(fkey)) {
            auto it = mapTestFractionsPool.find(fkey);
            mapTestFractionsPool.erase(it);
        }
        return false;
    }

    return true;
}

