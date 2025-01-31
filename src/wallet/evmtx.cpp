// Copyright (c) 2024 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evmtx.h"
#include <istream>
#include <string>
#include "base58.h"

#include <ethc/abi.h>
#include <ethc/account.h>
#include <ethc/address.h>
#include <ethc/hex.h>
#include <ethc/keccak256.h>
#include <ethc/rlp.h>

using namespace std;

static string int2hex(int64_t v) {
	string            vhex;
	std::stringstream stream;
	stream << std::hex << v;
	vhex = "0x" + string(stream.str());
	return vhex;
}

static vchtype make_evmtx_hash(int    chainid,
							   int    nonce,
							   float  max_priority_fee_per_gas_gwei,
							   float  max_fee_per_gas_gwei,
							   int    gaslimit,
							   string addr_to,
							   string input_data_hex) {
	struct eth_rlp rlp0;
	size_t         rlp0len;
	uint8_t        keccak[32], *rlp0bytes;
	// args as hex
	string chainid_hex                   = int2hex(chainid);
	char*  chainid_hexp                  = (char*)(chainid_hex.c_str());
	string nonce_hex                     = int2hex(nonce);
	char*  nonce_hexp                    = (char*)(nonce_hex.c_str());
	string max_priority_fee_per_gas_hex  = int2hex(int64_t(max_priority_fee_per_gas_gwei * 1.e9));
	char*  max_priority_fee_per_gas_hexp = (char*)(max_priority_fee_per_gas_hex.c_str());
	string max_fee_per_gas_hex           = int2hex(int64_t(max_fee_per_gas_gwei * 1.e9));
	char*  max_fee_per_gas_hexp          = (char*)(max_fee_per_gas_hex.c_str());
	string gaslimit_hex                  = int2hex(gaslimit);
	char*  gaslimit_hexp                 = (char*)(gaslimit_hex.c_str());
	char*  addr_to_p                     = (char*)(addr_to.c_str());
	string value_hex                     = "0x00";
	char*  value_hexp                    = (char*)(value_hex.c_str());
	char*  input_data_hexp               = (char*)(input_data_hex.c_str());
	// EIP-1559 unsigned tx
	eth_rlp_init(&rlp0, ETH_RLP_ENCODE);
	eth_rlp_array(&rlp0);
	eth_rlp_hex(&rlp0, &chainid_hexp, NULL);
	eth_rlp_hex(&rlp0, &nonce_hexp, NULL);
	eth_rlp_hex(&rlp0, &max_priority_fee_per_gas_hexp, NULL);
	eth_rlp_hex(&rlp0, &max_fee_per_gas_hexp, NULL);
	eth_rlp_hex(&rlp0, &gaslimit_hexp, NULL);
	eth_rlp_address(&rlp0, &addr_to_p);
	eth_rlp_hex(&rlp0, &value_hexp, NULL);
	eth_rlp_hex(&rlp0, &input_data_hexp, NULL);
	eth_rlp_array(&rlp0);  // empty for access_list
	eth_rlp_array_end(&rlp0);
	eth_rlp_array_end(&rlp0);
	eth_rlp_to_bytes(&rlp0bytes, &rlp0len, &rlp0);
	eth_rlp_free(&rlp0);
	// put EIP-1559 prefix
	uint8_t  tx_type_prefix            = 0x02;
	size_t   unsigned_tx_with_type_len = sizeof(tx_type_prefix) + rlp0len;
	uint8_t* unsigned_tx_with_type     = (uint8_t*)malloc(unsigned_tx_with_type_len);
	memcpy(unsigned_tx_with_type, &tx_type_prefix, sizeof(tx_type_prefix));
	memcpy(unsigned_tx_with_type + sizeof(tx_type_prefix), rlp0bytes, rlp0len);
	// compute the keccak hash of the encoded rlp elements
	eth_keccak256(keccak, unsigned_tx_with_type, unsigned_tx_with_type_len);
	free(rlp0bytes);
	free(unsigned_tx_with_type);
	vchtype hash(keccak, keccak + 32);
	return hash;
}

string MakeTxEvm(int         chainid,
				 int         nonce,
				 float       max_priority_fee_per_gas_gwei,
				 float       max_fee_per_gas_gwei,
				 int         gaslimit,
				 string      addr_to,
				 string      input_data_hex,
				 const CKey& vchSecret,
				 string&     txhash_hex) {
	// args as hex
	string chainid_hex                   = int2hex(chainid);
	char*  chainid_hexp                  = (char*)(chainid_hex.c_str());
	string nonce_hex                     = int2hex(nonce);
	char*  nonce_hexp                    = (char*)(nonce_hex.c_str());
	string max_priority_fee_per_gas_hex  = int2hex(int64_t(max_priority_fee_per_gas_gwei * 1.e9));
	char*  max_priority_fee_per_gas_hexp = (char*)(max_priority_fee_per_gas_hex.c_str());
	string max_fee_per_gas_hex           = int2hex(int64_t(max_fee_per_gas_gwei * 1.e9));
	char*  max_fee_per_gas_hexp          = (char*)(max_fee_per_gas_hex.c_str());
	string gaslimit_hex                  = int2hex(gaslimit);
	char*  gaslimit_hexp                 = (char*)(gaslimit_hex.c_str());
	char*  addr_to_p                     = (char*)(addr_to.c_str());
	string value_hex                     = "0x00";
	char*  value_hexp                    = (char*)(value_hex.c_str());
	char*  input_data_hexp               = (char*)(input_data_hex.c_str());
	// sign the transaction
	vchtype txhash = make_evmtx_hash(chainid, nonce, max_priority_fee_per_gas_gwei,
									 max_fee_per_gas_gwei, gaslimit, addr_to, input_data_hex);
	char *txhash_cstr;
	eth_hex_from_bytes(&txhash_cstr, txhash.data(), 32);
	txhash_hex = string(txhash_cstr);
	struct eth_ecdsa_signature sign;
	eth_ecdsa_sign(&sign, vchSecret.begin(), txhash.data());
	// calculate v
	size_t   siglen = 32;
	uint8_t  v      = sign.recid;
	uint8_t* r      = sign.r;
	uint8_t* s      = sign.s;
	// encode tx
	struct eth_rlp rlp1;
	eth_rlp_init(&rlp1, ETH_RLP_ENCODE);
	eth_rlp_array(&rlp1);
	eth_rlp_hex(&rlp1, &chainid_hexp, NULL);
	eth_rlp_hex(&rlp1, &nonce_hexp, NULL);
	eth_rlp_hex(&rlp1, &max_priority_fee_per_gas_hexp, NULL);
	eth_rlp_hex(&rlp1, &max_fee_per_gas_hexp, NULL);
	eth_rlp_hex(&rlp1, &gaslimit_hexp, NULL);
	eth_rlp_address(&rlp1, &addr_to_p);
	eth_rlp_hex(&rlp1, &value_hexp, NULL);
	eth_rlp_hex(&rlp1, &input_data_hexp, NULL);
	eth_rlp_array(&rlp1);
	eth_rlp_array_end(&rlp1);
	eth_rlp_uint8(&rlp1, &v);
	eth_rlp_bytes(&rlp1, &r, &siglen);
	eth_rlp_bytes(&rlp1, &s, &siglen);
	eth_rlp_array_end(&rlp1);
	{
		size_t         rlp1len;
		uint8_t        keccak[32], *rlp1bytes;
		eth_rlp_to_bytes(&rlp1bytes, &rlp1len, &rlp1);
		// put EIP-1559 prefix
		uint8_t  tx_type_prefix            = 0x02;
		size_t   signed_tx_with_type_len = sizeof(tx_type_prefix) + rlp1len;
		uint8_t* signed_tx_with_type     = (uint8_t*)malloc(signed_tx_with_type_len);
		memcpy(signed_tx_with_type, &tx_type_prefix, sizeof(tx_type_prefix));
		memcpy(signed_tx_with_type + sizeof(tx_type_prefix), rlp1bytes, rlp1len);
		// compute the keccak hash of the encoded rlp elements
		eth_keccak256(keccak, signed_tx_with_type, signed_tx_with_type_len);
		free(rlp1bytes);
		free(signed_tx_with_type);
		vchtype txhash(keccak, keccak + 32);
		char *txhash_cstr;
		eth_hex_from_bytes(&txhash_cstr, txhash.data(), 32);
		txhash_hex = string(txhash_cstr);
	}
	char* txn;
	eth_rlp_to_hex(&txn, &rlp1);
	eth_rlp_free(&rlp1);
	string tx_type_prefix_str  = "0x02";
	char*  tx_type_prefix_strp = (char*)(tx_type_prefix_str.c_str());
	int    tx_size             = strlen(txn);
	int    tx_prefix_size      = strlen(tx_type_prefix_strp);
	char   signed_tx[tx_size + tx_prefix_size];
	sprintf(signed_tx, "%s%s", tx_type_prefix_strp, txn);
	string signed_tx_str(signed_tx, tx_size + tx_prefix_size);
	return signed_tx_str;
}
