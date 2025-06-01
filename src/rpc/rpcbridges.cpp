// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018-2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/assign/list_of.hpp>

#include <fstream>
#include <iterator>
#include <vector>

#include "base58.h"
#include "init.h"
#include "main.h"
#include "rpcserver.h"
// #include "checkpoints.h"
#include "pegdb-leveldb.h"
#include "txdb-leveldb.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

extern void TxToJSON(const CTransaction& tx,
                     const uint256       hashBlock,
                     const MapFractions&,
                     int                  nSupply,
                     json_spirit::Object& entry);

Value bridges(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("bridges\n");

	Object result;

	CPegDB                   pegdb;
	map<string, CBridgeInfo> bridges;
	bool                     ok        = pindexBest->ReadBridges(pegdb, bridges);
	bool                     is_paused = false;
	ok                                 = pindexBest->ReadBridgesPause(pegdb, is_paused);

	for (const auto& it : bridges) {
		string      name   = it.first;
		CBridgeInfo bridge = it.second;

		Object jbridge;
		jbridge.push_back(Pair("name", bridge.name));
		jbridge.push_back(Pair("hash", bridge.hash));
		jbridge.push_back(Pair("symb", bridge.symbol));
		Array jurls;
		for (const string& url : bridge.urls) {
			jurls.push_back(url);
		}
		jbridge.push_back(Pair("links", jurls));
		jbridge.push_back(Pair("chain_id", bridge.chainId));
		jbridge.push_back(Pair("contract", bridge.contract));
		jbridge.push_back(Pair("pegsteps", bridge.pegSteps));
		jbridge.push_back(Pair("microsteps", bridge.microSteps));

		CFractions bridgePoolFraction;
		auto       bridge_fkey = uint320(uint256(bridge.hash), pindexBest->nHeight);
		if (!pegdb.ReadFractions(bridge_fkey, bridgePoolFraction, true /*must*/)) {
			jbridge.push_back(Pair("pool", "<read error>"));
		} else {
			CPegData pd;
			pd.peglevel                 = CPegLevel(1, 0, 0, 0, 0, 0);
			CBlockIndex* pindex         = pindexBest;
			int          nPegInterval   = Params().PegInterval();
			pd.peglevel.nCycle          = pindex->nHeight / nPegInterval;
			pd.peglevel.nCyclePrev      = pd.peglevel.nCycle - 1;
			pd.peglevel.nBuffer         = 0;
			pd.peglevel.nSupply         = pindex->nPegSupplyIndex;
			pd.peglevel.nSupplyNext     = pindex->GetNextIntervalPegSupplyIndex();
			pd.peglevel.nSupplyNextNext = pindex->GetNextNextIntervalPegSupplyIndex();
			pd.fractions                = bridgePoolFraction;
			pd.nLiquid                  = pd.fractions.High(pd.peglevel);
			pd.nReserve                 = pd.fractions.Low(pd.peglevel);
			jbridge.push_back(Pair("pool", pd.ToString()));
			jbridge.push_back(Pair("is_paused", is_paused));
		}

#ifdef ENABLE_WALLET
		bool   is_automated                  = false;
		double max_priority_fee_per_gas_gwei = 0.;
		double max_fee_per_gas_gwei          = 0.;
		CWalletDB(pwalletMain->strWalletFile)
		    .ReadBridgeIsAutomated(bridge.hash, is_automated, max_priority_fee_per_gas_gwei,
		                           max_fee_per_gas_gwei);
		jbridge.push_back(Pair("is_automated", is_automated));
		jbridge.push_back(Pair("max_priority_fee_per_gas_gwei", max_priority_fee_per_gas_gwei));
		jbridge.push_back(Pair("max_fee_per_gas_gwei", max_fee_per_gas_gwei));
#endif

		result.push_back(Pair(bridge.name, jbridge));
	}

	if (!ok) {
		result.push_back(Pair("read error", true));
		return result;
	}

	return result;
}

Value merklesin(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error(
		    "merklesin\n"
		    "Return last 100 (in) merkles - coins are coming from the bridges\n"
		    "merklesin <limit>\n"
		    "Return last <limit> (in) merkles - coins are coming from the bridges\n"
		    "merklesin 0\n"
		    "Return all (in) merkles - coins are coming from the bridges\n");

	Object result;

	int limit = 100;
	if (params.size() == 1) {
		limit = params[0].get_int();
	}

	CPegDB              pegdb;
	vector<CMerkleInfo> datas;
	bool                ok = pindexBest->ReadMerklesIn(pegdb, limit, datas);

	for (const CMerkleInfo& minfo : datas) {
		Object j_merkle;
		j_merkle.push_back(Pair("hash", minfo.hash));
		j_merkle.push_back(Pair("bridge", minfo.bridge));
		j_merkle.push_back(Pair("brhash", minfo.brhash));
		j_merkle.push_back(Pair("amount", minfo.amount));
		j_merkle.push_back(Pair("ntime", int64_t(minfo.ntime)));
		result.push_back(Pair(minfo.hash, j_merkle));
	}

	if (!ok) {
		result.push_back(Pair("read error", true));
		return result;
	}

	return result;
}

Value merklesout(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error(
		    "merklesout\n"
		    "Return last 100 bridge cycles (out) merkles - coins are coming to the bridges\n"
		    "merklesout <bridge_cycles>\n"
		    "Return last <bridge_cycles> bridge cycles (out) merkles - coins are coming to the "
		    "bridges\n"
		    "merklesout 0\n"
		    "Return all bridge cycles (out) merkles - coins are coming to the bridges\n");

	CPegDB pegdb;
	int    bridge_cycles = 100;
	Object result;

	if (params.size() == 1) {
		bridge_cycles = params[0].get_int();
	}

	CBlockIndex* bridge_index = pindexBest->BridgeCycleBlock();
	for (int i = 0; i < bridge_cycles || bridge_cycles == 0; i++) {
		if (!bridge_index)
			break;

		uint256     bhash_bridge_cycle = bridge_index->GetBlockHash();
		set<string> bridge_hashes;
		pegdb.ReadBridgeCycleBridgeHashes(bhash_bridge_cycle, bridge_hashes);

		for (const string& brhash : bridge_hashes) {
			string merkle_data;
			pegdb.ReadBridgeCycleMerkle(bhash_bridge_cycle, brhash, merkle_data);

			string         merkle, section;
			vector<string> args;
			boost::split(args, merkle_data, boost::is_any_of(":"));
			if (args.size() == 2) {
				merkle  = args[0];
				section = args[1];
				Object j_merkle;
				j_merkle.push_back(Pair("merkle", merkle));
				j_merkle.push_back(Pair("brhash", brhash));
				j_merkle.push_back(Pair("section", section));
				j_merkle.push_back(Pair("block", bhash_bridge_cycle.GetHex()));
				j_merkle.push_back(Pair("blocknum", bridge_index->nHeight));
				result.push_back(Pair(merkle, j_merkle));
			}
		}

		bridge_index = bridge_index->PrevBridgeCycleBlock();
	}

	return result;
}

Value getbridgepool(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("getbridgepool\n");

	Object result;
	return result;
}

Value timelockpasses(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("timelockpasses\n");

	Array result;

	CPegDB      pegdb;
	set<string> datas;
	bool        ok = pindexBest->ReadTimeLockPasses(pegdb, datas);

	for (const string& pubkey_txt : datas) {
		result.push_back(pubkey_txt);
	}

	if (!ok) {
		Object result;
		result.push_back(Pair("read error", true));
		return result;
	}

	return result;
}

Value bridgereceipt(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 1 || params.size() > 1)
		throw runtime_error(
		    "bridgereceipt <txhash:output>\n"
		    "Returns the receipt info for minting coins on the bridge.");

	string         txhashnout = params[0].get_str();
	vector<string> txhashnout_args;
	boost::split(txhashnout_args, txhashnout, boost::is_any_of(":"));

	if (txhashnout_args.size() != 2) {
		throw runtime_error(
		    "bridgereceipt <txhash:output>\n"
		    "Returns the receipt info for minting coins on the bridge.");
	}
	string txhash_str = txhashnout_args.front();
	int    nout       = std::stoi(txhashnout_args.back());

	Object result;
	result.push_back(Pair("txid", txhash_str));
	result.push_back(Pair("nout", nout));
	bool has_receipt = false;

	uint256 hash;
	hash.SetHex(txhash_str.c_str());

	CTransaction tx;
	uint256      hashBlock = 0;
	if (!GetTransaction(hash, tx, hashBlock))
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
		                   "No information available about transaction");
	if (nout < 0 || size_t(nout) >= tx.vout.size()) {
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No available transaction output");
	}

	uint256             bhash_bridge_cycle;
	map<string, string> brhash_to_merkle;
	map<string, string> brhash_to_brname;
	{
		uint32_t nTxNum = 0;
		uint256  blockhash;
		uint256  txhash = tx.GetHash();
		{
			LOCK(cs_main);
			CTxDB    txdb("r");
			CPegDB   pegdb("r");
			CTxIndex txindex;
			if (txdb.ReadTxIndex(txhash, txindex)) {
				txindex.GetHeightInMainChain(&nTxNum, txhash, &blockhash);
				if (mapBlockIndex.find(blockhash) != mapBlockIndex.end()) {
					CBlockIndex* pblockindex = mapBlockIndex.ref(blockhash);
					if (pblockindex) {
						CBlockIndex* blockcycle = pblockindex->BridgeCycleBlock();
						bhash_bridge_cycle      = blockcycle->GetBlockHash();
						set<string> bridge_hashes;
						pegdb.ReadBridgeCycleBridgeHashes(bhash_bridge_cycle, bridge_hashes);
						for (const string& brhash : bridge_hashes) {
							string merkle_data;
							pegdb.ReadBridgeCycleMerkle(bhash_bridge_cycle, brhash, merkle_data);
							string         merkle;
							vector<string> args;
							boost::split(args, merkle_data, boost::is_any_of(":"));
							if (args.size() == 2) {
								merkle                   = args[0];
								brhash_to_merkle[brhash] = merkle;
							}
						}
						map<string, CBridgeInfo> bridges;
						pblockindex->ReadBridges(pegdb, bridges);
						for (const auto& it : bridges) {
							CBridgeInfo bdatas            = it.second;
							brhash_to_brname[bdatas.hash] = bdatas.name;
						}
					}
				}
			}
		}
	}

	const CTxOut& txout = tx.vout[nout];

	CTxDestination address;
	if (ExtractDestination(txout.scriptPubKey, address)) {
		string strAddress = CBitcoinAddress(address).ToString();
		if (boost::starts_with(strAddress, "EXT:")) {
			vector<string> strAddress_args;
			boost::split(strAddress_args, strAddress, boost::is_any_of(":"));
			if (strAddress_args.size() == 3) {
				string brhash  = strAddress_args[1];
				string address = strAddress_args[2];
				string merkle  = brhash_to_merkle[brhash];

				Object receipt;

				string txid    = tx.GetHash().ToString();
				string txoutid = txid + ":" + std::to_string(nout);
				receipt.push_back(Pair("txid", txoutid));
				receipt.push_back(Pair("address", address));
				receipt.push_back(Pair("root", "0x" + merkle));

				Array reserve;
				Array proof;

				LOCK(cs_main);
				CPegDB pegdb("r");

				bool        is_receipt_ready = false;
				set<string> burns_data;
				pegdb.ReadBridgeCycleBurnsToBridge(bhash_bridge_cycle, brhash, burns_data);
				for (const string& burn_data : burns_data) {
					vector<string> burn_datas;
					boost::split(burn_datas, burn_data, boost::is_any_of(":"));
					if (burn_datas.size() >= 5) {
						// txout:nout
						string b_txid = burn_datas[0];
						string b_nout = burn_datas[1];
						if (b_txid == txid && b_nout == std::to_string(nout)) {
							string brname = brhash_to_brname[brhash];
							result.push_back(Pair("bridge", brname));
							result.push_back(Pair("brhash", brhash));
							int b_num_reserve = std::atoi(burn_datas[4].c_str());
							if (burn_datas.size() >= size_t(5 + b_num_reserve + 1)) {
								for (int i = 0; i < b_num_reserve; i++) {
									int64_t v = strtoll(burn_datas[5 + i].c_str(), NULL, 10);
									reserve.push_back(v);
									is_receipt_ready = true;
								}
								int b_num_proof = std::atoi(burn_datas[5 + b_num_reserve].c_str());
								if (burn_datas.size() ==
								    size_t(5 + b_num_reserve + 1 + b_num_proof)) {
									for (int i = 0; i < b_num_proof; i++) {
										string p = "0x" + burn_datas[5 + b_num_reserve + 1 + i];
										proof.push_back(p);
									}
								}
							}
						}
					}
				}
				receipt.push_back(Pair("reserve", reserve));
				receipt.push_back(Pair("proof", proof));

				result.push_back(Pair("receipt", receipt));
				result.push_back(Pair("receipt_is_ready", is_receipt_ready));
				has_receipt = true;
			}
		}
	}

	result.push_back(Pair("receipt_is_found", has_receipt));
	return result;
}

Value bridgeautomate(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 4 || params.size() > 4)
		throw runtime_error(
		    "bridgeautomate <bridge_name> enable <max_priority_fee_per_gas_gwei> "
		    "<max_fee_per_gas_gwei>\n"
		    "Enable/disable the bridge automation (curator) and set fees as float number of gwei.");

	RPCTypeCheck(params, list_of(str_type)(bool_type)(real_type)(real_type));

	bool   found                         = false;
	string name                          = params[0].get_str();
	bool   is_automated                  = params[1].get_bool();
	double max_priority_fee_per_gas_gwei = params[2].get_real();
	double max_fee_per_gas_gwei          = params[3].get_real();

	CPegDB                   pegdb;
	map<string, CBridgeInfo> bridges;
	pindexBest->ReadBridges(pegdb, bridges);
	for (const auto& it : bridges) {
		CBridgeInfo bridge      = it.second;
		string      bridge_name = bridge.name;
		if (bridge_name != name) {
			continue;
		}
		found = true;
		CWalletDB(pwalletMain->strWalletFile)
		    .WriteBridgeIsAutomated(bridge.hash, is_automated, max_priority_fee_per_gas_gwei,
		                            max_fee_per_gas_gwei);
	}
	if (!found) {
		throw runtime_error("bridge not found\n");
	}
	Object result;
	result.push_back(Pair("is_automated", is_automated));
	return result;
}
