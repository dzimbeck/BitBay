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
#include "proposals.h"
#include "txdb-leveldb.h"
#include "util.h"

#include <zconf.h>
#include <zlib.h>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

#include <ethc/abi.h>
#include <ethc/hex.h>
#include <ethc/keccak256.h>

// #define MERKLECPP_TRACE_ENABLED 1
#include "merklecpp/merklecpp.h"

using namespace std;
using namespace boost;

static inline bool sha256_keccak_cmp_lt(const uint8_t a[32], const uint8_t b[32]) {
	for (int i = 0; i < 32; i++) {
		if (a[i] < b[i])
			return true;
		if (a[i] > b[i])
			return false;
	}
	return false;
}

static inline void sha256_keccak(const merkle::HashT<32>& l,
                                 const merkle::HashT<32>& r,
                                 merkle::HashT<32>&       out,
                                 bool&                    swap) {
	swap = false;
	uint8_t block[32 * 2];
	if (sha256_keccak_cmp_lt(l.bytes, r.bytes)) {
		memcpy(&block[0], l.bytes, 32);
		memcpy(&block[32], r.bytes, 32);
	} else {
		memcpy(&block[0], r.bytes, 32);
		memcpy(&block[32], l.bytes, 32);
		swap = true;
	}
	eth_keccak256(out.bytes, block, 32 * 2);
}

int CBlockIndex::BridgeCycle() const {
	int nBridgeInterval = Params().BridgeInterval();
	return nHeight / nBridgeInterval;
}

CBlockIndex* CBlockIndex::BridgeCycleBlock() const {
	int          nBridgeInterval = Params().BridgeInterval();
	int          n               = nHeight % nBridgeInterval;
	CBlockIndex* cycle_pindex    = const_cast<CBlockIndex*>(this);
	while (n > 0) {
		cycle_pindex = cycle_pindex->Prev();
		n--;
	}
	return cycle_pindex;
}

CBlockIndex* CBlockIndex::PrevBridgeCycleBlock() const {
	int nBridgeInterval = Params().BridgeInterval();
	int n               = nBridgeInterval;
	// get current cycle block and roll cycle
	CBlockIndex* cycle_pindex = BridgeCycleBlock();
	while (n > 0) {
		if (!cycle_pindex)
			return cycle_pindex;
		cycle_pindex = cycle_pindex->Prev();
		n--;
	}
	return cycle_pindex;
}

bool ConnectConsensusStates(CPegDB& pegdb, CBlockIndex* pindex) {
	if (pindex->nHeight < nPegStartHeight - 1) {
		return true;
	}
	if (pindex->nHeight == nPegStartHeight - 1) {  // pre-peg init state datas
		if (!pegdb.TrustedStakers1Init())
			return error("TrustedStakers1Init() : init failed");
		if (!pegdb.TrustedStakers2Init())
			return error("TrustedStakers2Init() : init failed");
		if (!pegdb.ProposalConsensusInit())
			return error("ProposalConsensusInit() : init failed");
		if (!pegdb.BridgesInit())
			return error("BridgesInit() : init failed");
		if (!pegdb.MerklesInit())
			return error("MerklesInit() : init failed");
		if (!pegdb.TimeLockPassesInit())
			return error("TimeLockPassesInit() : init failed");
	}

	int nPegInterval = Params().PegInterval();
	if (pindex->nHeight % nPegInterval != 0)
		return true;

	// if chains reorganized, cycle data is overwritten here
	uint256 bhash_new_cycle = pindex->GetBlockHash();
	uint256 bhash_old_cycle = pindex->PrevPegCycleBlock()->GetBlockHash();

	// read previous cycle
	uint256 data_hash1;
	if (!pegdb.ReadCycleStateHash(bhash_old_cycle, CChainParams::ACCEPTED_TSTAKERS1, data_hash1))
		return error("ReadCycleStateHash() : ACCEPTED_TSTAKERS1 failed");
	uint256 data_hash2;
	if (!pegdb.ReadCycleStateHash(bhash_old_cycle, CChainParams::ACCEPTED_TSTAKERS2, data_hash2))
		return error("ReadCycleStateHash() : ACCEPTED_TSTAKERS2 failed");
	uint256 data_hash3;
	if (!pegdb.ReadCycleStateHash(bhash_old_cycle, CChainParams::ACCEPTED_CONSENSUS, data_hash3))
		return error("ReadCycleStateHash() : ACCEPTED_CONSENSUS failed");
	uint256 data_hash4;
	if (!pegdb.ReadCycleStateHash(bhash_old_cycle, CChainParams::ACCEPTED_BRIDGES, data_hash4))
		return error("ReadCycleStateHash() : ACCEPTED_BRIDGES failed");
	uint256 data_hash5;
	if (!pegdb.ReadCycleStateHash(bhash_old_cycle, CChainParams::ACCEPTED_MERKLES, data_hash5))
		return error("ReadCycleStateHash() : ACCEPTED_MERKLES failed");
	uint256 data_hash6;
	if (!pegdb.ReadCycleStateHash(bhash_old_cycle, CChainParams::ACCEPTED_TIMELOCKPASSES,
	                              data_hash6))
		return error("ReadCycleStateHash() : ACCEPTED_TIMELOCKPASSES failed");
	uint256 data_hash7;
	if (!pegdb.ReadCycleStateHash(bhash_old_cycle, CChainParams::ACCEPTED_BRIDGES_PAUSE,
	                              data_hash7))
		return error("ReadCycleStateHash() : ACCEPTED_BRIDGES_PAUSE failed");

	// write for current cycle
	if (!pegdb.WriteCycleStateHash(bhash_new_cycle, CChainParams::ACCEPTED_TSTAKERS1, data_hash1))
		return error("WriteCycleStateHash() : ACCEPTED_TSTAKERS1 failed");
	if (!pegdb.WriteCycleStateHash(bhash_new_cycle, CChainParams::ACCEPTED_TSTAKERS2, data_hash2))
		return error("WriteCycleStateHash() : ACCEPTED_TSTAKERS2 failed");
	if (!pegdb.WriteCycleStateHash(bhash_new_cycle, CChainParams::ACCEPTED_CONSENSUS, data_hash3))
		return error("WriteCycleStateHash() : ACCEPTED_CONSENSUS failed");
	if (!pegdb.WriteCycleStateHash(bhash_new_cycle, CChainParams::ACCEPTED_BRIDGES, data_hash4))
		return error("WriteCycleStateHash() : ACCEPTED_BRIDGES failed");
	if (!pegdb.WriteCycleStateHash(bhash_new_cycle, CChainParams::ACCEPTED_MERKLES, data_hash5))
		return error("WriteCycleStateHash() : ACCEPTED_MERKLES failed");
	if (!pegdb.WriteCycleStateHash(bhash_new_cycle, CChainParams::ACCEPTED_TIMELOCKPASSES,
	                               data_hash6))
		return error("WriteCycleStateHash() : ACCEPTED_TIMELOCKPASSES failed");
	if (!pegdb.WriteCycleStateHash(bhash_new_cycle, CChainParams::ACCEPTED_BRIDGES_PAUSE,
	                               data_hash7))
		return error("WriteCycleStateHash() : ACCEPTED_BRIDGES_PAUSE failed");

	// back to 3 intervals
	auto votes_cycle_block = pindex;
	while (votes_cycle_block->nHeight > (pindex->nHeight - nPegInterval * 3)) {
		votes_cycle_block = votes_cycle_block->Prev();
	}
	uint256 bhash_vot_cycle = votes_cycle_block->GetBlockHash();

	// run over current proposals
	set<string> pro_ids;
	pegdb.ReadCycleProposals(bhash_vot_cycle, pro_ids);  // can be empty, ok
	for (const string& pro_id : pro_ids) {
		if (pro_id == "")
			continue;

		// reread consensus if changes in prev proposal
		std::map<int, CChainParams::ConsensusVotes> consensus;
		if (!pindex->ReadConsensusMap(pegdb, consensus))
			return error("ReadConsensusMap() : failed");
		// onbehalf voters
		std::set<string> onbehalf_voted;
		// reread tstakers1
		std::set<string> tstakers1_voted;
		std::set<string> tstakers1;
		if (!pindex->ReadTrustedStakers1(pegdb, tstakers1))
			return error("ReadTrustedStakers1() : failed");
		// reread tstakers2
		std::set<string> tstakers2_voted;
		std::set<string> tstakers2;
		if (!pindex->ReadTrustedStakers2(pegdb, tstakers2))
			return error("ReadTrustedStakers2() : failed");

		// proposal details
		vector<string> pdatas;
		if (pegdb.ReadProposal(pro_id, pdatas)) {
			// just huge nums to skip unknown
			CChainParams::ConsensusVotes need_votes1 = {1000000, 1000000, 1000000};
			CChainParams::ConsensusVotes need_votes2 = {0, 0, 0};
			if (pdatas.size() > 0) {
				string scope = pdatas[0];
				if (scope == "tstakers1") {
					need_votes1 = consensus[CChainParams::CONSENSUS_TSTAKERS];
				} else if (scope == "tstakers2") {
					need_votes1 = consensus[CChainParams::CONSENSUS_TSTAKERS];
				} else if (scope == "consensus") {
					need_votes1 = consensus[CChainParams::CONSENSUS_CONSENSUS];
				} else if (scope == "bridge") {
					need_votes1 = consensus[CChainParams::CONSENSUS_BRIDGE];
				} else if (scope == "merkle") {
					need_votes1 = consensus[CChainParams::CONSENSUS_MERKLE];
				} else if (scope == "timelockpass") {
					need_votes1 = consensus[CChainParams::CONSENSUS_TIMELOCKPASS];
				}
				if (scope == "consensus") {
					if (pdatas.size() == 6) {
						int t1_votes = std::atoi(pdatas[3].c_str());
						int t2_votes = std::atoi(pdatas[4].c_str());
						int o_votes  = std::atoi(pdatas[5].c_str());
						need_votes2  = CChainParams::ConsensusVotes{t1_votes, t2_votes, o_votes};
					}
				}
			}

			CChainParams::ConsensusVotes pro_votes = {0, 0, 0};
			CBlockIndex*                 block     = votes_cycle_block;
			for (int i = 0; i < nPegInterval; i++) {
				set<string> txoutids;
				pegdb.ReadProposalBlockVotes(block->GetBlockHash(), pro_id, txoutids);
				for (const string& txoutid : txoutids) {
					string staker;
					if (pegdb.ReadProposalBlockVoteStaker(txoutid, staker)) {
						if (boost::starts_with(staker, "S:")) {
							staker = staker.substr(2);
							if (onbehalf_voted.count(staker) > 0) {
								continue;
							}
							onbehalf_voted.insert(staker);
						}
						if (tstakers1_voted.count(staker) > 0) {
							continue;
						}
						if (tstakers2_voted.count(staker) > 0) {
							continue;
						}
						if (tstakers1.count(staker) > 0) {
							tstakers1_voted.insert(staker);
							pro_votes.tstakers1++;
						} else if (tstakers2.count(staker) > 0) {
							tstakers2_voted.insert(staker);
							pro_votes.tstakers2++;
						} else {
							pro_votes.ostakers++;
						}
					}
				}
				block = block->Next();
			}
			// final check to accept
			// check pro_votes vs need_votes
			if (CChainParams::isAccepted(pro_votes, need_votes1) &&
			    CChainParams::isAccepted(pro_votes, need_votes2)) {
				string scope;
				if (pdatas.size() > 0) {
					scope = pdatas[0];
				}
				if (scope == "tstakers1") {
					if (pdatas.size() == 3) {
						string until   = pdatas[1];  // skip
						string action  = pdatas[2];
						string address = pdatas[3];
						// apply changes to cycle state
						if (action == "add") {
							std::set<string> tstakers1;
							if (!pindex->ReadTrustedStakers1(pegdb, tstakers1))
								return error("tstakers1 add ReadTrustedStakers1() : failed");
							uint256 shash;
							tstakers1.insert(address);
							if (!pegdb.WriteCycleStateData1(tstakers1, shash))
								return error("tstakers1 add WriteCycleStateData1() : failed");
							if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
							                               CChainParams::ACCEPTED_TSTAKERS1, shash))
								return error("tstakers1 add WriteCycleStateHash() : failed");
						}
						if (action == "remove") {
							std::set<string> tstakers1;
							if (!pindex->ReadTrustedStakers1(pegdb, tstakers1))
								return error("tstakers1 remove ReadTrustedStakers1() : failed");
							uint256 shash;
							tstakers1.erase(address);
							if (!pegdb.WriteCycleStateData1(tstakers1, shash))
								return error("tstakers1 remove WriteCycleStateData1() : failed");
							if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
							                               CChainParams::ACCEPTED_TSTAKERS1, shash))
								return error("tstakers1 remove WriteCycleStateHash() : failed");
						}
					}
				} else if (scope == "tstakers2") {
					if (pdatas.size() == 3) {
						string until   = pdatas[1];  // skip
						string action  = pdatas[2];
						string address = pdatas[3];
						// apply changes to cycle state
						if (action == "add") {
							std::set<string> tstakers2;
							if (!pindex->ReadTrustedStakers2(pegdb, tstakers2))
								return error("tstakers2 add ReadTrustedStakers2() : failed");
							uint256 shash;
							tstakers2.insert(address);
							if (!pegdb.WriteCycleStateData1(tstakers2, shash))
								return error("tstakers2 add WriteCycleStateData1() : failed");
							if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
							                               CChainParams::ACCEPTED_TSTAKERS2, shash))
								return error("tstakers2 add WriteCycleStateHash() : failed");
						}
						if (action == "remove") {
							std::set<string> tstakers2;
							if (!pindex->ReadTrustedStakers2(pegdb, tstakers2))
								return error("tstakers2 remove ReadTrustedStakers2() : failed");
							uint256 shash;
							tstakers2.erase(address);
							if (!pegdb.WriteCycleStateData1(tstakers2, shash))
								return error("tstakers2 remove WriteCycleStateData1() : failed");
							if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
							                               CChainParams::ACCEPTED_TSTAKERS2, shash))
								return error("tstakers2 remove WriteCycleStateHash() : failed");
						}
					}
				}

				if (scope == "consensus") {
					if (pdatas.size() == 6) {
						string                       until          = pdatas[1];  // skip
						string                       consensus_type = pdatas[2];
						int                          t1_votes       = std::atoi(pdatas[3].c_str());
						int                          t2_votes       = std::atoi(pdatas[4].c_str());
						int                          o_votes        = std::atoi(pdatas[5].c_str());
						CChainParams::ConsensusVotes cvotes{t1_votes, t2_votes, o_votes};
						std::map<int, CChainParams::ConsensusVotes> updated_consensus;
						if (!pindex->ReadConsensusMap(pegdb, updated_consensus))
							return error("consensus ReadConsensusMap() : failed");
						if (consensus_type == "tstakers") {
							updated_consensus[CChainParams::CONSENSUS_TSTAKERS] = cvotes;
						} else if (consensus_type == "consensus") {
							updated_consensus[CChainParams::CONSENSUS_CONSENSUS] = cvotes;
						} else if (consensus_type == "bridge") {
							updated_consensus[CChainParams::CONSENSUS_BRIDGE] = cvotes;
						} else if (consensus_type == "merkle") {
							updated_consensus[CChainParams::CONSENSUS_MERKLE] = cvotes;
						} else if (consensus_type == "timelockpass") {
							updated_consensus[CChainParams::CONSENSUS_TIMELOCKPASS] = cvotes;
						}
						uint256 shash;
						if (!pegdb.WriteCycleStateData2(updated_consensus, shash))
							return error("consensus WriteCycleStateData2() : failed");
						if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
						                               CChainParams::ACCEPTED_CONSENSUS, shash))
							return error("consensus WriteCycleStateHash() : failed");
					}
				}

				if (scope == "bridge") {
					if (pdatas.size() == 10) {
						string until  = pdatas[1];  // skip
						string action = pdatas[2];

						// apply changes to cycle state
						if (action == "add") {
							string name           = pdatas[3];
							string symb           = pdatas[4];
							string urls_txt       = pdatas[5];
							string chain_id_txt   = pdatas[6].c_str();
							string contract       = pdatas[7];
							string pegsteps_txt   = pdatas[8].c_str();
							string microsteps_txt = pdatas[9].c_str();

							vector<string> bdata;
							bdata.push_back(name);
							bdata.push_back(symb);
							bdata.push_back(urls_txt);
							bdata.push_back(chain_id_txt);
							bdata.push_back(contract);
							bdata.push_back(pegsteps_txt);
							bdata.push_back(microsteps_txt);

							map<string, vector<string>> bridges;
							if (!pindex->ReadBridgesMap(pegdb, bridges))
								return error("bridge add ReadBridgesMap() : failed");
							uint256 shash;
							bridges[name] = bdata;
							if (!pegdb.WriteCycleStateData3(bridges, shash))
								return error("bridge add WriteCycleStateData3() : failed");
							if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
							                               CChainParams::ACCEPTED_BRIDGES, shash))
								return error("bridge add WriteCycleStateHash() : failed");
						}
					}
				}

				if (scope == "bridge") {
					if (pdatas.size() == 4) {
						string until  = pdatas[1];  // skip
						string action = pdatas[2];

						// apply changes to cycle state
						if (action == "remove") {
							string name = pdatas[3];

							vector<string> bdata;
							bdata.push_back(name);

							map<string, vector<string>> bridges;
							if (!pindex->ReadBridgesMap(pegdb, bridges))
								return error("bridge remove ReadBridgesMap() : failed");
							uint256 shash;
							bridges.erase(name);
							if (!pegdb.WriteCycleStateData3(bridges, shash))
								return error("bridge remove WriteCycleStateData3() : failed");
							if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
							                               CChainParams::ACCEPTED_BRIDGES, shash))
								return error("bridge remove WriteCycleStateHash() : failed");
						}
					}
				}

				if (scope == "merkle") {
					if (pdatas.size() == 6) {
						string until  = pdatas[1];  // skip
						string action = pdatas[2];

						// apply changes to cycle state
						if (action == "add") {
							string brname     = pdatas[3];
							string merkle     = pdatas[4];
							string amount_txt = pdatas[5];

							bool pause = false;
							if (!pindex->ReadBridgesPause(pegdb, pause))
								return error("merkle ReadBridgesPause() : failed");
							string merkle_pause =
							    "899104002d6f8e009ec6dee6844cba6603d02dc351812e422c9e166e3e670506";
							string merkle_resume =
							    "b677f2790c4aabe172484a329694cfb54ec9cd93b38a3d2082b6d456f5e96f2d";
							if (merkle == merkle_pause) {
								uint256          shash;
								std::set<string> datas;
								datas.insert("true");
								if (!pegdb.WriteCycleStateData1(datas, shash))
									return error("merkle WriteCycleStateData1() : failed1");
								if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
								                               CChainParams::ACCEPTED_BRIDGES_PAUSE,
								                               shash))
									return error("merkle WriteCycleStateHash() : failed2");
							} else if (merkle == merkle_resume) {
								uint256          shash;
								std::set<string> datas;
								datas.insert("false");
								if (!pegdb.WriteCycleStateData1(datas, shash))
									return error("merkle WriteCycleStateData1() : failed2");
								if (!pegdb.WriteCycleStateHash(bhash_new_cycle,
								                               CChainParams::ACCEPTED_BRIDGES_PAUSE,
								                               shash))
									return error("merkle WriteCycleStateHash() : failed2");
							} else if (!pause) {
								CMerkleInfo merkle_info;
								merkle_info.hash   = merkle;
								merkle_info.bridge = brname;
								{
									CDataStream ss(SER_GETHASH, 0);
									ss << merkle_info.bridge;
									merkle_info.brhash = Hash(ss.begin(), ss.end()).GetHex();
								}
								merkle_info.amount       = strtoll(amount_txt.c_str(), 0, 0);
								merkle_info.ntime        = pindex->nHeight;
								CMerkleInfo merkle_check = pindex->ReadMerkleIn(pegdb, merkle);
								if (merkle_check.hash.empty()) {
									uint256 map_end;
									pegdb.ReadCycleStateHash(
									    bhash_new_cycle, CChainParams::ACCEPTED_MERKLES, map_end);
									uint256 shash;
									if (!pegdb.AppendCycleStateMapItem1(
									        bhash_new_cycle, "ACCEPTED_MERKLES", map_end, merkle,
									        merkle_info.Serialize(), shash))
										return error("merkle AppendCycleStateMapItem1() : failed");
									if (!pegdb.WriteCycleStateHash(
									        bhash_new_cycle, CChainParams::ACCEPTED_MERKLES, shash))
										return error("merkle WriteCycleStateHash() : failed");
								}
							}
						}
					}
				}

				if (scope == "timelockpass") {
					if (pdatas.size() == 4) {
						string until  = pdatas[1];  // skip
						string action = pdatas[2];

						// apply changes to cycle state
						if (action == "add") {
							string      pubkey_txt = pdatas[3];
							set<string> pubkeys;
							if (!pindex->ReadTimeLockPasses(pegdb, pubkeys))
								return error("timelockpass add ReadTimeLockPasses() : failed");
							uint256 shash;
							pubkeys.insert(pubkey_txt);
							if (!pegdb.WriteCycleStateData1(pubkeys, shash))
								return error("timelockpass add WriteCycleStateData1() : failed");
							if (!pegdb.WriteCycleStateHash(
							        bhash_new_cycle, CChainParams::ACCEPTED_TIMELOCKPASSES, shash))
								return error("timelockpass add WriteCycleStateHash() : failed");
						}
						// apply changes to cycle state
						if (action == "remove") {
							string      pubkey_txt = pdatas[3];
							set<string> pubkeys;
							if (!pindex->ReadTimeLockPasses(pegdb, pubkeys))
								return error("timelockpass remove ReadTimeLockPasses() : failed");
							uint256 shash;
							pubkeys.erase(pubkey_txt);
							if (!pegdb.WriteCycleStateData1(pubkeys, shash))
								return error("timelockpass remove WriteCycleStateData1() : failed");
							if (!pegdb.WriteCycleStateHash(
							        bhash_new_cycle, CChainParams::ACCEPTED_TIMELOCKPASSES, shash))
								return error("timelockpass remove WriteCycleStateHash() : failed");
						}
					}
				}
			}
		}
	}

	return true;
}

bool CalculateBlockBridgeVotes(const CBlock& cblock,
                               CBlockIndex*  pindex,
                               CTxDB&        txdb,
                               CPegDB&       pegdb,
                               string        staker_addr) {
	uint256 bhash = pindex->GetBlockHash();

	if (!staker_addr.empty()) {
		// calculate proposal votes
		CBlockIndex* cycle_pindex = pindex->PegCycleBlock();
		uint256      chash        = cycle_pindex->GetBlockHash();

		const CTransaction& tx = cblock.vtx[1];

		for (size_t i = 0; i < tx.vout.size(); i++) {
			string notary;
			if (tx.vout[i].scriptPubKey.ToNotary(notary)) {
				map<string, vector<string>> bridges;
				if (!pindex->ReadBridgesMap(pegdb, bridges))
					return false;

				// notary is ready, count votes
				string         address_voter = staker_addr;
				string         phash;
				string         address_override;
				vector<string> pdatas = NotaryToProposal(notary, bridges, phash, address_override);

				if (!phash.empty()) {
					// update "cycle proposals", "proposal data"
					// [cycle block hash] - proposals (ok on switch between chains)
					{
						bool        to_add = true;
						set<string> ids;
						set<string> ids_new;
						pegdb.ReadCycleProposals(chash, ids);
						for (const string& id : ids) {
							if (id == "")
								continue;
							if (id == phash) {
								to_add = false;
								continue;
							}
							ids_new.insert(id);
						}
						if (to_add) {
							ids_new.insert(phash);
							if (!pegdb.WriteCycleProposals(chash, ids_new))
								return false;
							// check proposal data is already there, if not then write

							if (!pegdb.ReadProposal(phash, pdatas)) {
								if (!pegdb.WriteProposal(phash, pdatas))
									return false;
							}
						}
					}

					// update txoutids for this block
					// [block hash] . [proposal] - txoutids (ok on switch, bound to block)
					{
						bool        to_add  = true;
						string      txoutid = tx.GetHash().ToString() + ":" + std::to_string(i);
						set<string> txoutids;
						set<string> txoutids_new;
						pegdb.ReadProposalBlockVotes(bhash, phash, txoutids);
						for (const string& id : txoutids) {
							if (id == "")
								continue;
							if (id == txoutid) {
								to_add = false;
								continue;
							}
							txoutids_new.insert(id);
						}
						if (to_add) {
							txoutids_new.insert(txoutid);
							if (!pegdb.WriteProposalBlockVotes(bhash, phash, txoutids_new))
								return false;
						}
					}

					if (!address_override.empty() && address_override != "invalid") {
						address_voter = "S:" + address_override;
					}

					// update staker of txoutid for this block
					// [txoutid] - staker (ok on switch, bound to txoutid)
					{
						string txoutid = tx.GetHash().ToString() + ":" + std::to_string(i);
						if (!pegdb.WriteProposalBlockVoteStaker(txoutid, address_voter))
							return false;
					}
				}
			}
		}
	}

	// pickup Z bridge burns
	for (size_t i = 0; i < cblock.vtx.size(); i++) {
		const CTransaction& tx = cblock.vtx[i];
		for (size_t j = 0; j < tx.vout.size(); j++) {
			string notary;
			if (tx.vout[j].scriptPubKey.ToNotary(notary)) {
				// notary, record burns by the block
				if (boost::starts_with(notary, "**Z**")) {
					string brdata = notary.substr(5);
					if (brdata.size() == (64 + 42)) {
						string br_hash  = brdata.substr(0, 64);
						string dst_addr = brdata.substr(64, 42);
						if (boost::starts_with(dst_addr, "0x") && IsHex(dst_addr.substr(2))) {
							bool   to_add = true;
							string txoutid =
							    tx.GetHash().ToString() + ":" + std::to_string(j) + ":" + dst_addr;
							set<string> txoutids;
							set<string> txoutids_new;
							pegdb.ReadBlockBurnsToBridge(bhash, br_hash, txoutids);
							for (const string& id : txoutids) {
								if (id == "")
									continue;
								if (id == txoutid) {
									to_add = false;
									continue;
								}
								txoutids_new.insert(id);
							}
							if (to_add) {
								txoutids_new.insert(txoutid);
								if (!pegdb.WriteBlockBurnsToBridge(bhash, br_hash, txoutids_new))
									return false;
							}
						}
					}
				}
			}
		}
	}

	return true;
}

/*!
 * \brief FetchBridgePoolsFractions
 * \param pegdb
 * \param pindex
 * \param mapQueuedFractionsChanges
 * \return
 * Reads "exit" fractions of previous block (nHeight-1)
 * The call does not write fractions to db, instead they are
 * placed into mapQueuedFractionsChanges to be written at the
 * end of block transactions connects and validation.
 */
bool FetchBridgePoolsFractions(CPegDB&       pegdb,
                               CBlockIndex*  pindex,
                               MapFractions& mapQueuedFractionsChanges) {
	if (pindex->nHeight < nPegStartHeight - 1) {
		return true;
	}

	int bridge_block_nout     = pindex->nHeight;
	int bridge_block_nout_pre = bridge_block_nout - 1;

	map<string, vector<string>> bridges;
	CBlockIndex*                pindex_pegcycle = pindex->PegCycleBlock();
	pindex_pegcycle->ReadBridgesMap(pegdb, bridges);
	for (const auto& it : bridges) {
		string brname = it.first;
		string brhash_txt;
		{
			CDataStream ss(SER_GETHASH, 0);
			ss << brname;
			brhash_txt = Hash(ss.begin(), ss.end()).GetHex();
		}
		uint256 brhash(brhash_txt);
		// read prev (nHeight-1) block pool
		auto       fkey_pre = uint320(brhash, bridge_block_nout_pre);
		CFractions fractions(0, CFractions::VALUE);
		if (!pegdb.ReadFractions(fkey_pre, fractions, false)) {
			return false;
		}
		fractions                           = fractions.Std();  // fractions pool pre cycle
		auto fkey_new                       = uint320(brhash, bridge_block_nout);
		mapQueuedFractionsChanges[fkey_new] = fractions;
	}
	return true;
}

bool ConnectBridgeCycleBurns(CPegDB&       pegdb,
                             CBlockIndex*  pindex,
                             MapFractions& mapQueuedFractionsChanges) {
	if (pindex->nHeight < nPegStartHeight - 1) {
		return true;
	}
	int nBridgeInterval = Params().BridgeInterval();
	if (pindex->nHeight % nBridgeInterval != 0) {
		return true;
	}

	int bridge_block_nout = pindex->nHeight;
	int nPegInterval      = Params().PegInterval();

	// back to 1 intervals
	auto    bridge_cycle_block = pindex->Prev()->PrevBridgeCycleBlock();
	uint256 bhash_bridge_cycle = bridge_cycle_block->GetBlockHash();

	// list of bridges during the cycle
	map<string, int>            bridges_sections;
	map<string, vector<string>> bridges;
	set<string>                 bridge_hashes;

	// get fractions of pools
	map<string, CFractions>  bridges_burn_fractions;
	map<string, set<string>> bridge_cycle_burn_txouts;

	// get list of Z burns over bridge interval
	CBlockIndex* pindex_in_bridge_cycle = bridge_cycle_block;
	for (int i = 0; i < nBridgeInterval; i++) {
		// read over the block
		uint256 bhash = pindex_in_bridge_cycle->GetBlockHash();

		// update bridges info if peg cycle passes
		if (pindex_in_bridge_cycle->nHeight % nPegInterval == 0) {
			pindex_in_bridge_cycle->ReadBridgesMap(pegdb, bridges);
			for (const auto& it : bridges) {
				string brname_txt = it.first;
				string brhash_txt;
				{
					CDataStream ss(SER_GETHASH, 0);
					ss << brname_txt;
					brhash_txt = Hash(ss.begin(), ss.end()).GetHex();
				}
				if (!bridge_hashes.count(brhash_txt)) {  // new one added
					bridge_hashes.insert(brhash_txt);
					uint256 brhash(brhash_txt);
					auto    brfkey = uint320(brhash, bridge_block_nout);
					if (!mapQueuedFractionsChanges.count(brfkey)) {
						mapQueuedFractionsChanges[brfkey] = CFractions();
					}
				}
			}
		}

		for (const auto& it : bridges) {
			vector<string> brdata     = it.second;
			int            pegsteps   = std::atoi(brdata[5].c_str());
			int            microsteps = std::atoi(brdata[6].c_str());
			if (pegsteps < 1 || microsteps < 1) {
				continue;  // wrong bridge steps
			}
			string brname = it.first;
			string brhash;
			{
				CDataStream ss(SER_GETHASH, 0);
				ss << brname;
				brhash = Hash(ss.begin(), ss.end()).GetHex();
			}
			// use pindex->nPegSupplyIndex to apply pegindex of
			// currently processed block (+2 bridge intervals from first)
			int section              = (pindex->nPegSupplyIndex * pegsteps) / PEG_SIZE;
			bridges_sections[brhash] = section;
			// ready to read Z txouts of the block
			set<string> txoutids;
			pegdb.ReadBlockBurnsToBridge(bhash, brhash, txoutids);
			for (const string& txout_to_addr : txoutids) {
				// read pegdata for burns
				vector<string> txhashnout_args;
				boost::split(txhashnout_args, txout_to_addr, boost::is_any_of(":"));
				if (txhashnout_args.size() != 3)
					return false;
				// fractions
				string  txhash_str  = txhashnout_args[0];
				int     nout        = std::stoi(txhashnout_args[1]);
				string  dstaddr_str = txhashnout_args[2];
				string  txoutid_str = txhashnout_args[0] + ":" + txhashnout_args[1];
				uint256 txhash;
				txhash.SetHex(txhash_str);
				auto       fkey = uint320(txhash, nout);
				CFractions fractions(0, CFractions::VALUE);
				if (!pegdb.ReadFractions(fkey, fractions, true)) {
					return false;
				}
				fractions = fractions.Std();  // fractions of burned coins

				CFractions& br_fractions = bridges_burn_fractions[brhash];
				br_fractions += fractions;

				// can calculate the leaf
				CCompressedFractions fc1(fractions, section, pegsteps, microsteps);

				char* addr_cstr = new char[dstaddr_str.length() + 1];
				strcpy(addr_cstr, dstaddr_str.c_str());
				char* txid_cstr = new char[txoutid_str.length() + 1];
				strcpy(txid_cstr, txoutid_str.c_str());
				size_t txid_len     = txoutid_str.length();
				char*  abi_hex_cstr = NULL;
				size_t abi_hexlen;

				// abi start
				struct eth_abi abi;
				eth_abi_init(&abi, ETH_ABI_ENCODE);
				eth_abi_address(&abi, &addr_cstr);
				for (int i = 0; i < fc1.nPegSteps; i++) {
					uint64_t psv = fc1.fps[i];
					eth_abi_uint64(&abi, &psv);
				}
				for (int i = 0; i < fc1.nMicroSteps; i++) {
					uint64_t msv = fc1.fms[i];
					eth_abi_uint64(&abi, &msv);
				}
				eth_abi_bytes(&abi, (uint8_t**)(&txid_cstr), &txid_len);
				eth_abi_to_hex(&abi, &abi_hex_cstr, &abi_hexlen);
				eth_abi_free(&abi);
				// abi end

				uint8_t* abi_bin_data;
				int      len_abi_bin = eth_hex_to_bytes(&abi_bin_data, abi_hex_cstr, abi_hexlen);
				if (len_abi_bin <= 0) {
					return false;
				}
				uint8_t keccak[32];
				int     keccak_ok = eth_keccak256(keccak, (uint8_t*)abi_bin_data, len_abi_bin);
				if (keccak_ok <= 0) {
					return false;
				}
				char* leaf_hex_cstr;
				int   leaf_hex_len = eth_hex_from_bytes(&leaf_hex_cstr, keccak, 32);
				if (leaf_hex_len < 0) {
					return false;
				}
				string leaf_hex        = string(leaf_hex_cstr);
				string txout_addr_leaf = txout_to_addr + ":" + leaf_hex + ":" +
				                         std::to_string(fc1.nPegSteps + fc1.nMicroSteps);
				for (int i = 0; i < fc1.nPegSteps; i++) {
					uint64_t psv = fc1.fps[i];
					txout_addr_leaf += ":" + std::to_string(psv);
				}
				for (int i = 0; i < fc1.nMicroSteps; i++) {
					uint64_t msv = fc1.fms[i];
					txout_addr_leaf += ":" + std::to_string(msv);
				}

				// collect in cycle burns with leaves
				set<string>& bridge_cycle_burn_txouts_per_bridge = bridge_cycle_burn_txouts[brhash];
				bridge_cycle_burn_txouts_per_bridge.insert(txout_addr_leaf);

				delete[] addr_cstr;
				delete[] txid_cstr;
				free(abi_hex_cstr);
				free(abi_bin_data);
				free(leaf_hex_cstr);
			}
		}

		// next
		pindex_in_bridge_cycle = pindex_in_bridge_cycle->Next();
	}

	// write used in this cycle bridge hashes
	if (!pegdb.WriteBridgeCycleBridgeHashes(bhash_bridge_cycle, bridge_hashes))
		return false;

	// write bridge cycle burns txouts
	for (const auto& it : bridge_cycle_burn_txouts) {
		string             br_hash                             = it.first;
		const set<string>& bridge_cycle_burn_txouts_per_bridge = it.second;
		set<string>        bridge_cycle_burn_txouts_per_bridge_with_receipt;
		// build merkle tree
		merkle::TreeT<32, sha256_keccak> tree;
		set<string>                      leaves_sorted;
		for (const string& txout_addr_leaf : bridge_cycle_burn_txouts_per_bridge) {
			vector<string> args;
			boost::split(args, txout_addr_leaf, boost::is_any_of(":"));
			if (args.size() < 5)
				return false;
			string leaf = args[3];
			leaves_sorted.insert(leaf);
		}
		int              idx = 0;
		map<string, int> leaf_to_merkle_idx;
		for (const string& leaf : leaves_sorted) {
			tree.insert(leaf);
			leaf_to_merkle_idx[leaf] = idx;
			idx++;
		}
		auto   merkle_root = tree.root();
		string merkle_str  = merkle_root.to_string();
		int    section     = bridges_sections[br_hash];
		// write ready merkle tree
		if (!pegdb.WriteBridgeCycleMerkle(bhash_bridge_cycle, br_hash,
		                                  merkle_str + ":" + std::to_string(section)))
			return false;
		// get receipts ready
		for (string txout_addr_leaf : bridge_cycle_burn_txouts_per_bridge) {
			vector<string> args;
			boost::split(args, txout_addr_leaf, boost::is_any_of(":"));
			string         leaf = args[3];
			int            idx  = leaf_to_merkle_idx[leaf];
			vector<string> leaf_path_elms;
			auto           leaf_path = tree.path(idx);
			for (size_t i = 0; i < leaf_path->size(); i++) {
				const auto& leaf_path_elm     = (*leaf_path)[i];
				string      leaf_path_elm_txt = leaf_path_elm.to_string();
				leaf_path_elms.push_back(leaf_path_elm_txt);
			}
			// +proofs
			txout_addr_leaf += ":" + std::to_string(leaf_path_elms.size());
			for (const string& proof_elm : leaf_path_elms) {
				txout_addr_leaf += ":" + proof_elm;
			}
			// data ready for bridge cycle
			bridge_cycle_burn_txouts_per_bridge_with_receipt.insert(txout_addr_leaf);
		}
		// write for it for bridge cycle
		if (!pegdb.WriteBridgeCycleBurnsToBridge(bhash_bridge_cycle, br_hash,
		                                         bridge_cycle_burn_txouts_per_bridge_with_receipt))
			return false;
	}

	// transit bridges_fractions, expect fractions ready in the map
	for (const string& brhash_txt : bridge_hashes) {
		uint256 brhash(brhash_txt);
		auto    fkey = uint320(brhash, bridge_block_nout);
		if (!mapQueuedFractionsChanges.count(fkey)) {
			return false;
		}
		CFractions fractions = mapQueuedFractionsChanges[fkey];
		fractions            = fractions.Std();
		// append burns to the pool
		if (bridges_burn_fractions.count(brhash.GetHex())) {
			fractions += bridges_burn_fractions[brhash.GetHex()];
		}
		mapQueuedFractionsChanges[fkey] = fractions;
	}

	return true;
}

bool ComputeMintMerkleLeaf(const string&   dest_addr_str,
                           vector<int64_t> sections,
                           int             section_peg,
                           int             nonce,
                           const string&   from,
                           string&         out_leaf_hex) {
	char* dest_addr_cstr = new char[dest_addr_str.length() + 1];
	strcpy(dest_addr_cstr, dest_addr_str.c_str());
	size_t dest_addr_len  = dest_addr_str.length();
	char*  from_addr_cstr = new char[from.length() + 1];
	strcpy(from_addr_cstr, from.c_str());
	char*  abi_hex_cstr = NULL;
	size_t abi_hexlen;

	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_ENCODE);
	eth_abi_bytes(&abi, (uint8_t**)(&dest_addr_cstr), &dest_addr_len);
	for (size_t i = 0; i < sections.size(); i++) {
		uint64_t sv = uint64_t(sections[i]);
		eth_abi_uint64(&abi, &sv);
	}
	uint64_t section_peg_u = uint64_t(section_peg);
	eth_abi_uint64(&abi, &section_peg_u);
	uint64_t nonce_u = uint64_t(nonce);
	eth_abi_uint64(&abi, &nonce_u);
	eth_abi_address(&abi, &from_addr_cstr);
	eth_abi_to_hex(&abi, &abi_hex_cstr, &abi_hexlen);
	eth_abi_free(&abi);
	// abi end

	uint8_t* abi_bin_data;
	int      len_abi_bin = eth_hex_to_bytes(&abi_bin_data, abi_hex_cstr, abi_hexlen);
	if (len_abi_bin <= 0) {
		return false;
	}
	uint8_t keccak[32];
	int     keccak_ok = eth_keccak256(keccak, (uint8_t*)abi_bin_data, len_abi_bin);
	if (keccak_ok <= 0) {
		return false;
	}
	char* leaf_hex_cstr;
	int   leaf_hex_len = eth_hex_from_bytes(&leaf_hex_cstr, keccak, 32);
	if (leaf_hex_len < 0) {
		return false;
	}
	out_leaf_hex = string(leaf_hex_cstr);
	delete[] dest_addr_cstr;
	delete[] from_addr_cstr;
	free(abi_hex_cstr);
	free(abi_bin_data);
	free(leaf_hex_cstr);
	return true;
}

bool ComputeMintMerkleRoot(const string&  inp_leaf_hex,
                           vector<string> proofs,
                           string&        out_root_hex) {
	if (proofs.size() == 0) {
		out_root_hex = inp_leaf_hex;
		return true;
	}
	string inp_branch = inp_leaf_hex;
	string out_branch = inp_leaf_hex;
	for (const string& proof : proofs) {
		string branch1;
		string branch2;
		int    cmp = proof.compare(inp_branch);
		if (cmp > 0) {
			branch1 = inp_branch;
			branch2 = proof;
		} else if (cmp < 0) {
			branch1 = proof;
			branch2 = inp_branch;
		} else {
			return false;  // can not match
		}

		char* branch1_cstr = new char[branch1.length() + 1];
		strcpy(branch1_cstr, branch1.c_str());
		uint8_t* branch1_bin_data;
		int len_branch1_bin = eth_hex_to_bytes(&branch1_bin_data, branch1_cstr, branch1.length());
		if (len_branch1_bin != 32) {
			delete[] branch1_cstr;
			if (len_branch1_bin > 0)
				free(branch1_bin_data);
			return false;
		}
		char* branch2_cstr = new char[branch2.length() + 1];
		strcpy(branch2_cstr, branch2.c_str());
		uint8_t* branch2_bin_data;
		int len_branch2_bin = eth_hex_to_bytes(&branch2_bin_data, branch2_cstr, branch2.length());
		if (len_branch2_bin != 32) {
			delete[] branch1_cstr;
			delete[] branch2_cstr;
			if (len_branch1_bin > 0)
				free(branch1_bin_data);
			if (len_branch2_bin > 0)
				free(branch2_bin_data);
			return false;
		}

		char*  abi_hex_cstr = NULL;
		size_t abi_hexlen;

		// abi start
		struct eth_abi abi;
		eth_abi_init(&abi, ETH_ABI_ENCODE);
		eth_abi_bytes32(&abi, branch1_bin_data);
		eth_abi_bytes32(&abi, branch2_bin_data);
		eth_abi_to_hex(&abi, &abi_hex_cstr, &abi_hexlen);
		eth_abi_free(&abi);
		// abi end

		uint8_t* abi_bin_data;
		int      len_abi_bin = eth_hex_to_bytes(&abi_bin_data, abi_hex_cstr, abi_hexlen);
		if (len_abi_bin <= 0) {
			return false;
		}
		uint8_t keccak[32];
		int     keccak_ok = eth_keccak256(keccak, (uint8_t*)abi_bin_data, len_abi_bin);
		if (keccak_ok <= 0) {
			return false;
		}
		char* out_branch_hex_cstr;
		int   leaf_hex_len = eth_hex_from_bytes(&out_branch_hex_cstr, keccak, 32);
		if (leaf_hex_len < 0) {
			return false;
		}
		out_branch = string(out_branch_hex_cstr);
		delete[] branch1_cstr;
		delete[] branch2_cstr;
		free(branch1_bin_data);
		free(branch2_bin_data);
		free(abi_hex_cstr);
		free(abi_bin_data);
		free(out_branch_hex_cstr);
		inp_branch = out_branch;
	}
	out_root_hex = out_branch;
	return true;
}

static int64_t getNumberFromScript(opcodetype opcode, const vector<unsigned char>& vch) {
	int64_t v = 0;
	if (opcode >= OP_1 && opcode <= OP_16) {
		v = opcode - OP_1 + 1;
		return v;
	}
	if (vch.empty()) {
		return 0;
	}
	for (size_t i = 0; i != vch.size(); ++i)
		v |= static_cast<int64_t>(vch[i]) << 8 * i;
	if (vch.size() == 8 && vch.back() & 0x80) {
		v = -(v & ~(0x80 << (8 * (vch.size() - 1))));
		return v;
	}
	return v;
}

bool DecodeAndValidateMintSigScript(const CScript&   scriptSig,
                                    CBitcoinAddress& addr_dest,
                                    vector<int64_t>& sections,
                                    int&             section_peg,
                                    int&             nonce,
                                    string&          from,
                                    string&          leaf,
                                    vector<string>&  proofs) {
	int sections_size = 0;
	int proofs_size   = 0;

	int                     idx = 0;
	opcodetype              opcode;
	vector<unsigned char>   vch;
	CScript::const_iterator pc = scriptSig.begin();
	while (pc < scriptSig.end()) {
		if (!scriptSig.GetOp(pc, opcode, vch)) {
			return error("DecodeAndValidateMintSigScript() : CoinMint sigScript decode err1: %s",
			             scriptSig.ToString());
		}
		if (0 <= opcode && opcode <= OP_16) {
			if (idx == 0) {
				vector<unsigned char> vch_pref1 =
				    Params().Base58Prefix(CChainParams::PUBKEY_ADDRESS);
				vector<unsigned char> vch_pref2 =
				    Params().Base58Prefix(CChainParams::SCRIPT_ADDRESS);
				if (boost::starts_with(vch, vch_pref1)) {
					vector<unsigned char> vch_data(vch.begin() + vch_pref1.size(), vch.end());
					addr_dest.SetData(vch_pref1, vch_data);
				}
				if (boost::starts_with(vch, vch_pref2)) {
					vector<unsigned char> vch_data(vch.begin() + vch_pref2.size(), vch.end());
					addr_dest.SetData(vch_pref2, vch_data);
				}

			} else if (idx == 1) {
				sections_size = getNumberFromScript(opcode, vch);
			} else if (idx >= 2 && idx < (2 + sections_size)) {
				int64_t f = getNumberFromScript(opcode, vch);
				sections.push_back(f);
			} else if (idx == 2 + sections_size) {
				section_peg = getNumberFromScript(opcode, vch);
			} else if (idx == 3 + sections_size) {
				nonce = getNumberFromScript(opcode, vch);
			} else if (idx == 4 + sections_size) {
				from = ValueString(vch);
			} else if (idx == 5 + sections_size) {
				leaf = ValueString(vch);
			} else if (idx == 6 + sections_size) {
				proofs_size = getNumberFromScript(opcode, vch);
			} else if (idx >= 7 + sections_size && idx < (7 + sections_size + proofs_size)) {
				string proof = ValueString(vch);
				proofs.push_back(proof);
			} else {
				std::string str = ValueString(vch);
			}
		} else {
			return error(
			    "DecodeAndValidateMintSigScript() : CoinMint sigScript decode %d err2: %s, op "
			    "%d",
			    idx, scriptSig.ToString(), opcode);
		}
		idx++;
	}
	// rebuild sigScript and compare vs input one
	{
		CScript proofSig;
		{
			proofSig << addr_dest.GetData();
			proofSig << int64_t(sections.size());
			for (int64_t f : sections) {
				proofSig << f;
			}
			proofSig << int64_t(section_peg);
			proofSig << int64_t(nonce);
			std::vector<unsigned char> vchSender = ParseHex(from);
			proofSig << vchSender;
			std::vector<unsigned char> vchLeaf = ParseHex(leaf);
			proofSig << vchLeaf;
			proofSig << int64_t(proofs.size());
			for (const string& proof : proofs) {
				std::vector<unsigned char> vchProof = ParseHex(proof);
				proofSig << vchProof;
			}
		}
		if (proofSig != scriptSig) {
			string proofSigHex;
			{
				CDataStream ss(SER_NETWORK, 0);
				ss << proofSig;
				proofSigHex = HexStr(ss.begin(), ss.end());
			}
			string scriptSigHex;
			{
				CDataStream ss(SER_NETWORK, 0);
				ss << scriptSig;
				scriptSigHex = HexStr(ss.begin(), ss.end());
			}
			return error(
			    "DecodeAndValidateMintSigScript() : CoinMint sigScript mismatch recoded: %s vs "
			    "%s, serialized %s vs %s",
			    scriptSig.ToString(), proofSig.ToString(), scriptSigHex, proofSigHex);
		}
	}

	return true;
}
