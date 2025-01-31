// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "init.h"
#include "net.h"
#include "pegdb-leveldb.h"
#include "proposals.h"
#include "rpcserver.h"
#include "util.h"
#include "wallet.h"
#include "walletdb.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/assign/list_of.hpp>

using namespace std;
using namespace boost::assign;
using namespace json_spirit;

Value myproposals(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("myproposals\n");

	if (params.size() != 0)
		throw runtime_error("myproposals\n");

	Array results;

	CPegDB      pegdb;
	set<string> sTrustedStakers1, sTrustedStakers2;
	pindexBest->ReadTrustedStakers1(pegdb, sTrustedStakers1);
	pindexBest->ReadTrustedStakers2(pegdb, sTrustedStakers2);
	std::map<int, CChainParams::ConsensusVotes> consensus;
	pindexBest->ReadConsensusMap(pegdb, consensus);
	map<string, vector<string>> bridges;
	pindexBest->ReadBridgesMap(pegdb, bridges);
	auto fnHasMerkleIn = [&](string hash) {
		CMerkleInfo m = pindexBest->ReadMerkleIn(pegdb, hash);
		return !m.hash.empty();
	};

	vector<string> ids;
	CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
	for (const string& id : ids) {
		if (id == "")
			continue;
		Object proposal;
		proposal.push_back(Pair("id", id));

		vector<string> datas;
		CWalletDB(pwalletMain->strWalletFile).ReadProposal(id, datas);

		bool   active       = true;
		string status       = "OK";
		Object proposal_obj = ProposalToJson(datas, sTrustedStakers1, sTrustedStakers2, consensus,
											 bridges, fnHasMerkleIn, active, status);

		if (!proposal_obj.empty()) {
			proposal_obj.push_back(Pair("id", id));
			results.push_back(proposal_obj);
			continue;
		}
	}

	return results;
}

Value addproposal(const Array& params, bool fHelp) {
	if (params.size() == 0) {
		fHelp = true;
	}

	if (fHelp)
		throw runtime_error(
		    "addproposal <tstakers1|tstakers2> <add|remove> <address>\n"
			"addproposal <consensus> <tstakers|consensus|bridge|merkle|timelockpass> "
			"votes_t1,votes_t2,votes\n"
		    "addproposal <bridge> add name symbol url1,url2 chain_id contract pegsteps microsteps\n"
		    "addproposal <bridge> remove name\n"
		    "addproposal <merkle> add bridge merkle amount\n"
			"addproposal <timelockpass> <add|remove> pubkey\n"
			"addproposal <signedproposalhex>\n");

	string scope = params[0].get_str();

	if (scope == "tstakers1" || scope == "tstakers2") {
		if (params.size() != 3) {
			throw runtime_error("addproposal <tstakers1|tstakers2> <add|remove> <address>\n");
		}

		string action      = params[1].get_str();
		string address_txt = params[2].get_str();

		CBitcoinAddress address(address_txt);
		if (!address.IsValid())
			throw runtime_error("cannot decode address.\n");

		if (scope != "tstakers1" && scope != "tstakers2")
			throw runtime_error("not scope tstakers1|tstakers2.\n");

		if (action != "add" && action != "remove")
			throw runtime_error("not actions add|remove.\n");

		string phash;
		{
			CDataStream ss(SER_GETHASH, 0);
			ss << scope;
			ss << action;
			ss << address_txt;
			phash = Hash(ss.begin(), ss.end()).GetHex();
		}

		vector<string> ids;
		vector<string> ids_new;
		CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
		for (const string& id : ids) {
			if (id == "")
				continue;
			if (id == phash) {
				throw runtime_error("proposal is already in the list.\n");
			}
			ids_new.push_back(id);
		}
		ids_new.push_back(phash);
		CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new);

		int            t_until = GetTime() + 3600 * 24 * 3;
		vector<string> datas;
		datas.push_back(scope);
		datas.push_back(std::to_string(t_until));
		datas.push_back(action);
		datas.push_back(address_txt);
		CWalletDB(pwalletMain->strWalletFile).WriteProposal(phash, datas);

		Object proposal;
		proposal.push_back(Pair("id", phash));
		proposal.push_back(Pair("until", t_until));
		proposal.push_back(Pair("scope", scope));
		proposal.push_back(Pair("action", action));
		proposal.push_back(Pair("address", address_txt));

		return proposal;
	}

	if (scope == "consensus") {
		if (params.size() != 3) {
			throw runtime_error(
				"addproposal <consensus> <tstakers|consensus|bridge|merkle|timelockpass> "
				"votes_t1,votes_t2,votes\n");
		}
		string consensus_type = params[1].get_str();
		if (consensus_type != "tstakers" && consensus_type != "consensus" &&
			consensus_type != "bridge" && consensus_type != "merkle" &&
			consensus_type != "timelockpass") {
			throw runtime_error("unknown consensus type\n");
		}
		string         votes_txt = params[2].get_str();
		vector<string> votes;
		boost::split(votes, votes_txt, boost::is_any_of(","));
		if (votes.size() != 3) {
			throw runtime_error(
				"addproposal <consensus> <tstakers|consensus|bridge|merkle|timelockpass> "
				"votes_t1,votes_t2,votes\n");
		}
		int t1_votes = std::atoi(votes[0].c_str());
		int t2_votes = std::atoi(votes[1].c_str());
		int o_votes  = std::atoi(votes[2].c_str());

		if (!all_of(votes[0].begin(), votes[0].end(), ::isdigit))
			throw runtime_error("votes_t1 not a number\n");
		if (!all_of(votes[1].begin(), votes[1].end(), ::isdigit))
			throw runtime_error("votes_t2 not a number\n");
		if (!all_of(votes[2].begin(), votes[2].end(), ::isdigit))
			throw runtime_error("votes not a number\n");

		// TODO check ranges and diff

		string phash;
		{
			CDataStream ss(SER_GETHASH, 0);
			ss << scope;
			ss << consensus_type;
			ss << t1_votes;
			ss << t2_votes;
			ss << o_votes;
			phash = Hash(ss.begin(), ss.end()).GetHex();
		}

		vector<string> ids;
		vector<string> ids_new;
		CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
		for (const string& id : ids) {
			if (id == "")
				continue;
			if (id == phash) {
				throw runtime_error("proposal is already in the list.\n");
			}
			ids_new.push_back(id);
		}
		ids_new.push_back(phash);
		CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new);

		int            t_until = GetTime() + 3600 * 24 * 3;
		vector<string> datas;
		datas.push_back(scope);
		datas.push_back(std::to_string(t_until));
		datas.push_back(consensus_type);
		datas.push_back(votes[0]);
		datas.push_back(votes[1]);
		datas.push_back(votes[2]);
		CWalletDB(pwalletMain->strWalletFile).WriteProposal(phash, datas);

		Object proposal;
		proposal.push_back(Pair("id", phash));
		proposal.push_back(Pair("until", t_until));
		proposal.push_back(Pair("scope", scope));
		proposal.push_back(Pair("consensus_type", consensus_type));

		Object con;
		con.push_back(Pair("tstakers1", t1_votes));
		con.push_back(Pair("tstakers2", t2_votes));
		con.push_back(Pair("ostakers", o_votes));
		proposal.push_back(Pair("consensus_votes", con));

		return proposal;
	}

	if (scope == "bridge") {
		if (params.size() < 2) {
			throw runtime_error(
			    "addproposal <bridge> add name symbol links chain_id contract pegsteps microsteps\n"
			    "addproposal <bridge> remove name\n");
		}

		string action = params[1].get_str();
		if (action != "add" && action != "remove")
			throw runtime_error("not actions add|remove.\n");

		if (action == "add") {
			if (params.size() < 9) {
				throw runtime_error(
				    "addproposal <bridge> add name symbol url1,url2 chain_id contract pegsteps "
				    "microsteps\n");
			}

			string name = params[2].get_str();
			string brhash;
			{
				CDataStream ss(SER_GETHASH, 0);
				ss << name;
				brhash = Hash(ss.begin(), ss.end()).GetHex();
			}

			string symb           = params[3].get_str();
			string urls_txt       = params[4].get_str();
			string chain_id_txt   = params[5].get_str();
			string contract       = params[6].get_str();
			string pegsteps_txt   = params[7].get_str();
			string microsteps_txt = params[8].get_str();

			// parse urls
			set<string> urls;
			boost::split(urls, urls_txt, boost::is_any_of(","));
			for (const string& url : urls) {
				if (!boost::starts_with(url, "https://")) {
					throw runtime_error("url has no htps:// prefix\n");
				}
			}

			// parse chain_id
			if (!all_of(chain_id_txt.begin(), chain_id_txt.end(), ::isdigit))
				throw runtime_error("chain_id not a number\n");
			int chain_id = std::atoi(chain_id_txt.c_str());

			if (!all_of(pegsteps_txt.begin(), pegsteps_txt.end(), ::isdigit))
				throw runtime_error("pegsteps not a number\n");
			int pegsteps = std::atoi(pegsteps_txt.c_str());

			if (!all_of(microsteps_txt.begin(), microsteps_txt.end(), ::isdigit))
				throw runtime_error("microsteps not a number\n");
			int microsteps = std::atoi(microsteps_txt.c_str());

			// parse contract
			if (!boost::starts_with(contract, "0x")) {
				throw runtime_error("contract has no 0x prefix\n");
			}
			if (!IsHex(contract.substr(2)) || contract.size() != 42) {
				throw runtime_error("contract is not 42 symbols length hex string\n");
			}

			string phash;
			{
				CDataStream ss(SER_GETHASH, 0);
				ss << scope;
				ss << action;
				ss << name;
				ss << symb;
				for (const string& url : urls) {
					ss << url;
				}
				ss << chain_id;
				ss << contract;
				ss << pegsteps;
				ss << microsteps;
				phash = Hash(ss.begin(), ss.end()).GetHex();
			}

			vector<string> ids;
			vector<string> ids_new;
			CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
			for (const string& id : ids) {
				if (id == "")
					continue;
				if (id == phash) {
					throw runtime_error("proposal is already in the list.\n");
				}
				ids_new.push_back(id);
			}
			ids_new.push_back(phash);
			CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new);

			int            t_until = GetTime() + 3600 * 24 * 3;
			vector<string> datas;
			datas.push_back(scope);
			datas.push_back(std::to_string(t_until));
			datas.push_back(action);
			datas.push_back(name);
			datas.push_back(symb);
			datas.push_back(boost::algorithm::join(urls, ";"));
			datas.push_back(chain_id_txt);
			datas.push_back(contract);
			datas.push_back(pegsteps_txt);
			datas.push_back(microsteps_txt);
			CWalletDB(pwalletMain->strWalletFile).WriteProposal(phash, datas);

			Object proposal;
			proposal.push_back(Pair("id", phash));
			proposal.push_back(Pair("until", t_until));
			proposal.push_back(Pair("scope", scope));
			proposal.push_back(Pair("action", action));
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
			proposal.push_back(Pair("bridge", bridge));

			return proposal;
		}

		if (action == "remove") {
			if (params.size() < 3) {
				throw runtime_error("addproposal <bridge> remove name\n");
			}

			string name = params[2].get_str();
			string brhash;
			{
				CDataStream ss(SER_GETHASH, 0);
				ss << name;
				brhash = Hash(ss.begin(), ss.end()).GetHex();
			}

			string phash;
			{
				CDataStream ss(SER_GETHASH, 0);
				ss << scope;
				ss << action;
				ss << name;
				phash = Hash(ss.begin(), ss.end()).GetHex();
			}

			vector<string> ids;
			vector<string> ids_new;
			CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
			for (const string& id : ids) {
				if (id == "")
					continue;
				if (id == phash) {
					throw runtime_error("proposal is already in the list.\n");
				}
				ids_new.push_back(id);
			}
			ids_new.push_back(phash);
			CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new);

			int            t_until = GetTime() + 3600 * 24 * 3;
			vector<string> datas;
			datas.push_back(scope);
			datas.push_back(std::to_string(t_until));
			datas.push_back(action);
			datas.push_back(name);
			CWalletDB(pwalletMain->strWalletFile).WriteProposal(phash, datas);

			Object proposal;
			proposal.push_back(Pair("id", phash));
			proposal.push_back(Pair("until", t_until));
			proposal.push_back(Pair("scope", scope));
			proposal.push_back(Pair("action", action));
			Object bridge;
			bridge.push_back(Pair("name", name));
			bridge.push_back(Pair("hash", brhash));
			proposal.push_back(Pair("bridge", bridge));

			return proposal;
		}

		Object proposal;
		return proposal;
	}

	CPegDB                      pegdb;
	map<string, vector<string>> bridges;
	pindexBest->ReadBridgesMap(pegdb, bridges);

	if (scope == "merkle") {
		if (params.size() < 2) {
			throw runtime_error("addproposal <merkle> add bridge merkle amount\n");
		}

		string action = params[1].get_str();
		if (action != "add")
			throw runtime_error("not actions add.\n");

		if (action == "add") {
			if (params.size() < 5) {
				throw runtime_error("addproposal <merkle> add bridge merkle amount\n");
			}

			string  brname     = params[2].get_str();
			string  merkle0x   = params[3].get_str();
			string  amount_txt = params[4].get_str();
			int64_t amount     = strtoll(amount_txt.c_str(), 0, 0);

			if (bridges.count(brname) == 0) {
				throw runtime_error("bridge is not listed\n");
			}
			// parse merkle
			if (!boost::starts_with(merkle0x, "0x")) {
				throw runtime_error("merkle has no 0x prefix\n");
			}
			if (!IsHex(merkle0x.substr(2)) || merkle0x.size() != 66) {
				throw runtime_error("merkle is not 66 symbols length hex string\n");
			}
			string merkle = merkle0x.substr(2);

			string phash;
			{
				CDataStream ss(SER_GETHASH, 0);
				ss << scope;
				ss << action;
				ss << brname;
				ss << merkle;
				ss << amount;
				phash = Hash(ss.begin(), ss.end()).GetHex();
			}

			vector<string> ids;
			vector<string> ids_new;
			CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
			for (const string& id : ids) {
				if (id == "")
					continue;
				if (id == phash) {
					throw runtime_error("proposal is already in the list.\n");
				}
				ids_new.push_back(id);
			}
			ids_new.push_back(phash);
			CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new);

			int            t_until = GetTime() + 3600 * 24 * 3;
			vector<string> datas;
			datas.push_back(scope);
			datas.push_back(std::to_string(t_until));
			datas.push_back(action);
			datas.push_back(brname);
			datas.push_back(merkle);
			datas.push_back(std::to_string(amount));
			CWalletDB(pwalletMain->strWalletFile).WriteProposal(phash, datas);

			Object proposal;
			proposal.push_back(Pair("id", phash));
			proposal.push_back(Pair("until", t_until));
			proposal.push_back(Pair("scope", scope));
			proposal.push_back(Pair("action", action));
			proposal.push_back(Pair("bridge", brname));
			proposal.push_back(Pair("merkle", merkle));
			proposal.push_back(Pair("amount", amount));

			return proposal;
		}

		Object proposal;
		return proposal;
	}

	if (scope == "timelockpass") {
		if (params.size() != 3) {
			throw runtime_error("addproposal <timelockpass> <add|remove> pubkey\n");
		}

		string action     = params[1].get_str();
		string pubkey_txt = params[2].get_str();

		std::vector<unsigned char> pubkey_vch = ParseHex(pubkey_txt);
		CPubKey                    pubKey(pubkey_vch);
		if (!pubKey.IsValid())
			throw runtime_error("not valid pubkey.\n");

		if (scope != "timelockpass")
			throw runtime_error("not scope timelockpass.\n");

		if (action != "add" && action != "remove")
			throw runtime_error("not actions add|remove.\n");

		string phash;
		{
			CDataStream ss(SER_GETHASH, 0);
			ss << scope;
			ss << action;
			ss << pubkey_txt;
			phash = Hash(ss.begin(), ss.end()).GetHex();
		}

		vector<string> ids;
		vector<string> ids_new;
		CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
		for (const string& id : ids) {
			if (id == "")
				continue;
			if (id == phash) {
				throw runtime_error("proposal is already in the list.\n");
			}
			ids_new.push_back(id);
		}
		ids_new.push_back(phash);
		CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new);

		int            t_until = GetTime() + 3600 * 24 * 3;
		vector<string> datas;
		datas.push_back(scope);
		datas.push_back(std::to_string(t_until));
		datas.push_back(action);
		datas.push_back(pubkey_txt);
		CWalletDB(pwalletMain->strWalletFile).WriteProposal(phash, datas);

		Object proposal;
		proposal.push_back(Pair("id", phash));
		proposal.push_back(Pair("until", t_until));
		proposal.push_back(Pair("scope", scope));
		proposal.push_back(Pair("action", action));
		proposal.push_back(Pair("pubkey", pubkey_txt));

		return proposal;
	}

	/*

7b226b223a5b22303233393566643832323137396635313235633632666662616131386235613338383830616437303566356433373865306532633130643865353634663462663164225d2c226d223a222a2a422a2a317b5c226e5c223a5c225365706f6c696130325c222c5c22735c223a5c227365704554485c222c5c226c5c223a5b5c2268747470733a2f2f7270632e616e6b722e636f6d2f6574685f7365706f6c69615c225d2c5c22695c223a31313135353131312c5c22635c223a5c223078363739324631346641443662643431373966384533344166394237383643353737623833653135425c222c5c22705c223a33302c5c226d5c223a387d222c226e223a313732373030353633332c2273223a2232306339326664386338363931316433363062373761383035363863353332323262633137316635666632363638383531666237636134356333633436366535636434393732336531343134303962393539336466613361656231386131386235663731663163343430626265316130633861353931346537313465353235626132227d
7b226b223a5b22303233393566643832323137396635313235633632666662616131386235613338383830616437303566356433373865306532634130643865353634663462663164225d2c226d223a222a2a422a2a317b5c226e5c223a5c225365706f6c696130325c222c5c22735c223a5c227365704554485c222c5c226c5c223a5b5c2268747470733a2f2f7270632e616e6b722e636f6d2f6574685f7365706f6c69615c225d2c5c22695c223a31313135353131312c5c22635c223a5c223078363739324631346641443662643431373966384533344166394237383643353737623833653135425c222c5c22705c223a33302c5c226d5c223a387d222c226e223a313732373030353633332c2273223a2232306339326664386338363931316433363062373761383035363863353332323262633137316635666632363638383531666237636134356333633436366535636434393732336531343134303962393539336466613361656231386131386235663731663163343430626265316130633861353931346537313465353235626132227d

	*/

	if (params.size() == 1) {  // hex-encoded proposal
		vector<unsigned char> jdata(ParseHex(params[0].get_str()));
		string                jraw = string(jdata.begin(), jdata.end());

		Object obj;
		// obj.push_back(Pair("jraw", jraw));

		string         sighex;
		string         prop;
		int64_t        nonce;
		vector<string> pubks;
		string         pub1hex;

		try {
			json_spirit::Value jval;
			json_spirit::read_string(jraw, jval);
			const json_spirit::Object& jobj = jval.get_obj();
			if (!jobj.empty()) {
				obj.push_back(Pair("jobj", jobj));

				Array pubs = json_spirit::find_value(jobj, "k").get_array();
				sighex     = json_spirit::find_value(jobj, "s").get_str();
				prop       = json_spirit::find_value(jobj, "m").get_str();
				nonce      = json_spirit::find_value(jobj, "n").get_int64();
				if (!pubs.empty())
					pub1hex = pubs[0].get_str();
				for (Value pub : pubs)
					pubks.push_back(pub.get_str());
			}
		} catch (std::exception& e) {
			throw runtime_error("proposal json parsing error.\n");
		}

		if (pub1hex.empty())
			throw runtime_error("proposal json has no pubkeys.\n");
		std::vector<unsigned char> pubkey_vch = ParseHex(pub1hex);
		CPubKey                    pubkey(pubkey_vch);
		if (!pubkey.IsValid())
			throw runtime_error("not valid pubkey.\n");
		CDataStream ss(SER_GETHASH, 0);
		ss << prop;
		ss << std::to_string(nonce);
		std::vector<unsigned char> vchSig = ParseHex(sighex);
		if (!pubkey.VerifyCompact(Hash(ss.begin(), ss.end()), vchSig))
			throw runtime_error("signature check failed.\n");

		obj.push_back(Pair("signature_check", true));

		scope         = "onbehalf";
		string action = "add";

		string phash;
		{
			CDataStream ss(SER_GETHASH, 0);
			ss << scope;
			ss << action;
			for (string pubk : pubks)
				ss << pubk;
			ss << prop;
			ss << std::to_string(nonce);
			ss << sighex;
			phash = Hash(ss.begin(), ss.end()).GetHex();
		}

		vector<string> ids;
		vector<string> ids_new;
		CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
		for (const string& id : ids) {
			if (id == "")
				continue;
			if (id == phash) {
				throw runtime_error("proposal " + id + " is already in the list.\n");
			}
			ids_new.push_back(id);
		}
		ids_new.push_back(phash);
		CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new);

		int            t_until = GetTime() + 3600 * 24 * 3;
		vector<string> datas;
		datas.push_back(scope);
		datas.push_back(std::to_string(t_until));
		datas.push_back(action);
		datas.push_back(boost::algorithm::join(pubks, ";"));
		datas.push_back(HexStr(prop));
		datas.push_back(std::to_string(nonce));
		datas.push_back(sighex);
		CWalletDB(pwalletMain->strWalletFile).WriteProposal(phash, datas);

		Object proposal;
		proposal.push_back(Pair("id", phash));
		proposal.push_back(Pair("until", t_until));
		proposal.push_back(Pair("scope", scope));
		proposal.push_back(Pair("action", action));
		Object proposal2;
		Array  jpubs;
		for (const string& pubk : pubks) {
			jpubs.push_back(pubk);
		}
		proposal2.push_back(Pair("k", jpubs));
		proposal2.push_back(Pair("m", prop));
		proposal2.push_back(Pair("n", std::to_string(nonce)));
		proposal2.push_back(Pair("s", sighex));
		proposal.push_back(Pair("proposal", proposal2));

		return proposal;

		return obj;
	}

	throw runtime_error("addproposal unknown scope\n");
}

Value voteproposal(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("voteproposal id\n");

	if (params.size() < 1 || params.size() > 1)
		throw runtime_error("voteproposal id\n");

	RPCTypeCheck(params, list_of(str_type));

	string id_proposal = params[0].get_str();

	vector<string> ids_my;
	CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids_my);
	for (const string& id : ids_my) {
		if (id == "")
			continue;
		if (id == id_proposal)
			throw runtime_error("proposal is already in my list.\n");
	}

	CPegDB       pegdb;
	CBlockIndex* cycle_block = pindexBest->PegCycleBlock();
	uint256      chash       = cycle_block->GetBlockHash();
	set<string>  ids;
	pegdb.ReadCycleProposals(chash, ids);
	for (const string& id : ids) {
		if (id == "")
			continue;
		if (id != id_proposal)
			continue;

		vector<string> pdatas;
		if (pegdb.ReadProposal(id, pdatas)) {
			if (pdatas.size() < 2) {  // no scope, until
				continue;
			}
			bool           has_proposal = false;
			vector<string> ids_my;
			vector<string> ids_new_my;
			CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids_my);
			for (const string& id : ids_my) {
				if (id == "")
					continue;
				if (id == id_proposal) {
					has_proposal = true;
					int t_now   = GetTime();
					int t_until = std::atoi(pdatas[1].c_str());
					if (t_now > t_until) {
						// proposal is already in list, old, reactivate
					} else {
						throw runtime_error("proposal is active and already in my list.\n");
					}
				}
				ids_new_my.push_back(id);
			}
			// update "until"
			int t_until = GetTime() + 3600 * 24 * 3;
			pdatas[1]   = std::to_string(t_until);
			if (!has_proposal) {
				ids_new_my.push_back(id_proposal);
			}
			CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new_my);
			CWalletDB(pwalletMain->strWalletFile).WriteProposal(id_proposal, pdatas);
		}
	}

	Object result;
	result.push_back(Pair("id", id_proposal));
	result.push_back(Pair("added", true));

	return result;
}

Value removeproposal(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("removeproposal id\n");

	if (params.size() < 1 || params.size() > 1)
		throw runtime_error("removeproposal id\n");

	RPCTypeCheck(params, list_of(str_type));

	string phash = params[0].get_str();

	bool           removed = false;
	vector<string> ids;
	vector<string> ids_new;
	CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
	for (const string& id : ids) {
		if (id == "")
			continue;
		if (id == phash) {
			removed = true;
			continue;
		}
		ids_new.push_back(id);
	}
	bool written = CWalletDB(pwalletMain->strWalletFile).WriteProposals(ids_new);

	Object result;
	result.push_back(Pair("id", phash));
	result.push_back(Pair("removed", removed && written));

	return result;
}

Value signproposal(const Array& params, bool fHelp) {
	if (fHelp)
		throw runtime_error("signproposal id address\n");

	if (params.size() < 2 || params.size() > 2)
		throw runtime_error("signproposal id address\n");

	RPCTypeCheck(params, list_of(str_type));

	string phash = params[0].get_str();
	string addr  = params[1].get_str();

	CBitcoinAddress address = CBitcoinAddress(addr);
	// CScript         scriptPubKey;
	if (!address.IsValid())
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid BitBay address");
	CKeyID keyID;
	if (!address.GetKeyID(keyID))
		throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid BitBay address(S)");

	EnsureWalletIsUnlocked();

	vector<string> ids;
	CWalletDB(pwalletMain->strWalletFile).ReadProposals(ids);
	for (const string& id : ids) {
		if (id == "")
			continue;
		if (id != phash) {
			continue;
		}

		CPegDB         pegdb;
		vector<string> pdatas;
		if (pegdb.ReadProposal(id, pdatas)) {
			string notary = ProposalToNotary(pdatas);
			if (notary == "")
				continue;
			CKey key;
			if (!pwalletMain->GetKey(keyID, key))
				throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: No key available.");
			int64_t     nonce = GetTime();
			CDataStream ss(SER_GETHASH, 0);
			ss << notary;
			ss << std::to_string(nonce);
			std::vector<unsigned char> vchSig;
			if (!key.SignCompact(Hash(ss.begin(), ss.end()), vchSig))
				throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Sign error.");
			Object jobj;
			Array  jkeys;
			jkeys.push_back(HexStr(key.GetPubKey()));
			jobj.push_back(Pair("k", jkeys));
			jobj.push_back(Pair("m", notary));
			jobj.push_back(Pair("n", nonce));
			jobj.push_back(Pair("s", HexStr(vchSig)));
			string jraw = json_spirit::write_string(Value(jobj), false);
			return Value(HexStr(jraw));
		}
	}

	Object result;
	return result;
}

/*

* *S* *###{
* 'k':[
* '045eb46525110bcf05e4494538bca1792e593a059d880a41d805eb2e3beeb0ae3c2817d24e84e32b49651b3b2c0ef83f93eef6367a545ff066a40341acabd30cc5',
* '046a44e2eae61de24144119b6148b419e8537f07d9dd21290ec3071a00fde01004a6df57ef18924b9733c40bad2de7fd883c3453cfce87e356f8a91dd00a4f391c'
* ],
* 's':'3045022100cf48d1a44655948cb283ebbec485a035cefca560e39d5333e4ae788c3592eaca022005a0bc4cad4ae7edfb7451a9e6e42598a91ead90509be4af91f06d41777ec7b6',
* 'm':'* *X*
*1049257afe04c69fa0a9f1719ed5d3aacd45fde634d93c235d4955363c7609f139b144e935f2d84e4fc1fb27bea2af34950de9a60c64580f56acbcc6fc29863eb8d',
* 'n':0}


 */
