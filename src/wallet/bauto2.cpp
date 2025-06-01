// Copyright (c) 2024 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "main.h"
#include "pegdb-leveldb.h"
#include "rpcprotocol.h"
#include "txdb-leveldb.h"
#include "wallet.h"

#include <ethc/abi.h>
#include <ethc/hex.h>
#include <ethc/keccak256.h>
#include <merklecpp/merklecpp.h>

#include <curl/curl.h>
#include <boost/algorithm/string.hpp>

using namespace std;
using namespace boost;
using namespace json_spirit;

using std::endl;
using std::string;

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

static size_t call_rpcapi_curl_int_wrt(void* contents, size_t size, size_t nmemb, void* userp) {
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}

static string call_rpcapi_curl_int(CURL* curl, string api_uri, string data) {
	std::string data_out;
	curl_easy_setopt(curl, CURLOPT_URL, api_uri.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, call_rpcapi_curl_int_wrt);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data_out);
	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK)
		return "";
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	if (http_code != 200)
		return "";
	return data_out;
}

static string call_rpcapi_curl(string api_uri, string data) {
	CURL*  curl = curl_easy_init();
	string out  = call_rpcapi_curl_int(curl, api_uri, data);
	curl_easy_cleanup(curl);
	return out;
}

static string call_rpcapi(string api_uri, string contract, string callsel, string data) {
	try {
		Array  params;
		Object arg1;
		arg1.push_back(Pair("from", Value()));
		arg1.push_back(Pair("to", contract));
		arg1.push_back(Pair("data", callsel + data));
		params.push_back(arg1);
		params.push_back("latest");
		string strRequest = JSONRPCRequest("eth_call", params, 1);
		string strReply   = call_rpcapi_curl(api_uri, strRequest);
		// LogPrintf("%s thread call_rpcapi %s %s callsel:%s data:%s out: %s\n", "bitbay-bauto2",
		//           api_uri, contract, callsel, data, strReply);
		if (strReply == "")
			return "";
		string             data;
		json_spirit::Value jval;
		json_spirit::read_string(strReply, jval);
		const json_spirit::Object& jobj = jval.get_obj();
		if (!jobj.empty()) {
			json_spirit::Value data_obj = json_spirit::find_value(jobj, "result");
			if (data_obj.is_null())
				return "";
			data = data_obj.get_str();
		}
		return data;
	} catch (std::exception& e) {
		LogPrintf("%s thread call_rpcapi %s %s callsel:%s data:%s err: %s\n", "bitbay-bauto2",
		          api_uri, contract, callsel, data, e.what());
		return "";
	}
}

static string call_minter(string api_uri, string data_contract) {
	string skip;
	string address_evm;
	string minterSig   = "minter()";
	string minterSel   = "0x07546172";
	string result_data = call_rpcapi(api_uri, data_contract, minterSel, "");
	if (result_data.empty())
		return skip;
	char*  res_cstr = (char*)(result_data.c_str());
	size_t res_len  = result_data.size();
	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_DECODE);
	eth_abi_from_hex(&abi, res_cstr, res_len);
	char* from_addr_cstr = new char[40 + 1];
	eth_abi_address(&abi, &from_addr_cstr);
	address_evm = string(from_addr_cstr, 40);
	delete[] from_addr_cstr;
	eth_abi_free(&abi);
	// abi end
	if (!address_evm.empty()) {
		address_evm = "0x" + address_evm;
	}
	return address_evm;
}

int64_t call_processingTime(string api_uri, string admin_contract, int nonce) {
	int64_t skip              = -1;
	int64_t ptime             = 0;
	string  processingTimeSig = "processingTime(uint256)";
	string  processingTimeSel = "0x71350bc8";
	uint256 nonce256(nonce);
	string result_data = call_rpcapi(api_uri, admin_contract, processingTimeSel, nonce256.GetHex());
	if (result_data.empty())
		return skip;
	char*  res_cstr = (char*)(result_data.c_str());
	size_t res_len  = result_data.size();
	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_DECODE);
	eth_abi_from_hex(&abi, res_cstr, res_len);
	vchtype res_arg_bytes;
	res_arg_bytes.resize(32);
	eth_abi_bytes32(&abi, res_arg_bytes.data());
	char* hash_hex_cstr;
	int   hash_hex_len = eth_hex_from_bytes(&hash_hex_cstr, res_arg_bytes.data(), 32);
	if (hash_hex_len < 0) {
		return skip;
	}
	uint256 d(hash_hex_cstr);
	ptime = int64_t(d.GetLow64());
	eth_abi_free(&abi);
	// abi end
	return ptime;
}

int64_t call_intervaltime(string api_uri, string admin_contract) {
	int64_t skip            = -1;
	int64_t itime           = 0;
	string  intervaltimeSig = "intervaltime()";
	string  intervaltimeSel = "0xb2930e05";
	string  result_data     = call_rpcapi(api_uri, admin_contract, intervaltimeSel, "");
	if (result_data.empty())
		return skip;
	char*  res_cstr = (char*)(result_data.c_str());
	size_t res_len  = result_data.size();
	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_DECODE);
	eth_abi_from_hex(&abi, res_cstr, res_len);
	vchtype res_arg_bytes;
	res_arg_bytes.resize(32);
	eth_abi_bytes32(&abi, res_arg_bytes.data());
	char* hash_hex_cstr;
	int   hash_hex_len = eth_hex_from_bytes(&hash_hex_cstr, res_arg_bytes.data(), 32);
	if (hash_hex_len < 0) {
		return skip;
	}
	uint256 d(hash_hex_cstr);
	itime = int64_t(d.GetLow64());
	eth_abi_free(&abi);
	// abi end
	return itime;
}

vector<string> call_listHashes(string api_uri, string admin_contract, int nonce) {
	vector<string> skip;
	vector<string> hashes;
	string         lishHashesSig = "listHashes(uint256)";
	string         listHashesSel = "0xfe473459";
	uint256        nonce256(nonce);
	string result_data = call_rpcapi(api_uri, admin_contract, listHashesSel, nonce256.GetHex());
	if (result_data.empty())
		return skip;
	char*  res_cstr = (char*)(result_data.c_str());
	size_t res_len  = result_data.size();
	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_DECODE);
	eth_abi_from_hex(&abi, res_cstr, res_len);
	uint64_t res_arr_len = 0;
	eth_abi_array(&abi, &res_arr_len);
	for (uint64_t i = 0; i < res_arr_len; i++) {
		vchtype res_arg_bytes;
		res_arg_bytes.resize(32);
		eth_abi_bytes32(&abi, res_arg_bytes.data());
		char* hash_hex_cstr;
		int   hash_hex_len = eth_hex_from_bytes(&hash_hex_cstr, res_arg_bytes.data(), 32);
		if (hash_hex_len < 0) {
			continue;
		}
		uint256 d(hash_hex_cstr);
		hashes.push_back(d.GetHex());
	}
	eth_abi_array_end(&abi);
	eth_abi_free(&abi);
	// abi end
	return hashes;
}

string call_addresses(string api_uri, string admin_contract, int nonce, int addr_idx) {
	string  skip;
	string  address_evm;
	string  addressesSig = "addresses(uint256,uint256)";
	string  addressesSel = "0x670815a9";
	uint256 nonce256(nonce);
	uint256 addri256(addr_idx);
	string  result_data =
	    call_rpcapi(api_uri, admin_contract, addressesSel, nonce256.GetHex() + addri256.GetHex());
	if (result_data.empty())
		return skip;
	char*  res_cstr = (char*)(result_data.c_str());
	size_t res_len  = result_data.size();
	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_DECODE);
	eth_abi_from_hex(&abi, res_cstr, res_len);
	char* from_addr_cstr = new char[40 + 1];
	eth_abi_address(&abi, &from_addr_cstr);
	address_evm = string(from_addr_cstr, 40);
	delete[] from_addr_cstr;
	eth_abi_free(&abi);
	// abi end
	return address_evm;
}

string call_recipient(string api_uri, string admin_contract, int nonce, string address_evm) {
	string  skip;
	string  address_bay;
	string  recipientSig = "recipient(uint256,address)";
	string  recipientSel = "0xb881c304";
	uint256 nonce256(nonce);
	string  result_data = call_rpcapi(api_uri, admin_contract, recipientSel,
	                                  nonce256.GetHex() + "000000000000000000000000" + address_evm);
	if (result_data.empty())
		return skip;
	char*  res_cstr = (char*)(result_data.c_str());
	size_t res_len  = result_data.size();
	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_DECODE);
	eth_abi_from_hex(&abi, res_cstr, res_len);
	size_t address_bay_len;
	char*  address_bay_cstr;
	eth_abi_bytes(&abi, (uint8_t**)(&address_bay_cstr), &address_bay_len);
	address_bay = string(address_bay_cstr, address_bay_len);
	eth_abi_free(&abi);
	// abi end
	return address_bay;
}

int64_t call_highkey(string api_uri, string admin_contract, int nonce, string address_evm) {
	int64_t skip       = -1;
	int64_t highkey    = 0;
	string  highkeySig = "highkey(uint256,address)";
	string  highkeySel = "0xb2f76f37";
	uint256 nonce256(nonce);
	string  result_data = call_rpcapi(api_uri, admin_contract, highkeySel,
	                                  nonce256.GetHex() + "000000000000000000000000" + address_evm);
	if (result_data.empty())
		return skip;
	char*  res_cstr = (char*)(result_data.c_str());
	size_t res_len  = result_data.size();
	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_DECODE);
	eth_abi_from_hex(&abi, res_cstr, res_len);
	vchtype res_arg_bytes;
	res_arg_bytes.resize(32);
	eth_abi_bytes32(&abi, res_arg_bytes.data());
	char* hash_hex_cstr;
	int   hash_hex_len = eth_hex_from_bytes(&hash_hex_cstr, res_arg_bytes.data(), 32);
	if (hash_hex_len < 0) {
		return skip;
	}
	uint256 d(hash_hex_cstr);
	highkey = int64_t(d.GetLow64());
	eth_abi_free(&abi);
	// abi end
	return highkey;
}

vector<int64_t> call_showReserve(string api_uri,
                                 string admin_contract,
                                 string address_evm,
                                 int    nonce,
                                 int    pegSteps,
                                 int    microSteps) {
	vector<int64_t> skip;
	vector<int64_t> fractions;
	string          showReserveSig = "showReserve(address,uint256)";
	string          showReserveSel = "0x12c0df7e";
	uint256         nonce256(nonce);
	string          result_data = call_rpcapi(api_uri, admin_contract, showReserveSel,
	                                          "000000000000000000000000" + address_evm + nonce256.GetHex());
	if (result_data.empty())
		return skip;
	char*  res_cstr = (char*)(result_data.c_str());
	size_t res_len  = result_data.size();
	// abi start
	struct eth_abi abi;
	eth_abi_init(&abi, ETH_ABI_DECODE);
	eth_abi_from_hex(&abi, res_cstr, res_len);
	uint64_t res_arr_len = pegSteps + microSteps;
	for (uint64_t i = 0; i < res_arr_len; i++) {
		vchtype res_arg_bytes;
		res_arg_bytes.resize(32);
		eth_abi_bytes32(&abi, res_arg_bytes.data());
		char* hash_hex_cstr;
		int   hash_hex_len = eth_hex_from_bytes(&hash_hex_cstr, res_arg_bytes.data(), 32);
		if (hash_hex_len < 0) {
			continue;
		}
		uint256 d(hash_hex_cstr);
		fractions.push_back(int64_t(d.GetLow64()));
	}
	eth_abi_free(&abi);
	// abi end
	if (fractions.size() != size_t(pegSteps + microSteps))
		return skip;
	// all ok
	return fractions;
}

bool get_merkle_amount(string         api_uri,
                       string         admin_contract,
                       int            nonce,
                       int            pegSteps,
                       int            microSteps,
                       vector<string> hashes,
                       int64_t&       amount) {
	for (int i = 0; i < int(hashes.size()); i++) {
		// get the "from" EVM address:
		string address_evm = call_addresses(api_uri, admin_contract, nonce, i);
		if (address_evm.empty())
			return false;
		// get the fractions:
		vector<int64_t> fractions =
		    call_showReserve(api_uri, admin_contract, address_evm, nonce, pegSteps, microSteps);
		if (fractions.empty())
			return false;
		for (size_t j = 0; j < size_t(pegSteps); j++)
			amount += fractions[j];
	}
	return true;
}

void ThreadBrigeAuto2(CWallet* pwallet) {
	SetThreadPriority(THREAD_PRIORITY_LOWEST);

	// Make this thread recognisable as the mining thread
	RenameThread("bitbay-bauto2");
	LogPrintf("%s thread start\n", "bitbay-bauto2");

	MilliSleep(5000);
	// bool first = true;

	while (true) {
		// if (!first)
		// first = false;
		MilliSleep(600000);
		if (GetAdjustedTime() - pindexBest->nTime > 60 * 60) {
			continue;  // wait a sync
		}
		CPegDB      pegdb;
		set<string> sTrustedStakers1, sTrustedStakers2;
		pindexBest->ReadTrustedStakers1(pegdb, sTrustedStakers1);
		pindexBest->ReadTrustedStakers2(pegdb, sTrustedStakers2);

		bool is_trusted_staker1 = false;
		bool is_trusted_staker2 = false;
		bool is_other_staker    = true;
		{
			LOCK(pwallet->cs_wallet);
			for (const pair<CTxDestination, std::string> item : pwallet->mapAddressBook) {
				const CBitcoinAddress& address = item.first;
				bool                   fMine   = IsMine(*pwallet, address.Get());
				if (fMine) {
					string addr_txt = address.ToString();
					if (sTrustedStakers1.count(addr_txt)) {
						is_trusted_staker1 = true;
						is_trusted_staker2 = false;
						is_other_staker    = false;
						break;
					}
					if (sTrustedStakers2.count(addr_txt)) {
						is_trusted_staker1 = false;
						is_trusted_staker2 = true;
						is_other_staker    = false;
						break;
					}
				}
			}
		}

		std::map<int, CChainParams::ConsensusVotes> consensus;
		bool ok = pindexBest->ReadConsensusMap(pegdb, consensus);
		if (!ok)
			continue;

		bool                         need_vote = false;
		CChainParams::ConsensusVotes votes     = consensus[CChainParams::CONSENSUS_MERKLE];
		if (votes.tstakers1 > 0 && is_trusted_staker1)
			need_vote = true;
		if (votes.tstakers2 > 0 && is_trusted_staker2)
			need_vote = true;
		if (votes.ostakers > 0 && is_other_staker)
			need_vote = true;
		if (!need_vote)
			continue;

		map<string, CBridgeInfo> bridges;
		ok = pindexBest->ReadBridges(pegdb, bridges);
		if (!ok)
			continue;

		vector<CMerkleInfo> merkles;
		ok = pindexBest->ReadMerklesIn(pegdb, 0, merkles);
		if (!ok)
			continue;

		for (const auto& it : bridges) {
			string      bridge_name = it.first;
			CBridgeInfo bridge_info = it.second;
			if (bridge_info.urls.empty())
				continue;

			string rpcapi             = *(bridge_info.urls.begin());
			string data_contract_addr = bridge_info.contract;

			string admin_contract_addr = call_minter(rpcapi, data_contract_addr);
			LogPrintf("%s thread: bridge %s, admin_contract_addr: %s\n", "bitbay-bauto2",
			          bridge_name, admin_contract_addr);
			if (admin_contract_addr.empty()) {
				continue;
			}

			int64_t interval_time = call_intervaltime(rpcapi, admin_contract_addr);
			LogPrintf("%s thread: bridge %s, intervaltime: %d\n", "bitbay-bauto2", bridge_name,
			          interval_time);
			if (interval_time < 0) {
				continue;
			}

			for (int nonce = 0;; nonce++) {
				bool completed = false;
				CWalletDB(pwallet->strWalletFile)
				    .ReadCompletedMerkleInNonce(bridge_info.hash, nonce, completed);

				int64_t processing_time = call_processingTime(rpcapi, admin_contract_addr, nonce);
				LogPrintf("%s thread: bridge %s, nonce %d has processing_time: %d\n",
				          "bitbay-bauto2", bridge_name, nonce, processing_time);
				if (processing_time <= 0) {
					continue;
				}
				int64_t now_time = GetTime();
				if (processing_time + interval_time > now_time) {
					LogPrintf(
					    "%s thread: bridge %s, nonce %d has non-mature processing time : %d\n",
					    "bitbay-bauto2", bridge_name, nonce, processing_time);
					continue;
				}

				if (!completed) {
					vector<string> hashes = call_listHashes(rpcapi, admin_contract_addr, nonce);
					if (hashes.empty())
						break;
					LogPrintf("%s thread: bridge %s, todo nonce: %d has hashes\n", "bitbay-bauto2",
					          bridge_name, nonce);

					merkle::TreeT<32, sha256_keccak> tree;
					set<string>                      leaves_sorted;
					for (const string& hash : hashes) {
						leaves_sorted.insert(hash);
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
					LogPrintf("%s thread: bridge %s, todo nonce: %s, computed merkle: %s\n",
					          "bitbay-bauto2", bridge_name, nonce, merkle_str);

					bool mined_merkle = false;
					for (const CMerkleInfo& minfo : merkles) {
						if (bridge_info.hash != minfo.brhash)
							continue;
						if (minfo.hash == merkle_str) {
							mined_merkle = true;
							break;
						}
					}

					if (!mined_merkle) {
						string         proposal_id;
						bool           has_proposal    = false;
						bool           active_proposal = true;
						vector<string> ids;
						CWalletDB(pwallet->strWalletFile).ReadProposals(ids);
						for (const string& id : ids) {
							if (id == "")
								continue;
							vector<string> datas;
							CWalletDB(pwallet->strWalletFile).ReadProposal(id, datas);
							if (datas.size() > 1)
								continue;
							string scope = datas[0];
							if (scope == "merkle") {
								if (datas.size() == 6) {
									string action = datas[2];
									if (action == "add") {
										string proposal_brname = datas[3];
										if (proposal_brname != bridge_info.name)
											continue;
										string proposal_merkle = datas[4];
										if (proposal_merkle == merkle_str) {
											proposal_id  = id;
											has_proposal = true;
											int t_now    = GetTime();
											int t_until  = std::atoi(datas[1].c_str());
											if (t_now > t_until) {
												active_proposal = false;
											}
											break;  // proposal found
										}
									}
								}
							}
						}

						if (!has_proposal) {
							int64_t amount = 0;
							if (!get_merkle_amount(rpcapi, admin_contract_addr, nonce,
							                       bridge_info.pegSteps, bridge_info.microSteps,
							                       hashes, amount)) {
								continue;  // until next attemp
							}
							if (amount == 0) {
								continue;
							}

							string scope  = "merkle";
							string action = "add";
							string phash;
							{
								CDataStream ss(SER_GETHASH, 0);
								ss << scope;
								ss << action;
								ss << bridge_info.name;
								ss << merkle_str;
								ss << amount;
								phash = Hash(ss.begin(), ss.end()).GetHex();
							}

							vector<string> ids_new;
							for (const string& id : ids) {
								if (id == "")
									continue;
								if (id == phash) {
									continue;
								}
								ids_new.push_back(id);
							}
							ids_new.push_back(phash);
							CWalletDB(pwallet->strWalletFile).WriteProposals(ids_new);

							int            t_until = GetTime() + 3600 * 24 * 3;
							vector<string> datas;
							datas.push_back(scope);
							datas.push_back(std::to_string(t_until));
							datas.push_back(action);
							datas.push_back(bridge_info.name);
							datas.push_back(merkle_str);
							datas.push_back(std::to_string(amount));
							CWalletDB(pwallet->strWalletFile).WriteProposal(phash, datas);
							LogPrintf(
							    "%s thread: bridge %s, todo nonce: %s, computed merkle: %s, added "
							    "proposal: %s\n",
							    "bitbay-bauto2", bridge_name, nonce, merkle_str, phash);
							continue;  // next nonce
						}

						if (!active_proposal) {
							vector<string> datas;
							CWalletDB(pwallet->strWalletFile).ReadProposal(proposal_id, datas);
							if (datas.size() > 1)
								continue;
							string scope = datas[0];
							if (scope == "merkle") {
								if (datas.size() == 6) {
									string action = datas[2];
									if (action == "add") {
										string proposal_brname = datas[3];
										if (proposal_brname != bridge_info.name)
											continue;
										string proposal_merkle = datas[4];
										if (proposal_merkle == merkle_str) {
											int t_until = GetTime() + 3600 * 24 * 3;
											datas[1]    = std::to_string(t_until);
											CWalletDB(pwallet->strWalletFile)
											    .WriteProposal(proposal_id, datas);
											LogPrintf(
											    "%s thread: bridge %s, todo nonce: %s, computed "
											    "merkle: %s, activated proposal: %s\n",
											    "bitbay-bauto2", bridge_name, nonce, merkle_str,
											    proposal_id);
										}
									}
								}
							}
						}
						// proposal is added and active now, all ok, continue to next attempt
						continue;
					} else {
						// merkle is mined, we can remove the proposal
						string         proposal_id;
						vector<string> ids;
						CWalletDB(pwallet->strWalletFile).ReadProposals(ids);
						for (const string& id : ids) {
							if (id == "")
								continue;
							vector<string> datas;
							CWalletDB(pwallet->strWalletFile).ReadProposal(id, datas);
							if (datas.size() > 1)
								continue;
							string scope = datas[0];
							if (scope == "merkle") {
								if (datas.size() == 6) {
									string action = datas[2];
									if (action == "add") {
										string proposal_brname = datas[3];
										if (proposal_brname != bridge_info.name)
											continue;
										string proposal_merkle = datas[4];
										if (proposal_merkle == merkle_str) {
											proposal_id = id;
											break;  // proposal is found
										}
									}
								}
							}
						}
						if (!proposal_id.empty()) {
							vector<string> ids;
							vector<string> ids_new;
							CWalletDB(pwallet->strWalletFile).ReadProposals(ids);
							for (const string& id : ids) {
								if (id == "")
									continue;
								if (id == proposal_id) {
									continue;
								}
								ids_new.push_back(id);
							}
							CWalletDB(pwallet->strWalletFile).WriteProposals(ids_new);
						}
					}

					// proposal is present and mined
					// mark the merkle (nonce) as completed in wallet.db
					CWalletDB(pwallet->strWalletFile)
					    .WriteCompletedMerkleInNonce(bridge_info.hash, nonce, true);
					LogPrintf("%s thread: bridge %s, nonce: %s, computed merkle: %s, completed\n",
					          "bitbay-bauto2", bridge_name, nonce, merkle_str);
				}

				bool txs_completed = false;
				CWalletDB(pwallet->strWalletFile)
				    .ReadCompletedMerkleInTxsNonce(bridge_info.hash, nonce, txs_completed);

				if (!txs_completed) {
					LogPrintf("%s thread: bridge %s, nonce: %s, todo txs\n", "bitbay-bauto2",
					          bridge_name, nonce);
					// merkle is mined, we can build and broadcast transactoins
					vector<string> hashes = call_listHashes(rpcapi, admin_contract_addr, nonce);
					if (hashes.empty())
						break;

					merkle::TreeT<32, sha256_keccak> tree;
					set<string>                      leaves_sorted;
					for (const string& hash : hashes) {
						leaves_sorted.insert(hash);
					}
					int              idx = 0;
					map<string, int> leaf_to_merkle_idx;
					for (const string& leaf : leaves_sorted) {
						tree.insert(leaf);
						leaf_to_merkle_idx[leaf] = idx;
						idx++;
					}
					auto     merkle_root  = tree.root();
					string   merkle_str   = merkle_root.to_string();
					uint32_t merkle_ntime = 0;
					for (const CMerkleInfo& minfo : merkles) {
						if (bridge_info.hash != minfo.brhash)
							continue;
						if (minfo.hash == merkle_str) {
							merkle_ntime = minfo.ntime;
							break;
						}
					}

					bool txs_mined = true;

					// read hashes detail
					for (int i = 0; i < int(hashes.size()); i++) {
						// get the "from" EVM address:
						string address_evm = call_addresses(rpcapi, admin_contract_addr, nonce, i);
						if (address_evm.empty())
							continue;
						// get the "to" BAY address:
						string address_bay =
						    call_recipient(rpcapi, admin_contract_addr, nonce, address_evm);
						if (address_bay.empty())
							continue;
						vector<int64_t> sections =
						    call_showReserve(rpcapi, admin_contract_addr, address_evm, nonce,
						                     bridge_info.pegSteps, bridge_info.microSteps);
						if (sections.empty())
							continue;
						// get section (peg) highkey[nonce][address_evm]
						int64_t section_peg =
						    call_highkey(rpcapi, admin_contract_addr, nonce, address_evm);
						if (section_peg < 0)
							continue;
						// recompute merkle leaf
						string out_leaf_hex;
						if (!ComputeMintMerkleLeaf(address_bay, sections, section_peg, nonce,
						                           "0x" + address_evm, out_leaf_hex)) {
							LogPrintf(
							    "%s thread: bridge %s, nonce: %s, todo txs, ComputeMintMerkleLeaf "
							    "failed: %s\n",
							    "bitbay-bauto2", bridge_name, nonce, address_evm);
							continue;
						}
						if (hashes[i] != out_leaf_hex) {
							LogPrintf(
							    "%s thread: bridge %s, nonce: %s, todo txs, ComputeMintMerkleLeaf "
							    "mimastch: %s vs %s computed\n",
							    "bitbay-bauto2", bridge_name, nonce, hashes[i], out_leaf_hex);
							continue;
						}

						// build proofs
						string         leaf = hashes[i];
						int            idx  = leaf_to_merkle_idx[leaf];
						vector<string> proofs;
						auto           leaf_path = tree.path(idx);
						for (size_t i = 0; i < leaf_path->size(); i++) {
							const auto& leaf_path_elm     = (*leaf_path)[i];
							string      leaf_path_elm_txt = leaf_path_elm.to_string();
							proofs.push_back(leaf_path_elm_txt);
						}

						// build tx
						{
							CTransaction tx;
							tx.nTime = merkle_ntime + 1;
							// coinmint inputs
							tx.vin.resize(1);
							tx.vin[0].prevout.SetNull();
							tx.vin[0].prevout.n = 0;
							// inputs from merkle
							tx.vin.resize(2);
							tx.vin[1].prevout.hash.SetHex(leaf);
							tx.vin[1].prevout.n = 0;  // 0
							int64_t nValue      = 0;
							// sigScript
							CScript proofSig;
							{
								// to address (BAY)
								CBitcoinAddress caddr(address_bay);
								proofSig << caddr.GetData();
								// fractions eth
								proofSig << int64_t(sections.size());
								for (int64_t f : sections) {
									proofSig << f;
								}
								for (size_t i = 0; i < size_t(bridge_info.pegSteps); i++) {
									if (i != size_t(section_peg)) {
										nValue += sections[i];
									} else {
										for (size_t j = 0; j < size_t(bridge_info.microSteps);
										     j++) {
											nValue += sections[bridge_info.pegSteps + j];
										}
									}
								}
								proofSig << int64_t(section_peg);
								proofSig << int64_t(nonce);
								std::vector<unsigned char> vchSender = ParseHex(address_evm);
								proofSig << vchSender;  // sender
								std::vector<unsigned char> vchLeaf = ParseHex(leaf);
								proofSig << vchLeaf;  // leaf
								proofSig << int64_t(proofs.size());
								for (const string& proof : proofs) {
									std::vector<unsigned char> vchProof = ParseHex(proof);
									proofSig << vchProof;
								}
							}
							tx.vin[1].scriptSig = proofSig;
							LogPrintf(
							    "%s thread: bridge %s, nonce: %s, todo txs, built "
							    "proofSig/scriptSig: %s\n",
							    "bitbay-bauto2", bridge_name, nonce, proofSig.ToString());

							{  // dest
								CScript scriptPubKey;
								scriptPubKey.SetDestination(CBitcoinAddress(address_bay).Get());
								tx.vout.push_back(CTxOut(nValue - MINT_TX_FEE, scriptPubKey));
							}

							uint256 hashTx = tx.GetHash();

							// check if tx is in chain already
							{
								LOCK(cs_main);
								CTxDB    txdb("r");
								CTxIndex txindex;
								if (!txdb.ReadTxIndex(hashTx, txindex)) {
									txs_mined = false;
								} else {
									int depth = txindex.GetDepthInMainChain();
									if (depth < nRecommendedConfirmations) {
										txs_mined = false;
									}
								}
							}
							CMerkleTx mtx(tx);
							mtx.AcceptToMemoryPool(false);
						}
					}
					// if all then mark nonce as completed in walletdb
					if (txs_mined) {
						CWalletDB(pwallet->strWalletFile)
						    .WriteCompletedMerkleInTxsNonce(bridge_info.hash, nonce, true);
						LogPrintf(
						    "%s thread: bridge %s, nonce: %s, computed merkle: %s, completed\n",
						    "bitbay-bauto2", bridge_name, nonce, merkle_str);
					}
				}
			}
		}
	}
}
