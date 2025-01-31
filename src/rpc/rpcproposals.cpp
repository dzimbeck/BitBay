// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018-2020 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/assign/list_of.hpp>

#include "main.h"
#include "rpcserver.h"
//#include "base58.h"
//#include "kernel.h"
//#include "checkpoints.h"
//#include "txdb-leveldb.h"
#include "pegdb-leveldb.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

Value tstakers1(const Array&, bool fHelp) {
	if (fHelp)
		throw runtime_error("tstakers1\n");

	Array results;

	CPegDB      pegdb;
	set<string> sTrustedStakers1;
	pindexBest->ReadTrustedStakers1(pegdb, sTrustedStakers1);
	for (const std::string& addr : sTrustedStakers1) {
		results.push_back(addr);
	}
	return results;
}

Value tstakers2(const Array&, bool fHelp) {
	if (fHelp)
		throw runtime_error("tstakers2\n");

	Array results;

	CPegDB      pegdb;
	set<string> sTrustedStakers2;
	pindexBest->ReadTrustedStakers2(pegdb, sTrustedStakers2);
	for (const std::string& addr : sTrustedStakers2) {
		results.push_back(addr);
	}
	return results;
}

Value consensus(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("consensus\n");

	Object result;

	CPegDB                                      pegdb;
	std::map<int, CChainParams::ConsensusVotes> consensus;
	bool                                        ok = pindexBest->ReadConsensusMap(pegdb, consensus);

	for (auto it : consensus) {
		Object con;
		con.push_back(Pair("tstakers1", it.second.tstakers1));
		con.push_back(Pair("tstakers2", it.second.tstakers2));
		con.push_back(Pair("ostakers", it.second.ostakers));

		if (it.first == CChainParams::CONSENSUS_TSTAKERS) {
			result.push_back(Pair("tstakers", con));
		}
		if (it.first == CChainParams::CONSENSUS_CONSENSUS) {
			result.push_back(Pair("consensus", con));
		}
		if (it.first == CChainParams::CONSENSUS_BRIDGE) {
			result.push_back(Pair("bridge", con));
		}
		if (it.first == CChainParams::CONSENSUS_MERKLE) {
			result.push_back(Pair("merkle", con));
		}
		if (it.first == CChainParams::CONSENSUS_TIMELOCKPASS) {
			result.push_back(Pair("timelockpass", con));
		}
	}

	if (!ok) {
		result.push_back(Pair("read error", true));
	}

	return result;
}

Value proposals(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("proposals\n");

	CBlockIndex* cycle_block = pindexBest->PegCycleBlock();
	uint256      chash       = cycle_block->GetBlockHash();

	Array       results;
	set<string> ids;

	CPegDB pegdb;
	pegdb.ReadCycleProposals(chash, ids);
	for (const string& id : ids) {
		if (id == "")
			continue;

		vector<string> pdatas;
		if (pegdb.ReadProposal(id, pdatas)) {
			if (pdatas.size() < 1) {  // no scope
				continue;
			}
			string scope = pdatas[0];
			if (scope == "tstakers1" || scope == "tstakers2") {
				if (pdatas.size() == 4) {
					Object pro;
					pro.push_back(Pair("id", id));
					pro.push_back(Pair("cycle", pindexBest->PegCycle()));

					pro.push_back(Pair("scope", scope));
					pro.push_back(Pair("until", pdatas[1]));
					pro.push_back(Pair("action", pdatas[2]));
					pro.push_back(Pair("address", pdatas[3]));

					Object       votes;
					CBlockIndex* block = pindexBest;
					while (block->nHeight >= cycle_block->nHeight) {
						set<string> txoutids;
						pegdb.ReadProposalBlockVotes(block->GetBlockHash(), id, txoutids);
						for (const string& txoutid : txoutids) {
							string staker;
							if (pegdb.ReadProposalBlockVoteStaker(txoutid, staker))
								votes.push_back(Pair(txoutid, staker));
						}
						block = block->Prev();
					}
					pro.push_back(Pair("votes", votes));
					results.push_back(pro);
				}
			}
			if (scope == "consensus") {
				if (pdatas.size() == 6) {
					Object pro;
					pro.push_back(Pair("id", id));
					pro.push_back(Pair("cycle", pindexBest->PegCycle()));

					pro.push_back(Pair("scope", scope));
					pro.push_back(Pair("until", pdatas[1]));
					pro.push_back(Pair("consensus_type", pdatas[2]));

					int t1_votes = std::atoi(pdatas[3].c_str());
					int t2_votes = std::atoi(pdatas[4].c_str());
					int o_votes  = std::atoi(pdatas[5].c_str());

					Object con;
					con.push_back(Pair("tstakers1", t1_votes));
					con.push_back(Pair("tstakers2", t2_votes));
					con.push_back(Pair("ostakers", o_votes));
					pro.push_back(Pair("consensus_votes", con));

					Object       votes;
					CBlockIndex* block = pindexBest;
					while (block->nHeight >= cycle_block->nHeight) {
						set<string> txoutids;
						pegdb.ReadProposalBlockVotes(block->GetBlockHash(), id, txoutids);
						for (const string& txoutid : txoutids) {
							string staker;
							if (pegdb.ReadProposalBlockVoteStaker(txoutid, staker))
								votes.push_back(Pair(txoutid, staker));
						}
						block = block->Prev();
					}
					pro.push_back(Pair("votes", votes));
					results.push_back(pro);
				}
			}

			if (scope == "bridge") {
				if (pdatas.size() == 10) {
					Object pro;
					pro.push_back(Pair("id", id));
					pro.push_back(Pair("cycle", pindexBest->PegCycle()));

					pro.push_back(Pair("scope", scope));
					pro.push_back(Pair("until", pdatas[1]));
					pro.push_back(Pair("action", pdatas[2]));
					string name = pdatas[3];
					string brhash;
					{
						CDataStream ss(SER_GETHASH, 0);
						ss << name;
						brhash = Hash(ss.begin(), ss.end()).GetHex();
					}
					string symb       = pdatas[4];
					string urls_txt   = pdatas[5];
					int    chain_id   = std::atoi(pdatas[6].c_str());
					string contract   = pdatas[7];
					int    pegsteps   = std::atoi(pdatas[8].c_str());
					int    microsteps = std::atoi(pdatas[9].c_str());

					set<string> urls;
					boost::split(urls, urls_txt, boost::is_any_of(";"));

					Object bridge;
					bridge.push_back(Pair("name", name));
					bridge.push_back(Pair("hash", brhash));
					bridge.push_back(Pair("symb", symb));
					Array jurls;
					for (const string& url : urls) {
						jurls.push_back(url);
					}
					bridge.push_back(Pair("links", jurls));
					bridge.push_back(Pair("chain_id", chain_id));
					bridge.push_back(Pair("contract", contract));
					bridge.push_back(Pair("pegsteps", pegsteps));
					bridge.push_back(Pair("microsteps", microsteps));
					pro.push_back(Pair("bridge", bridge));

					Object       votes;
					CBlockIndex* block = pindexBest;
					while (block->nHeight >= cycle_block->nHeight) {
						set<string> txoutids;
						pegdb.ReadProposalBlockVotes(block->GetBlockHash(), id, txoutids);
						for (const string& txoutid : txoutids) {
							string staker;
							if (pegdb.ReadProposalBlockVoteStaker(txoutid, staker))
								votes.push_back(Pair(txoutid, staker));
						}
						block = block->Prev();
					}
					pro.push_back(Pair("votes", votes));
					results.push_back(pro);
				}

				if (pdatas.size() == 4) {
					Object pro;
					pro.push_back(Pair("id", id));
					pro.push_back(Pair("cycle", pindexBest->PegCycle()));

					pro.push_back(Pair("scope", scope));
					pro.push_back(Pair("until", pdatas[1]));
					pro.push_back(Pair("action", pdatas[2]));
					string name = pdatas[3];
					string brhash;
					{
						CDataStream ss(SER_GETHASH, 0);
						ss << name;
						brhash = Hash(ss.begin(), ss.end()).GetHex();
					}

					Object bridge;
					bridge.push_back(Pair("name", name));
					bridge.push_back(Pair("hash", brhash));
					pro.push_back(Pair("bridge", bridge));

					Object       votes;
					CBlockIndex* block = pindexBest;
					while (block->nHeight >= cycle_block->nHeight) {
						set<string> txoutids;
						pegdb.ReadProposalBlockVotes(block->GetBlockHash(), id, txoutids);
						for (const string& txoutid : txoutids) {
							string staker;
							if (pegdb.ReadProposalBlockVoteStaker(txoutid, staker))
								votes.push_back(Pair(txoutid, staker));
						}
						block = block->Prev();
					}
					pro.push_back(Pair("votes", votes));
					results.push_back(pro);
				}
			}

			if (scope == "merkle") {
				if (pdatas.size() == 6) {
					Object pro;
					pro.push_back(Pair("id", id));
					pro.push_back(Pair("cycle", pindexBest->PegCycle()));

					pro.push_back(Pair("scope", scope));
					pro.push_back(Pair("until", pdatas[1]));
					pro.push_back(Pair("action", pdatas[2]));

					string  brname     = pdatas[3];
					string  merkle     = pdatas[4];
					string  amount_txt = pdatas[5];
					int64_t amount     = strtoll(amount_txt.c_str(), 0, 0);

					pro.push_back(Pair("bridge", brname));
					pro.push_back(Pair("merkle", merkle));
					pro.push_back(Pair("amount", amount));

					Object       votes;
					CBlockIndex* block = pindexBest;
					while (block->nHeight >= cycle_block->nHeight) {
						set<string> txoutids;
						pegdb.ReadProposalBlockVotes(block->GetBlockHash(), id, txoutids);
						for (const string& txoutid : txoutids) {
							string staker;
							if (pegdb.ReadProposalBlockVoteStaker(txoutid, staker))
								votes.push_back(Pair(txoutid, staker));
						}
						block = block->Prev();
					}
					pro.push_back(Pair("votes", votes));
					results.push_back(pro);
				}
			}

			if (scope == "timelockpass") {
				if (pdatas.size() == 4) {
					Object pro;
					pro.push_back(Pair("id", id));
					pro.push_back(Pair("cycle", pindexBest->PegCycle()));

					pro.push_back(Pair("scope", scope));
					pro.push_back(Pair("until", pdatas[1]));
					pro.push_back(Pair("action", pdatas[2]));

					string pubkey_txt = pdatas[3];
					pro.push_back(Pair("pubkey", pubkey_txt));

					Object       votes;
					CBlockIndex* block = pindexBest;
					while (block->nHeight >= cycle_block->nHeight) {
						set<string> txoutids;
						pegdb.ReadProposalBlockVotes(block->GetBlockHash(), id, txoutids);
						for (const string& txoutid : txoutids) {
							string staker;
							if (pegdb.ReadProposalBlockVoteStaker(txoutid, staker))
								votes.push_back(Pair(txoutid, staker));
						}
						block = block->Prev();
					}
					pro.push_back(Pair("votes", votes));
					results.push_back(pro);
				}
			}
		}
	}
	return results;
}
