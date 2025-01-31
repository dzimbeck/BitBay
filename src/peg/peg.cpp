// Copyright (c) 2018 yshurik
//
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The use in another cyptocurrency project the code is licensed under
// Jelurida Public License (JPL). See https://www.jelurida.com/resources/jpl

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/version.hpp>

#include <leveldb/cache.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>
#include <memenv/memenv.h>

#include "base58.h"
#include "main.h"
#include "peg.h"
#include "pegdb-leveldb.h"
#include "txdb-leveldb.h"
#include "util.h"

#include <zconf.h>
#include <zlib.h>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

using namespace std;
using namespace boost;

int  nPegStartHeight      = 1000000000;  // 1G blocks as high number
int  nPegMaxSupplyIndex   = 1198;
bool fPegIsActivatedViaTx = false;

static string sBurnAddress = "bJnV8J5v74MGctMyVSVPfGu1mGQ9nMTiB3";

extern leveldb::DB* txdb;  // global pointer for LevelDB object instance

bool SetBlocksIndexesReadyForPeg(CTxDB& ctxdb, LoadMsg load_msg) {
	leveldb::Iterator* iterator = txdb->NewIterator(leveldb::ReadOptions());
	// Seek to start key.
	CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
	ssStartKey << make_pair(string("blockindex"), uint256(0));
	iterator->Seek(ssStartKey.str());
	// Now read each entry.
	int indexCount = 0;
	while (iterator->Valid()) {
		boost::this_thread::interruption_point();
		// Unpack keys and values.
		CDataStream ssKey(SER_DISK, CLIENT_VERSION);
		ssKey.write(iterator->key().data(), iterator->key().size());
		CDataStream ssValue(SER_DISK, CLIENT_VERSION);
		ssValue.write(iterator->value().data(), iterator->value().size());
		string strType;
		ssKey >> strType;
		// Did we reach the end of the data to read?
		if (strType != "blockindex")
			break;
		CDiskBlockIndex diskindex;
		ssValue >> diskindex;

		uint256                              blockHash = diskindex.GetBlockHash();
        unordered_map<uint256, CBlockIndex*>::iterator mi        = mapBlockIndex.find(blockHash);
		if (mi == mapBlockIndex.end()) {
			return error("SetBlocksIndexesReadyForPeg() : mapBlockIndex failed");
		}
		CBlockIndex* pindexNew = (*mi).second;

		pindexNew->SetPeg(pindexNew->nHeight >= nPegStartHeight);
		diskindex.SetPeg(pindexNew->nHeight >= nPegStartHeight);
		ctxdb.WriteBlockIndex(diskindex);

		iterator->Next();

		indexCount++;
		if (indexCount % 10000 == 0) {
			load_msg(std::string(" update block indexes for peg: ") + std::to_string(indexCount));
		}
	}
	delete iterator;

	if (!ctxdb.WriteBlockIndexIsPegReady(true))
		return error("SetBlocksIndexesReadyForPeg() : flag write failed");

	return true;
}

int CBlockIndex::PegCycle() const {
	int nPegInterval = Params().PegInterval();
	return nHeight / nPegInterval;
}

CBlockIndex* CBlockIndex::PegCycleBlock() const {
	int          nPegInterval = Params().PegInterval();
	int          n            = nHeight % nPegInterval;
	CBlockIndex* cycle_pindex = const_cast<CBlockIndex*>(this);
	while (n > 0) {
		cycle_pindex = cycle_pindex->Prev();
		n--;
	}
	return cycle_pindex;
}

CBlockIndex* CBlockIndex::PrevPegCycleBlock() const {
	int nPegInterval = Params().PegInterval();
	int n            = nPegInterval;
	// get current cycle block and roll cycle
	CBlockIndex* cycle_pindex = PegCycleBlock();
	while (n > 0) {
		cycle_pindex = cycle_pindex->Prev();
		n--;
	}
	return cycle_pindex;
}

bool CalculateBlockPegIndex(CPegDB& pegdb, CBlockIndex* pindex) {
	if (!pindex->Prev()) {
		pindex->nPegSupplyIndex = 0;
		return true;
	}

	pindex->nPegSupplyIndex = pindex->Prev()->GetNextBlockPegSupplyIndex();
	return true;
}

int CBlockIndex::GetNextBlockPegSupplyIndex() const {
	int nNextHeight  = nHeight + 1;
	int nPegInterval = Params().PegInterval();

	if (nNextHeight < nPegStartHeight) {
		return 0;
	}
	if (nNextHeight % nPegInterval != 0) {
		return nPegSupplyIndex;
	}

	// back to 2 intervals and -1 to count voice of back-third interval, as votes sum at
	// nPegInterval-1
	auto usevotesindex = this;
	while (usevotesindex->nHeight > (nNextHeight - nPegInterval * 2 - 1))
		usevotesindex = usevotesindex->Prev();

	// back to 3 intervals and -1 for votes calculations of 2x and 3x
	auto prevvotesindex = this;
	while (prevvotesindex->nHeight > (nNextHeight - nPegInterval * 3 - 1))
		prevvotesindex = prevvotesindex->Prev();

	return ComputeNextPegSupplyIndex(nPegSupplyIndex, usevotesindex, prevvotesindex);
}

int CBlockIndex::GetNextIntervalPegSupplyIndex() const {
	if (nHeight < nPegStartHeight) {
		return 0;
	}

	int nPegInterval          = Params().PegInterval();
	int nCurrentInterval      = nHeight / nPegInterval;
	int nCurrentIntervalStart = nCurrentInterval * nPegInterval;

	// back to 2 intervals and -1 to count voice of back-third interval, as votes sum at
	// nPegInterval-1
	auto usevotesindex = this;
	while (usevotesindex->nHeight > (nCurrentIntervalStart - nPegInterval * 1 - 1))
		usevotesindex = usevotesindex->Prev();

	// back to 3 intervals and -1 for votes calculations of 2x and 3x
	auto prevvotesindex = this;
	while (prevvotesindex->nHeight > (nCurrentIntervalStart - nPegInterval * 2 - 1))
		prevvotesindex = prevvotesindex->Prev();

	return CBlockIndex::ComputeNextPegSupplyIndex(nPegSupplyIndex, usevotesindex, prevvotesindex);
}

int CBlockIndex::GetNextNextIntervalPegSupplyIndex() const {
	if (nHeight < nPegStartHeight) {
		return 0;
	}

	int nPegInterval          = Params().PegInterval();
	int nCurrentInterval      = nHeight / nPegInterval;
	int nCurrentIntervalStart = nCurrentInterval * nPegInterval;

	// back to 2 intervals and -1 to count voice of back-third interval, as votes sum at
	// nPegInterval-1
	auto usevotesindex = this;
	while (usevotesindex->nHeight > (nCurrentIntervalStart - nPegInterval * 0 - 1))
		usevotesindex = usevotesindex->Prev();

	// back to 3 intervals and -1 for votes calculations of 2x and 3x
	auto prevvotesindex = this;
	while (prevvotesindex->nHeight > (nCurrentIntervalStart - nPegInterval * 1 - 1))
		prevvotesindex = prevvotesindex->Prev();

	return CBlockIndex::ComputeNextPegSupplyIndex(GetNextIntervalPegSupplyIndex(), usevotesindex,
	                                              prevvotesindex);
}

// #NOTE15
int CBlockIndex::ComputeNextPegSupplyIndex(int                nPegBase,
                                           const CBlockIndex* back2interval,
                                           const CBlockIndex* back3interval) {
	auto usevotesindex       = back2interval;
	auto prevvotesindex      = back3interval;
	int  nNextPegSupplyIndex = nPegBase;

	int inflate  = usevotesindex->nPegVotesInflate;
	int deflate  = usevotesindex->nPegVotesDeflate;
	int nochange = usevotesindex->nPegVotesNochange;

	int inflate_prev  = prevvotesindex->nPegVotesInflate;
	int deflate_prev  = prevvotesindex->nPegVotesDeflate;
	int nochange_prev = prevvotesindex->nPegVotesNochange;

	if (deflate > inflate && deflate > nochange) {
		nNextPegSupplyIndex++;
		if (deflate > 2 * inflate_prev && deflate > 2 * nochange_prev)
			nNextPegSupplyIndex++;
		if (deflate > 3 * inflate_prev && deflate > 3 * nochange_prev)
			nNextPegSupplyIndex++;
	}
	if (inflate > deflate && inflate > nochange) {
		nNextPegSupplyIndex--;
		if (inflate > 2 * deflate_prev && inflate > 2 * nochange_prev)
			nNextPegSupplyIndex--;
		if (inflate > 3 * deflate_prev && inflate > 3 * nochange_prev)
			nNextPegSupplyIndex--;
	}

	// over max
	if (nNextPegSupplyIndex >= nPegMaxSupplyIndex)
		nNextPegSupplyIndex = nPegMaxSupplyIndex;
	// less min
	else if (nNextPegSupplyIndex < 0)
		nNextPegSupplyIndex = 0;

	return nNextPegSupplyIndex;
}

int CalculatePegVotes(const CFractions& fractions, int nPegSupplyIndex) {
	int nVotes = 1;

	int64_t nReserveWeight = 0;
	int64_t nLiquidWeight  = 0;

	fractions.LowPart(nPegSupplyIndex, &nReserveWeight);
	fractions.HighPart(nPegSupplyIndex, &nLiquidWeight);

	if (nLiquidWeight > INT_LEAST64_MAX / (nPegSupplyIndex + 2)) {
		// check for rare extreme case when user stake more than about 100M coins
		// in this case multiplication is very close int64_t overflow (int64 max is ~92 GCoins)
		multiprecision::uint128_t nLiquidWeight128(nLiquidWeight);
		multiprecision::uint128_t nPegSupplyIndex128(nPegSupplyIndex);
		multiprecision::uint128_t nPegMaxSupplyIndex128(nPegMaxSupplyIndex);
		multiprecision::uint128_t f128 =
		    (nLiquidWeight128 * nPegSupplyIndex128) / nPegMaxSupplyIndex128;
		nLiquidWeight -= f128.convert_to<int64_t>();
	} else  // usual case, fast calculations
		nLiquidWeight -= nLiquidWeight * nPegSupplyIndex / nPegMaxSupplyIndex;

	int nWeightMultiplier = nPegSupplyIndex / 120 + 1;
	if (nLiquidWeight > (nReserveWeight * 4)) {
		nVotes = 4 * nWeightMultiplier;
	} else if (nLiquidWeight > (nReserveWeight * 3)) {
		nVotes = 3 * nWeightMultiplier;
	} else if (nLiquidWeight > (nReserveWeight * 2)) {
		nVotes = 2 * nWeightMultiplier;
	}

	return nVotes;
}

bool CalculateBlockPegVotes(const CBlock& cblock,
                            CBlockIndex*  pindex,
                            CTxDB&        txdb,
                            CPegDB&       pegdb,
                            string&       staker_addr) {
	int nPegInterval = Params().PegInterval();

	if (!cblock.IsProofOfStake() || pindex->nHeight < nPegStartHeight) {
		pindex->nPegVotesInflate  = 0;
		pindex->nPegVotesDeflate  = 0;
		pindex->nPegVotesNochange = 0;
		return true;
	}

	if (pindex->nHeight % nPegInterval == 0) {
		pindex->nPegVotesInflate  = 0;
		pindex->nPegVotesDeflate  = 0;
		pindex->nPegVotesNochange = 0;
	} else if (pindex->Prev()) {
		pindex->nPegVotesInflate  = pindex->Prev()->nPegVotesInflate;
		pindex->nPegVotesDeflate  = pindex->Prev()->nPegVotesDeflate;
		pindex->nPegVotesNochange = pindex->Prev()->nPegVotesNochange;
	}

	int nVotes = 1;

	const CTransaction& tx = cblock.vtx[1];

	size_t n_vin = tx.vin.size();
	if (tx.IsCoinBase())
		n_vin = 0;
	for (uint32_t i = 0; i < n_vin; i++) {
		// to know the staker
		const COutPoint& prevout = tx.vin[i].prevout;
		if (i == 0) {
			CTxIndex txindex;
			if (txdb.ReadTxIndex(prevout.hash, txindex)) {
				CTransaction txPrev;
				if (txPrev.ReadFromDisk(txindex.pos)) {
					if (txPrev.vout.size() > prevout.n) {
						int                    nRequired;
						txnouttype             type;
						vector<CTxDestination> vAddresses;
						if (ExtractDestinations(txPrev.vout[prevout.n].scriptPubKey, type,
						                        vAddresses, nRequired)) {
							if (vAddresses.size() == 1) {
								staker_addr = CBitcoinAddress(vAddresses.front()).ToString();
							}
						}
					}
				}
			}
		}
		// peg votes
		CFractions fractions(0, CFractions::STD);
		if (!pegdb.ReadFractions(uint320(prevout.hash, prevout.n), fractions)) {
			return false;
		}

		nVotes = CalculatePegVotes(fractions, pindex->nPegSupplyIndex);
		break;
	}

	for (const CTxOut& out : tx.vout) {
		const CScript& scriptPubKey = out.scriptPubKey;

		txnouttype             type;
		vector<CTxDestination> addresses;
		int                    nRequired;

		if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
			continue;
		}

		bool voted = false;
		for (const CTxDestination& addr : addresses) {
			std::string str_addr = CBitcoinAddress(addr).ToString();
			if (str_addr == Params().PegInflateAddr()) {
				pindex->nPegVotesInflate += nVotes;
				voted = true;
				break;
			} else if (str_addr == Params().PegDeflateAddr()) {
				pindex->nPegVotesDeflate += nVotes;
				voted = true;
				break;
			} else if (str_addr == Params().PegNochangeAddr()) {
				pindex->nPegVotesNochange += nVotes;
				voted = true;
				break;
			}
		}

		if (voted)  // only one vote to count
			break;
	}

	return true;
}

bool CalculateStandardFractions(const CTransaction& tx,
                                int                 nSupply,
                                uint32_t            nBlockTime,
                                MapPrevTx&          mapTxInputs,
                                MapFractions&       mapInputsFractions,
                                set<uint32_t>       setTimeLockPass,
                                MapFractions&       mapTestFractionsPool,
                                CFractions&         feesFractions,
                                std::string&        sFailCause) {
	MapPrevOut mapInputs;
	size_t     n_vin = tx.vin.size();

	if (tx.IsCoinBase())
		n_vin = 0;
	for (uint32_t i = 0; i < n_vin; i++) {
		const COutPoint& prevout = tx.vin[i].prevout;
		CTransaction&    txPrev  = mapTxInputs[prevout.hash].second;
		if (prevout.n >= txPrev.vout.size()) {
			sFailCause = "P-G-P-1: Refered output out of range";
			return false;
		}

		auto fkey       = uint320(prevout.hash, prevout.n);
		mapInputs[fkey] = txPrev.vout[prevout.n];
	}

	return CalculateStandardFractions(tx, nSupply, nBlockTime, mapInputs, mapInputsFractions,
	                                  setTimeLockPass, mapTestFractionsPool, feesFractions,
	                                  sFailCause);
}

bool CalculateCoinMintFractions(const CTransaction&                tx,
								int                                nSupply,
								uint32_t                           nTime,
								const map<string, CBridgeInfo>     bridges,
								std::function<CMerkleInfo(string)> fnMerkleIn,
								int                                nBridgePoolNout,
								MapPrevOut&                        mapInputs,
								MapFractions&                      mapInputsFractions,
								MapFractions&                      mapTestFractionsPool,
								CFractions&                        feesFractions,
								std::string&                       sFailCause) {
	size_t n_vin  = tx.vin.size();
	size_t n_vout = tx.vout.size();

	if (n_vin != 2) {
		sFailCause = "P-MI-1: Not two inputs";
		return false;
	}
	if (n_vout != 1) {
		sFailCause = "P-MI-2: Not one output";
		return false;
	}

	int64_t    nValueIn = 0;
	CFractions frInpLeaf;
	string     bridge_hash;
	CFractions frBridgePool;

	for (uint32_t i = 0; i < n_vin; i++) {
		const COutPoint& prevout = tx.vin[i].prevout;
		auto             fkey    = uint320(prevout.hash, prevout.n);
		if (!mapInputs.count(fkey)) {
			sFailCause = "P-MI-3: Refered output is not found";
			return false;
		}
		const CTxOut& prevtxout = mapInputs[fkey];

		int64_t nValue = prevtxout.nValue;
		nValueIn += nValue;

		if (mapInputsFractions.find(fkey) == mapInputsFractions.end()) {
			sFailCause = "P-MI-4: No input fractions found";
			return false;
		}

		auto frInp = mapInputsFractions[fkey].Std();
		if (frInp.Total() != prevtxout.nValue) {
			std::stringstream ss;
			ss << "P-MI-5: Input fraction " << prevout.hash.GetHex() << ":" << prevout.n
			   << " total " << frInp.Total() << " mismatches prevout value " << prevtxout.nValue;
			sFailCause = ss.str();
			return false;
		}

		if (i == 1) {
			frInpLeaf = frInp;  // requested, skip

			CBitcoinAddress addr_dest;
			vector<int64_t> sections;
			int             section_peg = 0;
			int             nonce       = 0;
			string          from;
			string          leaf;
			vector<string>  proofs;

			if (!DecodeAndValidateMintSigScript(tx.vin[1].scriptSig, addr_dest, sections,
			                                    section_peg, nonce, from, leaf, proofs)) {
				std::stringstream ss;
				ss << "P-MI-5: CoinMint DecodeAndValidateMintSigScript err "
				   << tx.vin[1].scriptSig.ToString();
				sFailCause = ss.str();
				return false;
			}

			string merkle_root;
			{
				string out_root_hex;
				if (!ComputeMintMerkleRoot(leaf, proofs, out_root_hex)) {
					std::stringstream ss;
					ss << "P-MI-6: CoinMint ComputeMintMerkleRoot err "
					   << tx.vin[1].scriptSig.ToString();
					sFailCause = ss.str();
					return false;
				}
				merkle_root = out_root_hex;
			}

			const CMerkleInfo merkle = fnMerkleIn(merkle_root);
			if (merkle.hash.empty()) {
				std::stringstream ss;
				ss << "P-MI-7: CoinMint merkle not found " << merkle_root;
				sFailCause = ss.str();
				return false;
			}
			if (!bridges.count(merkle.brhash)) {
				std::stringstream ss;
				ss << "P-MI-8: CoinMint bridge not found " << merkle.bridge << " hash "
				   << merkle.brhash;
				sFailCause = ss.str();
				return false;
			}
			const CBridgeInfo& bridge = bridges.at(merkle.brhash);
			bridge_hash               = bridge.hash;
			auto bridge_fkey          = uint320(uint256(bridge_hash), nBridgePoolNout);

			if (!mapInputsFractions.count(bridge_fkey)) {
				std::stringstream ss;
				ss << "P-MI-9: CoinMint no input bridge fractions " << bridge_hash << ":"
				   << nBridgePoolNout;
				sFailCause = ss.str();
				return false;
			}
			frBridgePool = mapInputsFractions[bridge_fkey];
			CCompressedFractions cfrLeafFromBridge(sections, section_peg, bridge.pegSteps,
			                                       bridge.microSteps);
			CFractions           frDecompressed = cfrLeafFromBridge.Decompress(frBridgePool);
			if (frDecompressed.Total() == 0) {
				std::stringstream ss;
				ss << "P-MI-10: CoinMint fail to decompress fractions " << prevout.hash.ToString()
				   << ":" << prevout.n;
				sFailCause = ss.str();
				return false;
			}
			frInpLeaf = frDecompressed;
		}
	}

	frBridgePool -= frInpLeaf;

	CFractions frOut          = frInpLeaf.RatioPart(tx.vout[0].nValue);
	CFractions txFeeFractions = frInpLeaf;
	txFeeFractions -= frOut;

	int64_t  nValueOut     = 0;
	bool     fFailedPegOut = false;
	uint32_t nLatestPegOut = 0;

	// Calculation of outputs
	for (uint32_t i = 0; i < n_vout; i++) {
		nLatestPegOut  = i;
		int64_t nValue = tx.vout[i].nValue;
		nValueOut += nValue;

		auto fkey                  = uint320(tx.GetHash(), i);
		mapTestFractionsPool[fkey] = frOut;

		// deduct from bridge pool fractions
	}

	if (!fFailedPegOut) {
		// lets do some extra checks for totals
		for (uint32_t i = 0; i < n_vout; i++) {
			auto    fkey   = uint320(tx.GetHash(), i);
			auto    f      = mapTestFractionsPool[fkey];
			int64_t nValue = tx.vout[i].nValue;
			if (nValue != f.Total() || !f.IsPositive()) {
				sFailCause    = "P-MO-1: Total mismatch on output " + std::to_string(i);
				fFailedPegOut = true;
				break;
			}
		}
	}

	// when finished all outputs, fees
	int64_t nFee = nValueIn - nValueOut;
	if (nFee != MINT_TX_FEE) {
		sFailCause = "P-MO-2: MINT_TX_FEE mismatch on fee " + std::to_string(nFee) + " vs " +
		             std::to_string(MINT_TX_FEE);
		fFailedPegOut = true;
	}
	if (nFee != txFeeFractions.Total()) {
		sFailCause = "P-MO-3: Total mismatch on fee fractions " + std::to_string(nFee) + " vs " +
		             std::to_string(txFeeFractions.Total());
		fFailedPegOut = true;
	}
	if (!txFeeFractions.IsPositive()) {
		sFailCause    = "P-MO-4: Non-positive fee fractions";
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
	for (uint32_t i = 0; i < n_vout; i++) {
		auto fkey                = uint320(tx.GetHash(), i);
		mapInputsFractions[fkey] = mapTestFractionsPool[fkey];
	}

	// deducted bridge pool
	auto bridge_fkey                = uint320(uint256(bridge_hash), nBridgePoolNout);
	mapInputsFractions[bridge_fkey] = frBridgePool;

	return true;
}

bool CalculateCoinMintFractions(const CTransaction&                tx,
								int                                nSupply,
								uint32_t                           nTime,
								const map<string, CBridgeInfo>     bridges,
								std::function<CMerkleInfo(string)> fnMerkleIn,
								int                                nBridgePoolNout,
								MapPrevTx&                         mapTxInputs,
								MapFractions&                      mapInputsFractions,
								MapFractions&                      mapTestFractionsPool,
								CFractions&                        feesFractions,
								std::string&                       sFailCause) {
	MapPrevOut mapInputs;
	size_t     n_vin = tx.vin.size();

	if (tx.IsCoinBase())
		n_vin = 0;
	for (uint32_t i = 0; i < n_vin; i++) {
		const COutPoint& prevout = tx.vin[i].prevout;
		CTransaction&    txPrev  = mapTxInputs[prevout.hash].second;
		if (prevout.n >= txPrev.vout.size()) {
			sFailCause = "P-G-P-1: Refered output out of range";
			return false;
		}

		auto fkey       = uint320(prevout.hash, prevout.n);
		mapInputs[fkey] = txPrev.vout[prevout.n];
	}

	return CalculateCoinMintFractions(tx, nSupply, nTime, bridges, fnMerkleIn, nBridgePoolNout,
	                                  mapInputs, mapInputsFractions, mapTestFractionsPool,
	                                  feesFractions, sFailCause);
}

bool CalculateStakingFractions_v2(const CTransaction&          tx,
                                  const CBlockIndex*           pindexBlock,
                                  MapPrevTx&                   inputs,
                                  MapFractions&                fInputs,
                                  std::map<uint256, CTxIndex>& mapTestPool,
                                  MapFractions&                mapTestFractionsPool,
                                  const CFractions&            feesFractions,
                                  int64_t                      nCalculatedStakeRewardWithoutFees,
                                  std::string&                 sFailCause);

bool CalculateStakingFractions(const CTransaction&          tx,
                               const CBlockIndex*           pindexBlock,
                               MapPrevTx&                   inputs,
                               MapFractions&                fInputs,
                               std::map<uint256, CTxIndex>& mapTestPool,
                               MapFractions&                mapTestFractionsPool,
                               const CFractions&            feesFractions,
                               int64_t                      nCalculatedStakeRewardWithoutFees,
                               std::string&                 sFailCause) {
	if (!pindexBlock) {
		// need a block info
		return false;
	}

	return CalculateStakingFractions_v2(tx, pindexBlock, inputs, fInputs, mapTestPool,
	                                    mapTestFractionsPool, feesFractions,
	                                    nCalculatedStakeRewardWithoutFees, sFailCause);
}

// extern std::map<string, int> stake_addr_stats;

bool CalculateStakingFractions_v2(const CTransaction&          tx,
                                  const CBlockIndex*           pindexBlock,
                                  MapPrevTx&                   inputs,
                                  MapFractions&                fInputs,
                                  std::map<uint256, CTxIndex>& mapTestPool,
                                  MapFractions&                mapTestFractionsPool,
                                  const CFractions&            feesFractions,
                                  int64_t                      nCalculatedStakeRewardWithoutFees,
                                  std::string&                 sFailCause) {
	size_t n_vin  = tx.vin.size();
	size_t n_vout = tx.vout.size();

	if (n_vin != 1) {
		sFailCause = "More than one input";
		return false;
	}

	if (n_vout > 8) {
		sFailCause = "More than 8 outputs";
		return false;
	}

	int64_t    nValueStakeIn = 0;
	CFractions frStake(0, CFractions::STD);

	string sInputAddress;

	// only one input
	{
		uint32_t         i       = 0;
		const COutPoint& prevout = tx.vin[i].prevout;
		CTransaction&    txPrev  = inputs[prevout.hash].second;
		if (prevout.n >= txPrev.vout.size()) {
			sFailCause = "P-I-1: Refered output out of range";
			return false;
		}

		int64_t nValue = txPrev.vout[prevout.n].nValue;
		nValueStakeIn  = nValue;

		auto sAddress = txPrev.vout[prevout.n].scriptPubKey.ToAddress();
		sInputAddress = sAddress;

		auto fkey = uint320(prevout.hash, prevout.n);
		if (fInputs.find(fkey) == fInputs.end()) {
			sFailCause = "P-I-2: No input fractions found";
			return false;
		}

		frStake = fInputs[fkey].Std();
		if (frStake.Total() != nValueStakeIn) {
			sFailCause = "P-I-3: Input fraction total mismatches value";
			return false;
		}
	}

	// stake_addr_stats[sInputAddress]++;

	// Check funds to be returned to same address
	int64_t nValueReturn = 0;
	for (uint32_t i = 0; i < n_vout; i++) {
		std::string sAddress = tx.vout[i].scriptPubKey.ToAddress();
		if (sInputAddress == sAddress) {
			nValueReturn += tx.vout[i].nValue;
		}
	}
	if (nValueReturn < nValueStakeIn) {
		sFailCause = "No enough funds returned to input address";
		return false;
	}

	CFractions frReward(nCalculatedStakeRewardWithoutFees, CFractions::STD);
	frReward += feesFractions;

	int64_t nValueRewardLeft = frReward.Total();

	bool     fFailedPegOut = false;
	uint32_t nStakeOut     = 0;

	// Transfer mark and set stake output
	for (uint32_t i = 0; i < n_vout; i++) {
		int64_t nValue = tx.vout[i].nValue;

		auto  fkey  = uint320(tx.GetHash(), i);
		auto& frOut = mapTestFractionsPool[fkey];

		string sNotary;
		bool   fNotary  = false;
		auto   sAddress = tx.vout[i].scriptPubKey.ToAddress(&fNotary, &sNotary);

		// first output returning on same address and greater or equal stake value
		if (nValue >= nValueStakeIn && sInputAddress == sAddress) {
			if (nValue > (nValueStakeIn + nValueRewardLeft)) {
				sFailCause    = "P-O-1: No enough coins for stake output";
				fFailedPegOut = true;
			}

			int64_t nValueToTake = nValue;
			int64_t nStakeToTake = nValue;

			if (nStakeToTake > nValueStakeIn) {
				nStakeToTake = nValueStakeIn;
			}

			// first take whole stake input
			nValueToTake -= nStakeToTake;
			frOut = frStake;

			// leftover value take from reward
			if (nValueToTake > 0) {
				nValueRewardLeft -= nValueToTake;
				frReward.MoveRatioPartTo(nValueToTake, frOut);
			}

			// transfer marks and locktime
			if (frStake.nFlags & CFractions::NOTARY_F) {
				if (!frOut.SetMark(CFractions::MARK_TRANSFER, CFractions::NOTARY_F,
				                   frStake.nLockTime)) {
					sFailCause    = "P-O-2: Crossing marks are detected";
					fFailedPegOut = true;
				}
			} else if (frStake.nFlags & CFractions::NOTARY_V) {
				if (!frOut.SetMark(CFractions::MARK_TRANSFER, CFractions::NOTARY_V,
				                   frStake.nLockTime)) {
					sFailCause    = "P-O-3: Crossing marks are detected";
					fFailedPegOut = true;
				}
			} else if (frStake.nFlags & CFractions::NOTARY_C) {
				if (!frOut.SetMark(CFractions::MARK_TRANSFER, CFractions::NOTARY_C,
				                   frStake.nLockTime)) {
					sFailCause    = "P-O-4: Crossing marks are detected";
					fFailedPegOut = true;
				}
			}

			nStakeOut = i;
			break;
		}
	}

	if (fFailedPegOut) {
		return false;
	}

	if (nStakeOut == 0 && !fFailedPegOut) {
		sFailCause    = "P-O-5: No stake funds returned to input address";
		fFailedPegOut = true;
	}

	// Calculation of outputs
	for (uint32_t i = 0; i < n_vout; i++) {
		int64_t nValue = tx.vout[i].nValue;

		if (i == nStakeOut) {
			// already calculated and marked
			continue;
		}

		auto  fkey  = uint320(tx.GetHash(), i);
		auto& frOut = mapTestFractionsPool[fkey];

		if (nValue > nValueRewardLeft) {
			sFailCause    = "P-O-6: No coins left";
			fFailedPegOut = true;
			break;
		}

		frReward.MoveRatioPartTo(nValue, frOut);
		nValueRewardLeft -= nValue;
	}

	if (!fFailedPegOut) {
		// lets do some extra checks for totals
		for (uint32_t i = 0; i < n_vout; i++) {
			auto    fkey   = uint320(tx.GetHash(), i);
			auto    f      = mapTestFractionsPool[fkey];
			int64_t nValue = tx.vout[i].nValue;
			if (nValue != f.Total() || !f.IsPositive()) {
				sFailCause    = "P-O-7: Total mismatch on output " + std::to_string(i);
				fFailedPegOut = true;
				break;
			}
		}
	}

	if (fFailedPegOut) {
		// for now remove failed fractions from pool so they
		// are not written to db
		for (uint32_t i = 0; i < n_vout; i++) {
			auto fkey = uint320(tx.GetHash(), i);
			if (mapTestFractionsPool.count(fkey)) {
				auto it = mapTestFractionsPool.find(fkey);
				mapTestFractionsPool.erase(it);
			}
		}
		return false;
	}

	return true;
}

void PrunePegForBlock(const CBlock& blockprune, CPegDB& pegdb) {
	for (size_t i = 0; i < blockprune.vtx.size(); i++) {
		const CTransaction& tx = blockprune.vtx[i];
		for (size_t j = 0; j < tx.vin.size(); j++) {
			COutPoint prevout = tx.vin[j].prevout;
			auto      fkey    = uint320(prevout.hash, prevout.n);
			pegdb.Erase(fkey);
		}
		if (!tx.IsCoinStake())
			continue;

		uint256 txhash = tx.GetHash();
		for (size_t j = 0; j < tx.vout.size(); j++) {
			auto fkey = uint320(txhash, j);

			CTxOut out = tx.vout[j];

			if (out.nValue == 0) {
				pegdb.Erase(fkey);
				continue;
			}

			const CScript&         scriptPubKey = out.scriptPubKey;
			txnouttype             type;
			vector<CTxDestination> addresses;
			int                    nRequired;

			if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
				string notary;
				if (scriptPubKey.ToNotary(notary)) {
					pegdb.Erase(fkey);
					continue;
				}
				continue;
			}

			bool voted = false;
			for (const CTxDestination& addr : addresses) {
				std::string str_addr = CBitcoinAddress(addr).ToString();
				if (str_addr == Params().PegInflateAddr()) {
					voted = true;
				} else if (str_addr == Params().PegDeflateAddr()) {
					voted = true;
				} else if (str_addr == Params().PegNochangeAddr()) {
					voted = true;
				}
			}
			if (voted) {
				pegdb.Erase(fkey);
			}
		}
	}
}
