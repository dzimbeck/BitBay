// Copyright (c) 2009-2012 Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <fstream>
#include <iostream>

#include "base58.h"
#include "init.h"  // for pwalletMain
#include "rpcserver.h"
#include "ui_interface.h"

#include <ethc/abi.h>
#include <ethc/account.h>
#include <ethc/address.h>
#include <ethc/hex.h>
#include <ethc/keccak256.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/variant/get.hpp>

using namespace json_spirit;
using namespace std;

void EnsureWalletIsUnlocked();

namespace bt = boost::posix_time;

// Extended DecodeDumpTime implementation, see this page for details:
// http://stackoverflow.com/questions/3786201/parsing-of-date-time-from-string-boost
const std::locale formats[] = {
    std::locale(std::locale::classic(), new bt::time_input_facet("%Y-%m-%dT%H:%M:%SZ")),
    std::locale(std::locale::classic(), new bt::time_input_facet("%Y-%m-%d %H:%M:%S")),
    std::locale(std::locale::classic(), new bt::time_input_facet("%Y/%m/%d %H:%M:%S")),
    std::locale(std::locale::classic(), new bt::time_input_facet("%d.%m.%Y %H:%M:%S")),
    std::locale(std::locale::classic(), new bt::time_input_facet("%Y-%m-%d"))};

const size_t formats_n = sizeof(formats) / sizeof(formats[0]);

std::time_t pt_to_time_t(const bt::ptime& pt) {
	bt::ptime         timet_start(boost::gregorian::date(1970, 1, 1));
	bt::time_duration diff = pt - timet_start;
	return diff.ticks() / bt::time_duration::rep_type::ticks_per_second;
}

int64_t DecodeDumpTime(const std::string& s) {
	bt::ptime pt;

	for (size_t i = 0; i < formats_n; ++i) {
		std::istringstream is(s);
		is.imbue(formats[i]);
		is >> pt;
		if (pt != bt::ptime())
			break;
	}

	return pt_to_time_t(pt);
}

std::string static EncodeDumpTime(int64_t nTime) {
	return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

std::string static EncodeDumpString(const std::string& str) {
	std::stringstream ret;
	for (unsigned char c : str) {
		if (c <= 32 || c >= 128 || c == '%') {
			ret << '%' << HexStr(&c, &c + 1);
		} else {
			ret << c;
		}
	}
	return ret.str();
}

std::string DecodeDumpString(const std::string& str) {
	std::stringstream ret;
	for (uint32_t pos = 0; pos < str.length(); pos++) {
		unsigned char c = str[pos];
		if (c == '%' && pos + 2 < str.length()) {
			c = (((str[pos + 1] >> 6) * 9 + ((str[pos + 1] - '0') & 15)) << 4) |
			    ((str[pos + 2] >> 6) * 9 + ((str[pos + 2] - '0') & 15));
			pos += 2;
		}
		ret << c;
	}
	return ret.str();
}

class CTxDump {
public:
	CBlockIndex* pindex;
	int64_t      nValue;
	bool         fSpent;
	CWalletTx*   ptx;
	int          nOut;
	CTxDump(CWalletTx* ptx = NULL, int nOut = -1) {
		pindex     = NULL;
		nValue     = 0;
		fSpent     = false;
		this->ptx  = ptx;
		this->nOut = nOut;
	}
};

Value importprivkey(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 1 || params.size() > 3)
		throw runtime_error(
		    "importprivkey <bitbayprivkey> [label] [rescan=true]\n"
			"importprivkey <evmprivkey> [label] [rescan=true]\n"
			"Adds a private key (as returned by dumpprivkey) to your wallet.");

	string strPrivkey = params[0].get_str();
	string strLabel   = "";
	if (params.size() > 1)
		strLabel = params[1].get_str();

	// Whether to perform rescan after import
	bool fRescan = true;
	if (params.size() > 2)
		fRescan = params[2].get_bool();

	if (boost::starts_with(strPrivkey, "0x")) {  // evm privkey
		char* privkeyhex_cstr = new char[strPrivkey.length() + 1];
		strcpy(privkeyhex_cstr, strPrivkey.c_str());
		uint8_t* privkey_bin_data;
		int      len_privkey_bin =
			eth_hex_to_bytes(&privkey_bin_data, privkeyhex_cstr, strPrivkey.length());
		if (len_privkey_bin != 32) {
			delete[] privkeyhex_cstr;
			if (len_privkey_bin > 0)
				free(privkey_bin_data);
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot decode evm private key");
		}
		// have key in privkey_bin_data
		CKey key;

		key.Set(privkey_bin_data, privkey_bin_data + 32, true);

		/*

		ef

		4f4e0f
		8ebde9dd
		6269850b
		87345470

		416d6b69
		50be3823
		1fb94034
		114cab17

		a8

		01 // compressed

		23ba7211

		// evm

		4f4e0f8e
		bde9dd62
		69850b87
		34547041

		6d6b6950
		be38231f
		b9403411
		4cab17a8


16:33:43
￼
dumpprivkey myujymoDC8MmQNhXWXjx9aTLQXarN3WowK


16:33:43
￼
{
"address" : "myujymoDC8MmQNhXWXjx9aTLQXarN3WowK",
"pubkey" : "02395fd822179f5125c62ffbaa18b5a38880ad705f5d378e0e2c10d8e564f4bf1d",
"secret_b58" : "cQErqTyysamrtLLVZdThRQPyXmvAdQRTQgu11784wDPLHz3xj1CL",
"evm" : {
"address" : "4825181eee798f12d5ef15957b8074c654d73d5a",
"privatekey" : "4f4e0f8ebde9dd6269850b87345470416d6b6950be38231fb94034114cab17a8"
}
}


		*/

		CPubKey pubkey     = key.GetPubKey();
		CKeyID  vchAddress = pubkey.GetID();

		// cleanup
		delete[] privkeyhex_cstr;
		free(privkey_bin_data);

		{
			LOCK2(cs_main, pwalletMain->cs_wallet);

			pwalletMain->MarkDirty();
			pwalletMain->SetAddressBookName(vchAddress, strLabel);

			// Don't throw error in case a key is already there
			if (!pwalletMain->HaveKey(vchAddress)) {
				pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

				if (!pwalletMain->AddKeyPubKey(key, pubkey))
					throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

				// whenever a key is imported, we need to scan the whole chain
				pwalletMain->nTimeFirstKey = 1;  // 0 would be considered 'no value'

				if (fRescan) {
					pwalletMain->ScanForWalletTransactions(pindexGenesisBlock, true);
					pwalletMain->ReacceptWalletTransactions();
				}
			}
		}

		Object result;
		result.push_back(Pair("address", CBitcoinAddress(vchAddress).ToString()));
		Object result_evm;

		eth_account acc;
		if (eth_account_from_privkey(&acc, key.begin()) > 0) {
			int  len_cstr = 0;
			char addr_cstr[41];
			len_cstr = eth_account_address_get(addr_cstr, &acc);
			if (len_cstr > 0) {
				addr_cstr[len_cstr] = 0;
				result_evm.push_back(Pair("address", "0x" + string(addr_cstr)));
			}
		}

		result.push_back(Pair("evm", result_evm));
		return result;
	}

	string strSecret = strPrivkey;

	CBitcoinSecret vchSecret;
	bool           fGood = vchSecret.SetString(strSecret);

	if (!fGood)
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
	if (fWalletUnlockStakingOnly)
		throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for staking only.");

	CKey    key        = vchSecret.GetKey();
	CPubKey pubkey     = key.GetPubKey();
	CKeyID  vchAddress = pubkey.GetID();
	{
		LOCK2(cs_main, pwalletMain->cs_wallet);

		pwalletMain->MarkDirty();
		pwalletMain->SetAddressBookName(vchAddress, strLabel);

		// Don't throw error in case a key is already there
		if (!pwalletMain->HaveKey(vchAddress)) {
			pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

			if (!pwalletMain->AddKeyPubKey(key, pubkey))
				throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

			// whenever a key is imported, we need to scan the whole chain
			pwalletMain->nTimeFirstKey = 1;  // 0 would be considered 'no value'

			if (fRescan) {
				pwalletMain->ScanForWalletTransactions(pindexGenesisBlock, true);
				pwalletMain->ReacceptWalletTransactions();
			}
		}
	}

	Object result;
	result.push_back(Pair("address", CBitcoinAddress(vchAddress).ToString()));
	return result;
}

Value importaddress(const Array& params, bool fHelp) {
	if (fHelp || params.size() < 1 || params.size() > 3)
		throw runtime_error(
		    "importaddress <address> [label] [rescan=true]\n"
		    "Adds an address that can be watched as if it were in your wallet but cannot be used "
		    "to spend.");

	CBitcoinAddress address(params[0].get_str());
	if (!address.IsValid())
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Bitcoin address");
	CTxDestination dest;
	dest = address.Get();

	string strLabel = "";
	if (params.size() > 1)
		strLabel = params[1].get_str();

	// Whether to perform rescan after import
	bool fRescan = true;
	if (params.size() > 2)
		fRescan = params[2].get_bool();

	{
		LOCK2(cs_main, pwalletMain->cs_wallet);

		// Don't throw error in case an address is already there
		if (pwalletMain->HaveWatchOnly(dest))
			return Value::null;

		pwalletMain->MarkDirty();
		pwalletMain->SetAddressBookName(dest, strLabel);

		if (!pwalletMain->AddWatchOnly(dest))
			throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");

		if (fRescan) {
			pwalletMain->ScanForWalletTransactions(pindexGenesisBlock, true);
			pwalletMain->ReacceptWalletTransactions();
		}
	}

	return Value::null;
}

Value importwallet(const Array& params, bool fHelp) {
	if (fHelp || params.size() != 1)
		throw runtime_error(
		    "importwallet <filename>\n"
		    "Imports keys from a wallet dump file (see dumpwallet).");

	EnsureWalletIsUnlocked();

	ifstream file;
	file.open(params[0].get_str().c_str());
	if (!file.is_open())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

	int64_t nTimeBegin = pindexBest->nTime;

	bool fGood = true;

	while (file.good()) {
		std::string line;
		std::getline(file, line);
		if (line.empty() || line[0] == '#')
			continue;

		std::vector<std::string> vstr;
		boost::split(vstr, line, boost::is_any_of(" "));
		if (vstr.size() < 2)
			continue;
		CBitcoinSecret vchSecret;
		if (!vchSecret.SetString(vstr[0]))
			continue;
		CKey    key    = vchSecret.GetKey();
		CPubKey pubkey = key.GetPubKey();
		CKeyID  keyid  = pubkey.GetID();
		if (pwalletMain->HaveKey(keyid)) {
			LogPrintf("Skipping import of %s (key already present)\n",
			          CBitcoinAddress(keyid).ToString());
			continue;
		}
		int64_t     nTime = DecodeDumpTime(vstr[1]);
		std::string strLabel;
		bool        fLabel = true;
		for (uint32_t nStr = 2; nStr < vstr.size(); nStr++) {
			if (boost::algorithm::starts_with(vstr[nStr], "#"))
				break;
			if (vstr[nStr] == "change=1")
				fLabel = false;
			if (vstr[nStr] == "reserve=1")
				fLabel = false;
			if (boost::algorithm::starts_with(vstr[nStr], "label=")) {
				strLabel = DecodeDumpString(vstr[nStr].substr(6));
				fLabel   = true;
			}
		}
		LogPrintf("Importing %s...\n", CBitcoinAddress(keyid).ToString());
		if (!pwalletMain->AddKey(key)) {
			fGood = false;
			continue;
		}
		pwalletMain->mapKeyMetadata[keyid].nCreateTime = nTime;
		if (fLabel)
			pwalletMain->SetAddressBookName(keyid, strLabel);
		nTimeBegin = std::min(nTimeBegin, nTime);
	}
	file.close();

	CBlockIndex* pindex = pindexBest;
	while (pindex && pindex->Prev() && pindex->nTime > nTimeBegin - 7200)
		pindex = pindex->Prev();

	if (!pwalletMain->nTimeFirstKey || nTimeBegin < pwalletMain->nTimeFirstKey)
		pwalletMain->nTimeFirstKey = nTimeBegin;

	LogPrintf("Rescanning last %i blocks\n", pindexBest->nHeight - pindex->nHeight + 1);
	pwalletMain->ScanForWalletTransactions(pindex);
	pwalletMain->ReacceptWalletTransactions();
	pwalletMain->MarkDirty();

	if (!fGood)
		throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

	return Value::null;
}

Value dumpprivkey(const Array& params, bool fHelp) {
	if (fHelp || params.size() != 1)
		throw runtime_error(
		    "dumpprivkey <bitbayaddress>\n"
		    "Reveals the private key corresponding to <bitbayaddress>.");

	EnsureWalletIsUnlocked();

	string          strAddress = params[0].get_str();
	CBitcoinAddress address;
	if (!address.SetString(strAddress))
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid BitBay address");
	if (fWalletUnlockStakingOnly)
		throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for staking only.");
	CKeyID keyID;
	if (!address.GetKeyID(keyID))
		throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
	CKey vchSecret;
	if (!pwalletMain->GetKey(keyID, vchSecret))
		throw JSONRPCError(RPC_WALLET_ERROR,
		                   "Private key for address " + strAddress + " is not known");
	CPubKey pubkey = vchSecret.GetPubKey();
	if (!pubkey.IsValid())
		throw JSONRPCError(RPC_WALLET_ERROR,
						   "Public key for address " + strAddress + " is not valid");

	Object result;

	result.push_back(Pair("address", strAddress));
	result.push_back(Pair("pubkey", HexStr(pubkey.begin(), pubkey.end())));
	result.push_back(Pair("secret", CBitcoinSecret(vchSecret).ToString()));

	Object result_evm;

	eth_account acc;
	if (eth_account_from_privkey(&acc, vchSecret.begin()) > 0) {
		int  len_cstr = 0;
		char addr_cstr[41];
		len_cstr = eth_account_address_get(addr_cstr, &acc);
		if (len_cstr > 0) {
			addr_cstr[len_cstr] = 0;
			result_evm.push_back(Pair("address", "0x" + string(addr_cstr)));
		}
		char prvk_cstr[65];
		len_cstr = eth_account_privkey_get(prvk_cstr, &acc);
		if (len_cstr > 0) {
			prvk_cstr[len_cstr] = 0;
			result_evm.push_back(Pair("privatekey", "0x" + string(prvk_cstr)));
		}
	}

	result.push_back(Pair("evm", result_evm));

	return result;
}

Value dumpwallet(const Array& params, bool fHelp) {
	if (fHelp || params.size() != 1)
		throw runtime_error(
		    "dumpwallet <filename>\n"
		    "Dumps all wallet keys in a human-readable format.");

	EnsureWalletIsUnlocked();

	ofstream file;
	file.open(params[0].get_str().c_str());
	if (!file.is_open())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

	std::map<CKeyID, int64_t> mapKeyBirth;

	std::set<CKeyID> setKeyPool;

	pwalletMain->GetKeyBirthTimes(mapKeyBirth);

	pwalletMain->GetAllReserveKeys(setKeyPool);

	// sort time/key pairs
	std::vector<std::pair<int64_t, CKeyID> > vKeyBirth;
	for (std::map<CKeyID, int64_t>::const_iterator it = mapKeyBirth.begin();
	     it != mapKeyBirth.end(); it++) {
		vKeyBirth.push_back(std::make_pair(it->second, it->first));
	}
	mapKeyBirth.clear();
	std::sort(vKeyBirth.begin(), vKeyBirth.end());

	// produce output
	file << strprintf("# Wallet dump created by BitBay %s (%s)\n", CLIENT_BUILD, CLIENT_DATE);
	file << strprintf("# * Created on %s\n", EncodeDumpTime(GetTime()));
	file << strprintf("# * Best block at time of backup was %i (%s),\n", nBestHeight,
	                  hashBestChain.ToString());
	file << strprintf("#   mined on %s\n", EncodeDumpTime(pindexBest->nTime));
	file << "\n";
	for (std::vector<std::pair<int64_t, CKeyID> >::const_iterator it = vKeyBirth.begin();
	     it != vKeyBirth.end(); it++) {
		const CKeyID& keyid   = it->second;
		std::string   strTime = EncodeDumpTime(it->first);
		std::string   strAddr = CBitcoinAddress(keyid).ToString();

		CKey key;
		if (pwalletMain->GetKey(keyid, key)) {
			if (pwalletMain->mapAddressBook.count(keyid)) {
				file << strprintf("%s %s label=%s # addr=%s\n", CBitcoinSecret(key).ToString(),
				                  strTime, EncodeDumpString(pwalletMain->mapAddressBook[keyid]),
				                  strAddr);
			} else if (setKeyPool.count(keyid)) {
				file << strprintf("%s %s reserve=1 # addr=%s\n", CBitcoinSecret(key).ToString(),
				                  strTime, strAddr);
			} else {
				file << strprintf("%s %s change=1 # addr=%s\n", CBitcoinSecret(key).ToString(),
				                  strTime, strAddr);
			}
		}
	}
	file << "\n";
	file << "# End of dump\n";
	file.close();
	return Value::null;
}
