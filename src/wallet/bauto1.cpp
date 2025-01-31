// Copyright (c) 2024 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "evmtx.h"
#include "main.h"
#include "pegdb-leveldb.h"
#include "rpcprotocol.h"
#include "wallet.h"

#include <ethc/abi.h>
#include <ethc/account.h>
#include <ethc/address.h>
#include <ethc/hex.h>
#include <ethc/keccak256.h>
#include <ethc/rlp.h>

#include <curl/curl.h>
#include <boost/algorithm/string.hpp>

using namespace std;
using namespace boost;
using namespace json_spirit;

using std::cout;
using std::endl;
using std::string;

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

static string sendrawtx_rpcapi(string api_uri, string txhex) {
	try {
		map<string, string> mapRequestHeaders;
		Array               params;
		params.push_back(txhex);
		string strRequest = JSONRPCRequest("eth_sendRawTransaction", params, 1);
		string strReply   = call_rpcapi_curl(api_uri, strRequest);
		if (strReply == "")
			return "";
		std::cout << "API sendrawtx_rpcapi response:" << strReply << std::endl;
		string             data;
		json_spirit::Value jval;
		json_spirit::read_string(strReply, jval);
		const json_spirit::Object& jobj = jval.get_obj();
		if (!jobj.empty()) {
			data = json_spirit::find_value(jobj, "result").get_str();
		}
		return data;
	} catch (std::exception& e) {
		LogPrintf("%s thread sendrawtx_rpcapi %s err: %s\n", "bitbay-bauto1", api_uri, e.what());
		return "";
	}
}

static string call_rpcapi(string api_uri, string contract, string callsel, string data) {
	string strReply;
	try {
		Array  params;
		Object arg1;
		arg1.push_back(Pair("from", Value()));
		arg1.push_back(Pair("to", contract));
		arg1.push_back(Pair("data", callsel + data));
		params.push_back(arg1);
		params.push_back("latest");
		string strRequest = JSONRPCRequest("eth_call", params, 1);
		strReply          = call_rpcapi_curl(api_uri, strRequest);
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
		LogPrintf("%s thread call_rpcapi %s %s callsel:%s data:%s strReply:%s err: %s\n", "bitbay-bauto1",
		          api_uri, contract, callsel, data, strReply, e.what());
		return "";
	}
}

static int64_t nonce_rpcapi(string api_uri, string address) {
	try {
		Array params;
		params.push_back(address);
		params.push_back("latest");
		string strRequest = JSONRPCRequest("eth_getTransactionCount", params, 1);
		string strReply   = call_rpcapi_curl(api_uri, strRequest);
		if (strReply == "")
			return -1;
		string             data;
		int64_t            nonce = -1;
		json_spirit::Value jval;
		json_spirit::read_string(strReply, jval);
		const json_spirit::Object& jobj = jval.get_obj();
		if (!jobj.empty()) {
			data = json_spirit::find_value(jobj, "result").get_str();
			uint256 d(data);
			nonce = int64_t(d.GetLow64());
		}
		return nonce;
	} catch (std::exception& e) {
		LogPrintf("%s thread nonce_rpcapi %s %s err: %s\n", "bitbay-bauto1", api_uri, address,
		          e.what());
		return -1;
	}
}

int64_t gasprice_rpcapi(string api_uri) {
	try {
		Array  params;
		string strRequest = JSONRPCRequest("eth_gasPrice", params, 1);
		string strReply   = call_rpcapi_curl(api_uri, strRequest);
		if (strReply == "")
			return -1;
		string             data;
		int64_t            nonce = -1;
		json_spirit::Value jval;
		json_spirit::read_string(strReply, jval);
		const json_spirit::Object& jobj = jval.get_obj();
		if (!jobj.empty()) {
			data = json_spirit::find_value(jobj, "result").get_str();
			uint256 d(data);
			nonce = int64_t(d.GetLow64());
		}
		return nonce;
	} catch (std::exception& e) {
		LogPrintf("%s thread gasprice_rpcapi %s err: %s\n", "bitbay-bauto1", api_uri, e.what());
		return -1;
	}
}

string call_curators(string api_uri, string contract, int idx) {
	string         skip;
	string         address_evm;
	vector<string> hashes;
	string         curatorsSig = "curators(uint256)";
	string         curatorsSel = "0xdff43434";
	uint256        idx256(idx);
	string         result_data = call_rpcapi(api_uri, contract, curatorsSel, idx256.GetHex());
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

string call_proxy(string api_uri, string contract) {
	string         skip;
	string         address_evm;
	vector<string> hashes;
	string         proxySig    = "proxy()";
	string         proxySel    = "0xec556889";
	string         result_data = call_rpcapi(api_uri, contract, proxySel, "");
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

int64_t call_getSupply(string api_uri, string contract) {
	int64_t        skip = -1;
	int64_t        supply;
	vector<string> hashes;
	string         getSupplySig = "getSupply()";
	string         getSupplySel = "0x6c9c2faf";
	string         result_data  = call_rpcapi(api_uri, contract, getSupplySel, "");
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
	supply = int64_t(d.GetLow64());
	eth_abi_free(&abi);
	// abi end
	return supply;
}

int64_t call_myvotetimes(string api_uri, string contract, string address_evm, int votetype) {
	int64_t skip = -1;
	int64_t vtime;
	string  myvotetimesSig = "myvotetimes(address,uint256)";
	string  myvotetimesSel = "0xad84f03c";
	uint256 votetype256(votetype);
	string  result_data =
	    call_rpcapi(api_uri, contract, myvotetimesSel,
	                "000000000000000000000000" + address_evm + votetype256.GetHex());
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
	vtime = int64_t(d.GetLow64());
	eth_abi_free(&abi);
	// abi end
	return vtime;
}

string call_Merkles(string api_uri, string contract, int merkle_idx) {
	string  skip;
	string  merkle;
	string  merklesSig = "Merkles(uint256)";
	string  merklesSel = "0xe775f6cc";
	uint256 merkleidx256(merkle_idx);
	string  result_data = call_rpcapi(api_uri, contract, merklesSel, merkleidx256.GetHex());
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
	merkle = d.GetHex();
	eth_abi_free(&abi);
	// abi end
	return merkle;
}

static bool sync_pegindex(string                     rpcapi,
                          string                     datacontract_addr,
                          const CBridgeInfo&         bridge,
                          const set<string>&         sMineCurators,
                          const map<string, CKeyID>& mMineEvmPrivkeys,
                          string                     contract,
                          CWallet*                   pwallet,
                          float                      max_priority_fee_per_gas_gwei,
                          float                      max_fee_per_gas_gwei) {
	int64_t supply_evm = call_getSupply(rpcapi, "0x" + datacontract_addr);
	if (supply_evm < 0) {
		return false;  // err
	}
	LogPrintf("%s thread: bridge %s, pegsync, datacontract:%s supply_evm:%d\n", "bitbay-bauto1",
	          bridge.name, datacontract_addr, supply_evm);
	int64_t supply_bay =
	    pindexBest->nPegSupplyIndex * bridge.pegSteps * bridge.microSteps / PEG_SIZE;
	LogPrintf("%s thread: bridge %s, pegsync, datacontract:%s supply_bay:%d\n", "bitbay-bauto1",
	          bridge.name, datacontract_addr, supply_bay);
	if (supply_bay == supply_evm) {
		LogPrintf("%s thread: bridge %s, pegsync, NO CHANGE SUPPLY: %d --> %d\n", "bitbay-bauto1",
		          bridge.name, supply_evm, supply_bay);
		return true;
	}
	int votetime_setSupply = 7;
	for (const string& curator_addr : sMineCurators) {
		int64_t votetimet = call_myvotetimes(rpcapi, contract, curator_addr, votetime_setSupply);
		if (votetimet < 0) {
			LogPrintf("%s thread: bridge %s, pegsync, call_myvotetimes err %d\n", "bitbay-bauto1",
			          bridge.name, votetimet);
			continue;  // err
		}
		LogPrintf("%s thread: bridge %s, pegsync, myvotetimes for: %s is %d\n", "bitbay-bauto1",
		          bridge.name, curator_addr, votetimet);
		int64_t curtimet = GetAdjustedTime();
		if (votetimet > curtimet) {
			LogPrintf(
			    "%s thread: bridge %s, pegsync, %s CANNOT VOTE FOR SUPPLY due to votetime %d vs "
			    "%d\n",
			    "bitbay-bauto1", bridge.name, curator_addr, votetimet, curtimet);
			continue;
		}
		LogPrintf("%s thread: bridge %s, pegsync, %s CAN VOTE FOR SUPPLY\n", "bitbay-bauto1",
		          bridge.name, curator_addr);
		int64_t nonce_acc = nonce_rpcapi(rpcapi, "0x" + curator_addr);
		if (nonce_acc < 0) {
			LogPrintf("%s thread: bridge %s, pegsync, nonce_rpcapi err %d\n", "bitbay-bauto1",
			          bridge.name, nonce_acc);
			continue;  // err
		}
		LogPrintf("%s thread: bridge %s, pegsync, %s TX NONCE %d\n", "bitbay-bauto1", bridge.name,
		          curator_addr, nonce_acc);
		int64_t gas_price = gasprice_rpcapi(rpcapi);
		if (gas_price < 0) {
			LogPrintf("%s thread: bridge %s, pegsync, gasprice_rpcapi err %d\n", "bitbay-bauto1",
			          bridge.name, gas_price);
			continue;  // err
		}
		double gas_price_f = float(gas_price) / 1.e9;
		LogPrintf("%s thread: bridge %s, pegsync, GAS PRICE GWEI %f\n", "bitbay-bauto1",
		          bridge.name, gas_price_f);
		if (gas_price_f > max_fee_per_gas_gwei) {
			LogPrintf("%s thread: bridge %s, pegsync, GAS PRICE GWEI TOO HIGH %f vs max %f\n",
			          "bitbay-bauto1", bridge.name, gas_price_f, max_fee_per_gas_gwei);
			continue;  // ok, skip
		}
		CKeyID keyID = mMineEvmPrivkeys.at("0x" + curator_addr);
		CKey   vchSecret;
		if (!pwallet->GetKey(keyID, vchSecret)) {
			LogPrintf("%s thread: bridge %s, pegsync, %s NO KEY\n", "bitbay-bauto1", bridge.name,
			          curator_addr);
			continue;  // no key
		}
		{  // double-recheck
			eth_account acc;
			if (eth_account_from_privkey(&acc, vchSecret.begin()) > 0) {
				int  len_cstr = 0;
				char addr_cstr[41];
				len_cstr = eth_account_address_get(addr_cstr, &acc);
				if (len_cstr > 0) {
					addr_cstr[len_cstr] = 0;
					string address_evm  = "0x" + string(addr_cstr);
					if (address_evm != "0x" + curator_addr) {
						LogPrintf("%s thread: bridge %s, pegsync, KEY RECHECK FAILED %s vs %s\n",
						          "bitbay-bauto1", bridge.name, "0x" + curator_addr, address_evm);
						continue;
					}
				}
			}
		}
		int      gaslimit     = 500000;
		string   setSupplySig = "setSupply(uint256,address[])";
		char*    setSupplyFun = (char*)(setSupplySig.c_str());
		uint64_t sbv          = uint64_t(supply_bay);
		// abi start
		struct eth_abi abi;
		eth_abi_init(&abi, ETH_ABI_ENCODE);
		eth_abi_call(&abi, &setSupplyFun, NULL);
		eth_abi_uint64(&abi, &sbv);
		eth_abi_array(&abi, NULL);  // [
		eth_abi_array_end(&abi);    // ]
		eth_abi_call_end(&abi);
		char*  abi_hex_cstr = NULL;
		size_t abi_hexlen;
		eth_abi_to_hex(&abi, &abi_hex_cstr, &abi_hexlen);
		string abi_hex = string(abi_hex_cstr);
		string txhash_hex;
		string signed_tx =
		    MakeTxEvm(bridge.chainId, nonce_acc, max_priority_fee_per_gas_gwei,
		              max_fee_per_gas_gwei, gaslimit, contract, abi_hex, vchSecret, txhash_hex);
		LogPrintf("%s thread: bridge %s, pegsync, EIP-1559 Signed transaction: %s\n",
		          "bitbay-bauto1", bridge.name, signed_tx);
		LogPrintf("%s thread: bridge %s, pegsync, EIP-1559 Signed transaction hash: %s\n",
		          "bitbay-bauto1", bridge.name, txhash_hex);
		string res_rawtx = sendrawtx_rpcapi(rpcapi, signed_tx);
		LogPrintf("%s thread: bridge %s, pegsync, EIP-1559 Signed transaction SENT: %s\n",
		          "bitbay-bauto1", bridge.name, res_rawtx);
	}
	return true;
}

static bool sync_merkles(string                     rpcapi,
                         string                     datacontract_addr,
                         const CBridgeInfo&         bridge,
                         const set<string>&         sMineCurators,
                         const map<string, CKeyID>& mMineEvmPrivkeys,
                         string                     contract,
                         CWallet*                   pwallet,
                         float                      max_priority_fee_per_gas_gwei,
                         float                      max_fee_per_gas_gwei) {
	// last 100 cycles merkles to be processed out of bridges cycle
	CPegDB           pegdb;
	set<string>      sMerklesOut;
	map<string, int> mMerkleOutSections;
	map<string, int> mMerkleIndexes;
	int              bridge_cycles = 100;
	CBlockIndex*     bridge_index  = pindexBest->BridgeCycleBlock();
	for (int i = 0; i < bridge_cycles; i++) {
		if (!bridge_index)
			break;
		uint256     bhash_bridge_cycle = bridge_index->GetBlockHash();
		set<string> bridge_hashes;
		pegdb.ReadBridgeCycleBridgeHashes(bhash_bridge_cycle, bridge_hashes);
		for (const string& brhash : bridge_hashes) {
			if (brhash != bridge.hash) {
				continue;  // skip other bridges
			}
			string merkle_data;
			pegdb.ReadBridgeCycleMerkle(bhash_bridge_cycle, brhash, merkle_data);
			string         merkle, section;
			vector<string> args;
			boost::split(args, merkle_data, boost::is_any_of(":"));
			if (args.size() == 2) {
				merkle  = args[0];
				section = args[1];
				sMerklesOut.insert(merkle);
				mMerkleOutSections[merkle] = std::atoi(section.c_str());
				// std::cout << "bitbay-bauto1: bridge:" << rpcapi << " merkleout:" << merkle
				//           << std::endl;
			}
		}
		bridge_index = bridge_index->PrevBridgeCycleBlock();
	}
	set<string> sMerklesOutTodo = sMerklesOut;
	for (int merkle_idx = 0;; merkle_idx++) {
		string merklehash;
		CWalletDB(pwallet->strWalletFile)
		    .ReadCompletedMerkleOutIdx(bridge.hash, merkle_idx, merklehash);
		if (!merklehash.empty()) {
			if (sMerklesOut.count(merklehash)) {
				LogPrintf(
				    "%s thread: bridge %s, merkles, merkle_idx: %d merklehash: %s ALREADY "
				    "REGISTRED (cached)\n",
				    "bitbay-bauto1", bridge.name, merkle_idx, merklehash);
				sMerklesOutTodo.erase(merklehash);
			}
			continue;
		}
		merklehash = call_Merkles(rpcapi, contract, merkle_idx);
		if (merklehash.empty())
			break;  // last one, break
		mMerkleIndexes[merklehash] = merkle_idx;
		// the merklehash is already registred and online, skip it from processing next time
		CWalletDB(pwallet->strWalletFile)
		    .WriteCompletedMerkleOutIdx(bridge.hash, merkle_idx, merklehash);
		if (sMerklesOut.count(merklehash)) {
			LogPrintf(
			    "%s thread: bridge %s, merkles, merkle_idx: %d merklehash: %s ALREADY REGISTRED\n",
			    "bitbay-bauto1", bridge.name, merkle_idx, merklehash);
			sMerklesOutTodo.erase(merklehash);
		}
	}
	for (const string& merklehash : sMerklesOutTodo) {
		LogPrintf("%s thread: bridge %s, merkles, processing merklehash: %s\n", "bitbay-bauto1",
		          bridge.name, merklehash);
		int     section = mMerkleOutSections[merklehash];
		vchtype merklehash_b32;
		merklehash_b32.resize(32);
		uint8_t* merklehash_b32_data = merklehash_b32.data();
		int      len_merklehash_b32 =
		    eth_hex_to_bytes(&merklehash_b32_data, merklehash.c_str(), merklehash.length());
		if (len_merklehash_b32 != 32) {
			continue;  // wrong hex
		}
		int votetime_addMerkle = 9;
		for (const string& curator_addr : sMineCurators) {
			int64_t votetimet =
			    call_myvotetimes(rpcapi, contract, curator_addr, votetime_addMerkle);
			if (votetimet < 0) {
				LogPrintf("%s thread: bridge %s, merkles, call_myvotetimes err %d\n",
				          "bitbay-bauto1", bridge.name, votetimet);
				continue;  // err
			}
			LogPrintf("%s thread: bridge %s, merkles, myvotetimes for: %s is %d\n", "bitbay-bauto1",
			          bridge.name, curator_addr, votetimet);
			int64_t curtimet = GetAdjustedTime();
			if (votetimet > curtimet) {
				LogPrintf(
				    "%s thread: bridge %s, merkles, %s CANNOT VOTE FOR MERKLE due to votetime %d "
				    "vs "
				    "%d\n",
				    "bitbay-bauto1", bridge.name, curator_addr, votetimet, curtimet);
				continue;
			}
			LogPrintf("%s thread: bridge %s, merkles, %s CAN VOTE FOR MERKLE\n", "bitbay-bauto1",
			          bridge.name, curator_addr);
			int64_t nonce_acc = nonce_rpcapi(rpcapi, "0x" + curator_addr);
			if (nonce_acc < 0) {
				LogPrintf("%s thread: bridge %s, merkles, nonce_rpcapi err %d\n", "bitbay-bauto1",
				          bridge.name, nonce_acc);
				continue;  // err
			}
			LogPrintf("%s thread: bridge %s, merkles, %s TX NONCE %d\n", "bitbay-bauto1",
			          bridge.name, curator_addr, nonce_acc);
			int64_t gas_price = gasprice_rpcapi(rpcapi);
			if (gas_price < 0) {
				LogPrintf("%s thread: bridge %s, merkles, gasprice_rpcapi err %d\n",
				          "bitbay-bauto1", bridge.name, gas_price);
				continue;  // err
			}
			double gas_price_f = float(gas_price) / 1.e9;
			LogPrintf("%s thread: bridge %s, merkles, GAS PRICE GWEI %f\n", "bitbay-bauto1",
			          bridge.name, gas_price_f);
			if (gas_price_f > max_fee_per_gas_gwei) {
				LogPrintf("%s thread: bridge %s, merkles, GAS PRICE GWEI TOO HIGH %f vs max %f\n",
				          "bitbay-bauto1", bridge.name, gas_price_f, max_fee_per_gas_gwei);
				continue;  // ok, skip
			}
			CKeyID keyID = mMineEvmPrivkeys.at("0x" + curator_addr);
			CKey   vchSecret;
			if (!pwallet->GetKey(keyID, vchSecret)) {
				LogPrintf("%s thread: bridge %s, merkles, %s NO KEY\n", "bitbay-bauto1",
				          bridge.name, curator_addr);
				continue;  // no key
			}
			{  // double-recheck
				eth_account acc;
				if (eth_account_from_privkey(&acc, vchSecret.begin()) > 0) {
					int  len_cstr = 0;
					char addr_cstr[41];
					len_cstr = eth_account_address_get(addr_cstr, &acc);
					if (len_cstr > 0) {
						addr_cstr[len_cstr] = 0;
						string address_evm  = "0x" + string(addr_cstr);
						if (address_evm != "0x" + curator_addr) {
							LogPrintf(
							    "%s thread: bridge %s, merkles, KEY RECHECK FAILED %s vs %s\n",
							    "bitbay-bauto1", bridge.name, "0x" + curator_addr, address_evm);
							continue;
						}
					}
				}
			}
			int      gaslimit     = 500000;
			string   addMerkleSig = "addMerkle(bytes32,uint256)";
			char*    addMerkleFun = (char*)(addMerkleSig.c_str());
			uint64_t sv           = uint64_t(section);
			// abi start
			struct eth_abi abi;
			eth_abi_init(&abi, ETH_ABI_ENCODE);
			eth_abi_call(&abi, &addMerkleFun, NULL);
			eth_abi_bytes32(&abi, merklehash_b32_data);
			eth_abi_uint64(&abi, &sv);
			eth_abi_call_end(&abi);
			char*  abi_hex_cstr = NULL;
			size_t abi_hexlen;
			eth_abi_to_hex(&abi, &abi_hex_cstr, &abi_hexlen);
			string abi_hex = string(abi_hex_cstr);
			string txhash_hex;
			string signed_tx =
			    MakeTxEvm(bridge.chainId, nonce_acc, max_priority_fee_per_gas_gwei,
			              max_fee_per_gas_gwei, gaslimit, contract, abi_hex, vchSecret, txhash_hex);
			LogPrintf("%s thread: bridge %s, merkles, EIP-1559 Signed transaction: %s\n",
			          "bitbay-bauto1", bridge.name, signed_tx);
			LogPrintf("%s thread: bridge %s, merkles, EIP-1559 Signed transaction hash: %s\n",
			          "bitbay-bauto1", bridge.name, txhash_hex);
			string res_rawtx = sendrawtx_rpcapi(rpcapi, signed_tx);
			LogPrintf("%s thread: bridge %s, merkles, EIP-1559 Signed transaction SENT: %s\n",
			          "bitbay-bauto1", bridge.name, res_rawtx);
		}
		// do only once due to myvotetime
		break;
	}
	return true;
}

void ThreadBrigeAuto1(CWallet* pwallet) {
	SetThreadPriority(THREAD_PRIORITY_LOWEST);

	// Make this thread recognisable
	RenameThread("bitbay-bauto1");
	LogPrintf("%s thread start\n", "bitbay-bauto1");

	MilliSleep(5000);
	bool first = true;

	while (true) {
		if (!first)
			MilliSleep(600000);
		else
			MilliSleep(60000);
		first = false;
		if (GetAdjustedTime() - pindexBest->nTime > 60 * 60) {
			continue;  // wait a sync
		}
		CPegDB              pegdb;
		set<string>         sMineEvmAddresses;
		map<string, CKeyID> mMineEvmPrivkeys;
		{
			LOCK(pwallet->cs_wallet);
			for (const pair<CTxDestination, std::string> item : pwallet->mapAddressBook) {
				const CBitcoinAddress& address = item.first;
				bool                   fMine   = IsMine(*pwallet, address.Get());
				if (fMine) {
					CKeyID keyID;
					if (address.GetKeyID(keyID)) {
						CKey vchSecret;
						if (pwallet->GetKey(keyID, vchSecret)) {
							CPubKey pubkey = vchSecret.GetPubKey();
							if (pubkey.IsValid()) {
								eth_account acc;
								if (eth_account_from_privkey(&acc, vchSecret.begin()) > 0) {
									int  len_cstr = 0;
									char addr_cstr[41];
									len_cstr = eth_account_address_get(addr_cstr, &acc);
									if (len_cstr > 0) {
										addr_cstr[len_cstr] = 0;
										string address_evm  = "0x" + string(addr_cstr);
										sMineEvmAddresses.insert(address_evm);
										mMineEvmPrivkeys.insert({address_evm, keyID});
									}
								}
							}
						}
					}
				}
			}
		}
		map<string, CBridgeInfo> bridges;
		bool                     ok = pindexBest->ReadBridges(pegdb, bridges);
		if (!ok)
			continue;
		for (const auto& it : bridges) {
			string      bridge_name = it.first;
			CBridgeInfo bridge_info = it.second;
			if (bridge_info.urls.empty()) {
				continue;
			}
			bool   is_automated                  = false;
			double max_priority_fee_per_gas_gwei = 0.;
			double max_fee_per_gas_gwei          = 0.;
			CWalletDB(pwallet->strWalletFile)
			    .ReadBridgeIsAutomated(bridge_info.hash, is_automated,
			                           max_priority_fee_per_gas_gwei, max_fee_per_gas_gwei);
			if (!is_automated) {
				continue;
			}
			if (max_fee_per_gas_gwei == 0.) {
				continue;
			}
			LogPrintf("%s thread: bridge %s, is automated\n", "bitbay-bauto1", bridge_name);
			string      rpcapi        = *(bridge_info.urls.begin());
			string      contract      = bridge_info.contract;
			bool        mine_curators = false;
			set<string> sMineCurators;
			for (int curator_idx = 0;; curator_idx++) {
				string curator_addr = call_curators(rpcapi, contract, curator_idx);
				if (curator_addr.empty()) {
					break;
				}
				if (sMineEvmAddresses.count("0x" + curator_addr)) {
					LogPrintf("%s thread: bridge %s, has curator: %s\n", "bitbay-bauto1",
					          bridge_name, "0x" + curator_addr);
					mine_curators = true;
					sMineCurators.insert(curator_addr);
				}
			}
			if (!mine_curators) {
				LogPrintf("%s thread: bridge %s, no curator\n", "bitbay-bauto1", bridge_name);
				continue;  // to next bridge
			}
			// to know bitbaydata contract
			string datacontract_addr = call_proxy(rpcapi, contract);
			if (datacontract_addr.empty()) {
				LogPrintf("%s thread: bridge %s, no datacontract\n", "bitbay-bauto1", bridge_name);
				continue;  // not known
			}
			LogPrintf("%s thread: bridge %s, datacontract: %s\n", "bitbay-bauto1", bridge_name,
			          datacontract_addr);
			sync_pegindex(rpcapi, datacontract_addr, bridge_info, sMineCurators, mMineEvmPrivkeys,
			              contract, pwallet, max_priority_fee_per_gas_gwei, max_fee_per_gas_gwei);
			sync_merkles(rpcapi, datacontract_addr, bridge_info, sMineCurators, mMineEvmPrivkeys,
			             contract, pwallet, max_priority_fee_per_gas_gwei, max_fee_per_gas_gwei);
		}
	}
}
