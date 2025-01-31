// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txmempool.h"
#include "core.h"
#include "main.h"  // for CTransaction
#include "txdb.h"

using namespace std;

CTxMemPool::CTxMemPool() {}

uint32_t CTxMemPool::GetTransactionsUpdated() const {
	LOCK(cs);
	return nTransactionsUpdated;
}

void CTxMemPool::AddTransactionsUpdated(uint32_t n) {
	LOCK(cs);
	nTransactionsUpdated += n;
}

bool CTxMemPool::addUnchecked(const uint256&    hash,
                              CTransaction&     tx,
                              const MapPrevOut& mapInputs,
                              MapFractions&     mapFractions) {
	// Add to memory pool without checking anything.
	// Used by main.cpp AcceptToMemoryPool(), which DOES do
	// all the appropriate checks.
	LOCK(cs);
	{
		mapTx[hash]       = tx;
		mapPrevOuts[hash] = mapInputs;
		for (uint32_t i = 0; i < tx.vin.size(); i++) {
			if (!tx.vin[i].prevout.IsNull() && tx.vin[i].prevout.hash != 0) {
				mapNextTx[tx.vin[i].prevout] = CInPoint(&mapTx[hash], i);
			}
		}
		nTransactionsUpdated++;
		// store fractions
		for (MapFractions::iterator mi = mapFractions.begin(); mi != mapFractions.end(); ++mi) {
			CDataStream fout(SER_DISK, CLIENT_VERSION);
			(*mi).second.Pack(fout);
			mapPackedFractions[(*mi).first] = fout.str();
		}
		mapPrevOuts[hash] = mapInputs;
	}
	return true;
}

bool CTxMemPool::remove(const CTransaction& tx, bool fRecursive) {
	// Remove transaction from memory pool
	{
		LOCK(cs);
		uint256 hash = tx.GetHash();
		if (mapTx.count(hash)) {
			if (fRecursive) {
				for (uint32_t i = 0; i < tx.vout.size(); i++) {
					std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
					if (it != mapNextTx.end())
						remove(*it->second.ptx, true);
				}
			}
			for (const CTxIn& txin : tx.vin) {
				mapNextTx.erase(txin.prevout);
			}
			for (size_t i = 0; i < tx.vout.size(); i++) {
				auto fkey = uint320(hash, i);
				mapPackedFractions.erase(fkey);
			}
			mapTx.erase(hash);
			mapPrevOuts.erase(hash);
			nTransactionsUpdated++;
		}
	}
	return true;
}

bool CTxMemPool::removeConflicts(const CTransaction& tx) {
	// Remove transactions which depend on inputs of tx, recursively
	LOCK(cs);
	for (const CTxIn& txin : tx.vin) {
		std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
		if (it != mapNextTx.end()) {
			const CTransaction& txConflict = *it->second.ptx;
			if (txConflict != tx)
				remove(txConflict, true);
		}
	}
	return true;
}

void CTxMemPool::reviewOnPegChange() {
	LOCK(cs);
	vector<uint256> vRemove;

	// build reverse map in->out to find roots
	map<CInPoint, COutPoint> mapPrevTx;
	for (map<uint256, CTransaction>::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi) {
		CTransaction& tx   = (*mi).second;
		uint256       hash = tx.GetHash();
		for (uint32_t i = 0; i < tx.vout.size(); i++) {
			std::map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
			if (it != mapNextTx.end()) {
				mapPrevTx[it->second] = COutPoint(hash, i);
			}
		}
	}

	// start peg checks from tx non-dependent on other tx
	for (map<uint256, CTransaction>::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi) {
		CTransaction& tx         = (*mi).second;
		bool          fDependent = false;
		for (uint32_t i = 0; i < tx.vin.size(); i++) {
			std::map<CInPoint, COutPoint>::iterator it = mapPrevTx.find(CInPoint(&tx, i));
			if (it != mapPrevTx.end()) {
				fDependent = true;
			}
		}
		if (!fDependent) {
			reviewOnPegChange(tx, vRemove);
		}
	}

	// remove collected and all dependent
	for (uint256 hash : vRemove) {
		std::map<uint256, CTransaction>::const_iterator it = mapTx.find(hash);
		if (it == mapTx.end())
			continue;
		const CTransaction& tx = (*it).second;
		remove(tx, true /*recursive*/);
	}
}

void CTxMemPool::reviewOnPegChange(CTransaction& tx, vector<uint256>& vRemove) {
	uint256 hash = tx.GetHash();

	MapPrevTx              mapInputs;
	MapFractions           mapInputsFractions;
	map<uint256, CTxIndex> mapUnused;
	MapFractions           mapOutputsFractions;
	CFractions             feesFractions;
	int                    nBridgePoolNout = pindexBest->nHeight;
	int64_t                nVirtBlockTime  = GetAdjustedTime();

	{
		CTxDB  txdb("r");
		CPegDB pegdb("r");

		map<string, CBridgeInfo> bridges;
		if (!pindexBest->ReadBridges(pegdb, bridges)) {
			vRemove.push_back(hash);
			return;
		}
		auto fnMerkleIn = [&](string hash) { return pindexBest->ReadMerkleIn(pegdb, hash); };

		bool fInvalid = false;
		if (!tx.FetchInputs(txdb, pegdb, nBridgePoolNout, false /*to read*/, bridges, fnMerkleIn,
							mapUnused, mapOutputsFractions, false /*is block*/, false /*is miner*/,
							nVirtBlockTime, false /*skip pruned*/, mapInputs, mapInputsFractions,
							fInvalid)) {
			if (fInvalid) {
				vRemove.push_back(hash);
				return;
			}
		}

		bool peg_ok = false;

		if (tx.IsCoinMint()) {
			string sPegFailCause;
			peg_ok = CalculateCoinMintFractions(tx, pindexBest->nPegSupplyIndex, pindexBest->nTime,
												bridges, fnMerkleIn, nBridgePoolNout, mapInputs,
			                                    mapInputsFractions, mapOutputsFractions,
			                                    feesFractions, sPegFailCause);
			if (!peg_ok) {
				vRemove.push_back(hash);
				return;
			}
		} else {
			string        sPegFailCause;
			set<uint32_t> sTimeLockPassInputs;
			peg_ok = CalculateStandardFractions(tx, pindexBest->nPegSupplyIndex, pindexBest->nTime,
			                                    mapInputs, mapInputsFractions, sTimeLockPassInputs,
			                                    mapOutputsFractions, feesFractions, sPegFailCause);
			if (!peg_ok) {
				vRemove.push_back(hash);
				return;
			}
		}

		if (peg_ok) {
			// overwrite fractions (changed due to new peg supply index)
			for (MapFractions::iterator mi = mapOutputsFractions.begin();
			     mi != mapOutputsFractions.end(); ++mi) {
				CDataStream fout(SER_DISK, CLIENT_VERSION);
				(*mi).second.Pack(fout);
				mapPackedFractions[(*mi).first] = fout.str();
			}
			// check dependent transactions
			for (uint32_t i = 0; i < tx.vout.size(); i++) {
				map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
				if (it != mapNextTx.end()) {
					reviewOnPegChange(*it->second.ptx, vRemove);
				}
			}
		}
	}
}

void CTxMemPool::clear() {
	LOCK(cs);
	mapTx.clear();
	mapPrevOuts.clear();
	mapNextTx.clear();
	++nTransactionsUpdated;
}

void CTxMemPool::queryHashes(std::vector<uint256>& vtxid) {
	vtxid.clear();

	LOCK(cs);
	vtxid.reserve(mapTx.size());
	for (map<uint256, CTransaction>::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
		vtxid.push_back((*mi).first);
}

bool CTxMemPool::lookup(uint256 hash, CTransaction& result, MapFractions& mapFractions) const {
	LOCK(cs);
	std::map<uint256, CTransaction>::const_iterator it = mapTx.find(hash);
	if (it == mapTx.end())
		return false;
	result = it->second;
	if (pindexBest->nHeight < nPegStartHeight) {
		return true;
	}
	for (size_t i = 0; i < result.vout.size(); i++) {
		auto fkey = uint320(hash, i);
		if (!mapPackedFractions.count(fkey))
			return false;
		string      strValue = mapPackedFractions.at(fkey);
		CFractions  f(result.vout[i].nValue, CFractions::STD);
		CDataStream finp(strValue.data(), strValue.data() + strValue.size(), SER_DISK,
		                 CLIENT_VERSION);
		f.Unpack(finp);
		mapFractions[fkey] = f;
	}
	return true;
}

bool CTxMemPool::lookup(uint256 hash, size_t n, CFractions& f) const {
	std::map<uint256, CTransaction>::const_iterator it = mapTx.find(hash);
	if (it == mapTx.end())
		return false;
	CTransaction result = it->second;
	auto         fkey   = uint320(hash, n);
	if (!mapPackedFractions.count(fkey))
		return false;
	string strValue = mapPackedFractions.at(fkey);
	f               = CFractions(result.vout[n].nValue, CFractions::STD);
	CDataStream finp(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
	f.Unpack(finp);
	return true;
}
