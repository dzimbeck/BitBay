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

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

#include "pegstd.h"
#include "main.h"
#include "base58.h"

using namespace std;
using namespace boost;

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

bool CalculateStandardFractions(const CTransaction & tx,
                                int nSupply,
                                unsigned int nTime,
                                MapPrevOut & mapInputs,
                                MapFractions& mapInputsFractions,
                                MapFractions& mapTestFractionsPool,
                                CFractions& feesFractions,
                                std::string& sFailCause)
{
    size_t n_vin = tx.vin.size();
    size_t n_vout = tx.vout.size();

    int64_t nValueIn = 0;
    int64_t nReservesTotal =0;
    int64_t nLiquidityTotal =0;

    map<string, CFractions> poolReserves;
    map<string, CFractions> poolLiquidity;
    map<long, FrozenTxOut> poolFrozen;
    CFractions frColdFees(0, CFractions::VALUE);
    bool fFreezeAll = false;

    set<string> setInputAddresses;

    if (tx.IsCoinBase()) n_vin = 0;
    for (unsigned int i = 0; i < n_vin; i++)
    {
        const COutPoint & prevout = tx.vin[i].prevout;
        auto fkey = uint320(prevout.hash, prevout.n);
        if (!mapInputs.count(fkey)) {
            sFailCause = "P-I-1: Refered output is not found";
            return false;
        }
        const CTxOut & prevtxout = mapInputs[fkey];

        int64_t nValue = prevtxout.nValue;
        nValueIn += nValue;
        auto sAddress = toAddress(prevtxout.scriptPubKey);
        setInputAddresses.insert(sAddress);

        if (mapInputsFractions.find(fkey) == mapInputsFractions.end()) {
            sFailCause = "P-I-2: No input fractions found";
            return false;
        }

        auto frInp = mapInputsFractions[fkey].Std();
        if (frInp.Total() != prevtxout.nValue) {
            std::stringstream ss;
            ss << "P-I-3: Input fraction "
               << prevout.hash.GetHex() << ":" << prevout.n
               << " total " << frInp.Total()
               << " mismatches prevout value " << prevtxout.nValue;
            sFailCause = ss.str();
            return false;
        }

        if (frInp.nFlags & CFractions::NOTARY_F) {
            if (frInp.nLockTime > tx.nTime) {
                sFailCause = "P-I-4: Frozen input used before time expired";
                return false;
            }
        }

        if (frInp.nFlags & CFractions::NOTARY_V) {
            if (frInp.nLockTime > tx.nTime) {
                sFailCause = "P-I-5: Voluntary frozen input used before time expired";
                return false;
            }
        }

        int64_t nReserveIn = 0;
        auto & frReserve = poolReserves[sAddress];
        frReserve += frInp.LowPart(nSupply, &nReserveIn);

        int64_t nLiquidityIn = 0;
        auto & frLiquidity = poolLiquidity[sAddress];
        frLiquidity += frInp.HighPart(nSupply, &nLiquidityIn);

        // check if intend to transfer frozen
        // if so need to do appropriate deductions from pools
        // if there is a notary on same position as input
        if (i < n_vout) {
            string sNotary;
            bool fNotary = false;
            {
                opcodetype opcode1;
                vector<unsigned char> vch1;
                const CScript& script1 = tx.vout[i].scriptPubKey;
                CScript::const_iterator pc1 = script1.begin();
                if (script1.GetOp(pc1, opcode1, vch1)) {
                    if (opcode1 == OP_RETURN && script1.size()>1) {
                        unsigned long len_bytes = script1[1];
                        if (len_bytes > script1.size()-2)
                            len_bytes = script1.size()-2;
                        fNotary = true;
                        for (uint32_t i=0; i< len_bytes; i++) {
                            sNotary.push_back(char(script1[i+2]));
                        }
                    }
                }
            }

            if (fNotary && (frInp.nFlags & CFractions::NOTARY_C)) {
                sFailCause = "P-I-N-1: Can not notary for input cold coins";
                return false;
            }

            if (frInp.nFlags & CFractions::NOTARY_C) {
                // input is cold,
                // it can return back to original address
                // or go to any address but then get into frozen state
                unsigned int nOutIndex = i; // same index as input
                int64_t nValueOut = tx.vout[size_t(nOutIndex)].nValue;
                string sOutAddress = toAddress(tx.vout[size_t(nOutIndex)].scriptPubKey);
                auto & frozenTxOut = poolFrozen[nOutIndex];

                if (frozenTxOut.nValue >0 || frozenTxOut.fractions.Total() != 0) {
                    sFailCause = "P-I-C-1: Cold notary output has already assigned value";
                    return false;
                }

                CFractions frOut = frInp.RatioPart(nValueOut);
                frozenTxOut.nValue = nValueOut;
                frozenTxOut.fractions += frOut;
                frozenTxOut.fractions.nLockTime = 0;
                frozenTxOut.fIsColdOutput = true;

                if (frInp.sReturnAddr == sOutAddress) {
                    // no mark already on frozenTxOut.fractions
                } else {
                    if (!frozenTxOut.fractions.SetMark(CFractions::MARK_COLD_TO_FROZEN, CFractions::NOTARY_F, nTime)) {
                        sFailCause = "P-I-C-2: Crossing marks are detected";
                        return false;
                    }
                }

                // deduct whole frInp - not frOut
                // the diff can go only to fee fractions
                int64_t nReserveDeduct = 0;
                int64_t nLiquidityDeduct = 0;
                frReserve -= frInp.LowPart(nSupply, &nReserveDeduct);
                frLiquidity -= frInp.HighPart(nSupply, &nLiquidityDeduct);
                nReserveIn -= nReserveDeduct;
                nLiquidityIn -= nLiquidityDeduct;
                frColdFees += (frInp - frOut);
            }

            bool fNotaryF = boost::starts_with(sNotary, "**F**");
            bool fNotaryV = boost::starts_with(sNotary, "**V**");
            bool fNotaryL = boost::starts_with(sNotary, "**L**");
            bool fNotaryC = boost::starts_with(sNotary, "**C**");

            // #NOTE5
            if (fNotary && (fNotaryF || fNotaryV || fNotaryL || fNotaryC)) {
                bool fSharedFreeze = false;
                auto sOutputDef = sNotary.substr(5 /*length **F** */);
                vector<long> vFrozenIndexes;
                set<long> setFrozenIndexes;
                vector<string> vOutputArgs;
                boost::split(vOutputArgs, sOutputDef, boost::is_any_of(":"));

                if (fNotaryC && vOutputArgs.size() != 1) {
                    sFailCause = "P-I-N-2: Cold notary: refer more than one output";
                    return false;
                }

                for(string sOutputArg : vOutputArgs) {
                    char * pEnd = nullptr;
                    long nFrozenIndex = strtol(sOutputArg.c_str(), &pEnd, 0);
                    bool fValidIndex = !(pEnd == sOutputArg.c_str()) && nFrozenIndex >= 0 && size_t(nFrozenIndex) < n_vout;
                    if (!fValidIndex) {
                        sFailCause = "P-I-N-3: Freeze notary: not convertible to output index";
                        return false;
                    }
                    if (nFrozenIndex == i) {
                        sFailCause = "P-I-N-4: Freeze notary: output refers itself";
                        return false;
                    }

                    int64_t nFrozenValueOut = tx.vout[size_t(nFrozenIndex)].nValue;
                    auto & frozenTxOut = poolFrozen[nFrozenIndex];

                    if (frozenTxOut.fIsColdOutput) {
                        sFailCause = "P-I-N-5: Output already referenced as cold";
                        return false;
                    }

                    frozenTxOut.nValue = nFrozenValueOut;
                    frozenTxOut.sAddress = sAddress;
                    bool fMarkSet = false;
                    if (fNotaryF) fMarkSet = frozenTxOut.fractions.SetMark(CFractions::MARK_SET, CFractions::NOTARY_F, nTime);
                    if (fNotaryV) fMarkSet = frozenTxOut.fractions.SetMark(CFractions::MARK_SET, CFractions::NOTARY_V, nTime);
                    if (fNotaryL) fMarkSet = frozenTxOut.fractions.SetMark(CFractions::MARK_SET, CFractions::NOTARY_L, nTime);
                    if (fNotaryC) fMarkSet = frozenTxOut.fractions.SetMark(CFractions::MARK_SET, CFractions::NOTARY_C, nTime);
                    if (!fMarkSet) {
                        sFailCause = "P-I-N-6: Crossing marks are detected";
                        return false;
                    }
                    vFrozenIndexes.push_back(nFrozenIndex);
                    setFrozenIndexes.insert(nFrozenIndex);
                }

                if (vOutputArgs.size() > 1) {
                    fFreezeAll = true;
                    fSharedFreeze = true;
                }

                if (vFrozenIndexes.size() == 2) {
                    long nFrozenIndex1 = vFrozenIndexes.front();
                    long nFrozenIndex2 = vFrozenIndexes.back();
                    if (nFrozenIndex1 > nFrozenIndex2) {
                        swap(nFrozenIndex1, nFrozenIndex2);
                    }
                    auto & frozenTxOut = poolFrozen[nFrozenIndex1];
                    frozenTxOut.nFairWithdrawFromEscrowIndex1 = nFrozenIndex1;
                    frozenTxOut.nFairWithdrawFromEscrowIndex2 = nFrozenIndex2;
                }

                if (vFrozenIndexes.size() == 1) {
                    long nFrozenIndex = vFrozenIndexes.front();

                    int64_t nFrozenValueOut = tx.vout[size_t(nFrozenIndex)].nValue;
                    auto & frozenTxOut = poolFrozen[nFrozenIndex];

                    if (fNotaryF && nReserveIn < nFrozenValueOut) {
                        fFreezeAll = true;
                        fSharedFreeze = true;
                    }
                    else if (fNotaryV && nLiquidityIn < nFrozenValueOut) {
                        fFreezeAll = true;
                        fSharedFreeze = true;
                    }
                    else if (fNotaryL && nLiquidityIn < nFrozenValueOut) {
                        sFailCause = "P-I-N-7: Freeze notary: not enough input liquidity";
                        return false;
                    }
                    else if (fNotaryC && frInp.Total() < nFrozenValueOut) {
                        sFailCause = "P-I-N-8: Cold notary: not enough input value";
                        return false;
                    }

                    // deductions if not shared freeze
                    if (!fSharedFreeze) {
                        if (fNotaryF) {
                            CFractions frozenOut(0, CFractions::STD);
                            frReserve.MoveRatioPartTo(nFrozenValueOut, frozenOut);
                            frozenTxOut.fractions += frozenOut;
                            if (!frozenTxOut.fractions.SetMark(CFractions::MARK_SET, CFractions::NOTARY_F, nTime)) {
                                sFailCause = "P-I-N-9: Crossing marks are detected";
                                return false;
                            }
                            frInp -= frozenOut;
                            nReserveIn -= nFrozenValueOut;
                        }
                        else if (fNotaryV) {
                            CFractions frozenOut(0, CFractions::STD);
                            frLiquidity.MoveRatioPartTo(nFrozenValueOut, frozenOut);
                            frozenTxOut.fractions += frozenOut;
                            if (!frozenTxOut.fractions.SetMark(CFractions::MARK_SET, CFractions::NOTARY_V, nTime)) {
                                sFailCause = "P-I-N-10: Crossing marks are detected";
                                return false;
                            }
                            frInp -= frozenOut;
                            nLiquidityIn -= nFrozenValueOut;
                        }
                        else if (fNotaryL) {
                            CFractions frozenOut(0, CFractions::STD);
                            frLiquidity.MoveRatioPartTo(nFrozenValueOut, frozenOut);
                            frozenTxOut.fractions += frozenOut;
                            if (!frozenTxOut.fractions.SetMark(CFractions::MARK_SET, CFractions::NOTARY_L, nTime)) {
                                sFailCause = "P-I-N-11: Crossing marks are detected";
                                return false;
                            }
                            frInp -= frozenOut;
                            nLiquidityIn -= nFrozenValueOut;
                        }
                        else if (fNotaryC) {
                            if (frozenTxOut.fractions.Total() != 0) {
                                sFailCause = "P-I-N-12: Cold notary output has already assigned value";
                                return false;
                            }
                            CBitcoinAddress address(sAddress);
                            if (!address.IsValid()) {
                                sFailCause = "P-I-N-13: Cold notary: input address is not valid";
                                return false;
                            }
                            CFractions frozenOut = frInp.RatioPart(nFrozenValueOut);
                            frozenTxOut.fractions += frozenOut;
                            if (!frozenTxOut.fractions.SetMark(CFractions::MARK_SET, CFractions::NOTARY_C, nTime)) {
                                sFailCause = "P-I-N-14: Crossing marks are detected";
                                return false;
                            }
                            frozenTxOut.fractions.sReturnAddr = sAddress;
                            // deduct frozenOut for this C output as it is once
                            // the diff can go to other outputs
                            int64_t nReserveDeduct = 0;
                            int64_t nLiquidityDeduct = 0;
                            frReserve -= frozenOut.LowPart(nSupply, &nReserveDeduct);
                            frLiquidity -= frozenOut.HighPart(nSupply, &nLiquidityDeduct);
                            nReserveIn -= nReserveDeduct;
                            nLiquidityIn -= nLiquidityDeduct;
                        }
                    }
                }
            }
        }

        nReservesTotal += nReserveIn;
        nLiquidityTotal += nLiquidityIn;
    }

    // #NOTE6
    CFractions frCommonLiquidity(0, CFractions::STD);
    for(const auto & item : poolLiquidity) {
        frCommonLiquidity += item.second;
    }

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

        // #NOTE7
        if (fFreezeAll && poolFrozen.count(i)) {

            if (poolFrozen[i].fractions.Total() >0) {
                frOut = poolFrozen[i].fractions;
            }
            else {
                if (poolFrozen[i].fractions.nFlags & CFractions::NOTARY_V) {
                    if (!frOut.SetMark(CFractions::MARK_SET, CFractions::NOTARY_V, nTime)) {
                        sFailCause = "P-O-N-1: Crossing marks are detected";
                        return false;
                    }
                    frCommonLiquidity.MoveRatioPartTo(nValue, frOut);
                    nCommonLiquidity -= nValue;
                }
                else if (poolFrozen[i].fractions.nFlags & CFractions::NOTARY_F) {
                    if (!frOut.SetMark(CFractions::MARK_SET, CFractions::NOTARY_F, nTime)) {
                        sFailCause = "P-O-N-2: Crossing marks are detected";
                        return false;
                    }

                    vector<string> vAddresses;
                    auto sFrozenAddress = poolFrozen[i].sAddress;
                    vAddresses.push_back(sFrozenAddress); // make it first
                    for(auto it = poolReserves.begin(); it != poolReserves.end(); it++) {
                        if (it->first == sFrozenAddress) continue;
                        vAddresses.push_back(it->first);
                    }

                    int64_t nValueLeft = nValue;
                    int64_t nValueToTakeReserves = nValueLeft;
                    if (poolFrozen[i].nFairWithdrawFromEscrowIndex1 == i) {
                        if (poolFrozen.size()==2) {
                            long nIndex1 = poolFrozen[i].nFairWithdrawFromEscrowIndex1;
                            long nIndex2 = poolFrozen[i].nFairWithdrawFromEscrowIndex2;
                            if (nIndex1 <0 || nIndex2 <0 ||
                                size_t(nIndex1) >= n_vout ||
                                size_t(nIndex2) >= n_vout) {
                                sFailCause = "P-O-N-3: Wrong refering output for fair withdraw from escrow";
                                fFailedPegOut = true;
                                break;
                            }
                            // Making an fair withdraw of reserves funds from escrow.
                            // Takes proportionally less from input address to freeze into
                            // first output - to leave fair amount of reserve for second.
                            int64_t nValue1 = poolFrozen[nIndex1].nValue;
                            int64_t nValue2 = poolFrozen[nIndex2].nValue;
                            if (poolReserves.count(sFrozenAddress) > 0) {
                                auto & frReserve = poolReserves[sFrozenAddress];
                                int64_t nReserve = frReserve.Total();
                                if (nReserve <= (nValue1+nValue2) && (nValue1+nValue2)>0) {
                                    int64_t nScaledValue1 = RatioPart(nReserve, nValue1, nValue1+nValue2);
                                    int64_t nScaledValue2 = RatioPart(nReserve, nValue2, nValue1+nValue2);
                                    int64_t nRemain = nReserve - nScaledValue1 - nScaledValue2;
                                    nValueToTakeReserves = nScaledValue1+nRemain;
                                }
                            }
                        }
                    }

                    for(const string & sAddress : vAddresses) {
                        if (poolReserves.count(sAddress) == 0) continue;
                        auto & frReserve = poolReserves[sAddress];
                        int64_t nReserve = frReserve.Total();
                        if (nReserve ==0) continue;
                        int64_t nValueToTake = nValueToTakeReserves;
                        if (nValueToTake > nReserve)
                            nValueToTake = nReserve;

                        frReserve.MoveRatioPartTo(nValueToTake, frOut);
                        nValueLeft -= nValueToTake;
                        nValueToTakeReserves -= nValueToTake;

                        if (nValueToTakeReserves == 0) {
                            break;
                        }
                    }

                    if (nValueLeft > 0) {
                        if (nValueLeft > nCommonLiquidity) {
                            sFailCause = "P-O-1: No liquidity left";
                            fFailedPegOut = true;
                            break;
                        }
                        frCommonLiquidity.MoveRatioPartTo(nValueLeft, frOut);
                        nCommonLiquidity -= nValueLeft;
                    }
                }
            }
        }
        else {
            if (!poolFrozen.count(i)) { // not frozen
                if (poolReserves.count(sAddress)) { // back to reserve
                    int64_t nValueLeft = nValue;
                    int64_t nValueToTake = nValueLeft;

                    auto & frReserve = poolReserves[sAddress];
                    int64_t nReserve = frReserve.Total();
                    if (nReserve >0) {
                        if (nValueToTake > nReserve)
                            nValueToTake = nReserve;

                        frReserve.MoveRatioPartTo(nValueToTake, frOut);
                        nValueLeft -= nValueToTake;
                    }

                    if (nValueLeft > 0) {
                        if (nValueLeft > nCommonLiquidity) {
                            sFailCause = "P-O-2: No liquidity left";
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
                            int64_t nReserve = frReserve.Total();
                            if (nReserve ==0) continue;
                            int64_t nValueToTake = nValueLeft;
                            if (nValueToTake > nReserve)
                                nValueToTake = nReserve;

                            frReserve.MoveRatioPartTo(nValueToTake, frOut);
                            nValueLeft -= nValueToTake;

                            if (nValueLeft == 0) {
                                break;
                            }
                        }

                        if (nValueLeft > 0) {
                            if (nValueLeft > nCommonLiquidity) {
                                sFailCause = "P-O-3: No liquidity left";
                                fFailedPegOut = true;
                                break;
                            }
                            frCommonLiquidity.MoveRatioPartTo(nValueLeft, frOut);
                            nCommonLiquidity -= nValueLeft;
                        }
                    }
                    else {
                        int64_t nValueLeft = nValue;
                        if (nValueLeft > nCommonLiquidity) {
                            sFailCause = "P-O-4: No liquidity left";
                            fFailedPegOut = true;
                            break;
                        }
                        frCommonLiquidity.MoveRatioPartTo(nValueLeft, frOut);
                        nCommonLiquidity -= nValueLeft;
                    }
                }
            }
            else { // frozen, but no fFreezeAll
                frOut = poolFrozen[i].fractions;
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
                sFailCause = "P-G-2: Total mismatch on output "+std::to_string(i);
                fFailedPegOut = true;
                break;
            }
        }
    }

    // when finished all outputs, poolReserves and frCommonLiquidity are fees
    int64_t nFee = nValueIn - nValueOut;
    CFractions txFeeFractions(0, CFractions::STD);
    txFeeFractions += frCommonLiquidity;
    txFeeFractions += frColdFees;
    for(const auto & item : poolReserves) {
        txFeeFractions += item.second;
    }
    if (nFee != txFeeFractions.Total() || !txFeeFractions.IsPositive()) {
        sFailCause = "P-G-3: Total mismatch on fee fractions";
        fFailedPegOut = true;
    }

    if (fFailedPegOut) {
        // remove failed fractions from pool
        auto fkey = uint320(tx.GetHash(), nLatestPegOut);
        if (mapTestFractionsPool.count(fkey)) {
            auto it = mapTestFractionsPool.find(fkey);
            mapTestFractionsPool.erase(it);
        }
        return false;
    }

    feesFractions += txFeeFractions;

    // now all outputs are ready, place them as inputs for next tx in the list
    for (unsigned int i = 0; i < n_vout; i++)
    {
        auto fkey = uint320(tx.GetHash(), i);
        mapInputsFractions[fkey] = mapTestFractionsPool[fkey];
    }

    return true;
}

// to be in peg.cpp as reference Params()

bool CFractions::SetMark(MarkAction action, uint32_t nMark, uint64_t nTime)
{
    uint32_t nNewFlags = nFlags | nMark;

    int nMarks = 0;
    if (nNewFlags & CFractions::NOTARY_F) nMarks++;
    if (nNewFlags & CFractions::NOTARY_V) nMarks++;
    if (nNewFlags & CFractions::NOTARY_L) nMarks++;
    if (nNewFlags & CFractions::NOTARY_C) nMarks++;

    if (nMarks > 1) { /* marks are crossing */
        return false;
    }

    nFlags = nNewFlags;

    if (action == MARK_SET) {
        if (nFlags & CFractions::NOTARY_F) {
            nLockTime = nTime + Params().PegFrozenTime();
        }
        else if (nFlags & CFractions::NOTARY_V) {
            nLockTime = nTime + Params().PegVFrozenTime();
        }
        else if (nFlags & CFractions::NOTARY_L) {
            nLockTime = 0;
        }
        else if (nFlags & CFractions::NOTARY_C) {
            nLockTime = 0;
        }
    }
    else if (action == MARK_TRANSFER) {
        nLockTime = nTime;
    }
    else if (action == MARK_COLD_TO_FROZEN) {
        nLockTime = nTime + 4 * Params().PegFrozenTime();
    }

    return true;
}

