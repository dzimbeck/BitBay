// Copyright (c) 2024 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposals.h"

#include "base58.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>

#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"

using namespace std;
using namespace json_spirit;

Object ProposalToJson(vector<string>                         datas,
					  set<string>                            sTrustedStakers1,
					  set<string>                            sTrustedStakers2,
					  map<int, CChainParams::ConsensusVotes> consensus,
					  map<string, vector<string>>            bridges,
					  std::function<bool(std::string)>       f_merkle_in_has,
					  bool&                                  active,
					  string&                                status) {
	Object proposal;
	active = true;
	status = "OK";

	if (datas.size() < 1) {  // no scope
		active = false;
		status = "N/A";
		return proposal;
	}

	string scope = datas[0];
	if (scope == "tstakers1" || scope == "tstakers2") {
		if (datas.size() == 4) {
			int t_now   = GetTime();
			int t_until = std::atoi(datas[1].c_str());
			if (t_now > t_until) {
				status = "Expired";
				active = false;
			}
			string      action  = datas[2];
			string      address = datas[3];
			set<string> stakers;
			if (scope == "tstakers1")
				stakers = sTrustedStakers1;
			if (scope == "tstakers2")
				stakers = sTrustedStakers2;
			if (action == "add" && stakers.count(address)) {
				status = "Inactive, the address is already in the list";
				active = false;
			}
			if (action == "remove" && stakers.count(address) == 0) {
				status = "Inactive, the address is not in the list";
				active = false;
			}

			proposal.push_back(Pair("mining", active));
			proposal.push_back(Pair("status", status));
			proposal.push_back(Pair("scope", scope));
			proposal.push_back(Pair("until", t_until));
			proposal.push_back(Pair("action", action));
			proposal.push_back(Pair("address", address));
			proposal.push_back(Pair("notary", ProposalToNotary(datas)));
			return proposal;
		}
	}

	if (scope == "consensus") {
		if (datas.size() == 6) {
			int t_now   = GetTime();
			int t_until = std::atoi(datas[1].c_str());
			if (t_now > t_until) {
				status = "Expired";
				active = false;
			}

			string consensus_type = datas[2];
			int    t1_votes       = std::atoi(datas[3].c_str());
			int    t2_votes       = std::atoi(datas[4].c_str());
			int    o_votes        = std::atoi(datas[5].c_str());

			if (consensus_type == "tstakers") {
				CChainParams::ConsensusVotes c = consensus[CChainParams::CONSENSUS_TSTAKERS];
				if (c.tstakers1 == t1_votes && c.tstakers2 == t2_votes && c.ostakers == o_votes) {
					status = "Inactive, the consensus is same";
					active = false;
				}
			}
			if (consensus_type == "consensus") {
				CChainParams::ConsensusVotes c = consensus[CChainParams::CONSENSUS_CONSENSUS];
				if (c.tstakers1 == t1_votes && c.tstakers2 == t2_votes && c.ostakers == o_votes) {
					status = "Inactive, the consensus is same";
					active = false;
				}
			}
			if (consensus_type == "bridge") {
				CChainParams::ConsensusVotes c = consensus[CChainParams::CONSENSUS_BRIDGE];
				if (c.tstakers1 == t1_votes && c.tstakers2 == t2_votes && c.ostakers == o_votes) {
					status = "Inactive, the consensus is same";
					active = false;
				}
			}
			if (consensus_type == "merkle") {
				CChainParams::ConsensusVotes c = consensus[CChainParams::CONSENSUS_MERKLE];
				if (c.tstakers1 == t1_votes && c.tstakers2 == t2_votes && c.ostakers == o_votes) {
					status = "Inactive, the consensus is same";
					active = false;
				}
			}
			if (consensus_type == "timelockpass") {
				CChainParams::ConsensusVotes c = consensus[CChainParams::CONSENSUS_TIMELOCKPASS];
				if (c.tstakers1 == t1_votes && c.tstakers2 == t2_votes && c.ostakers == o_votes) {
					status = "Inactive, the consensus is same";
					active = false;
				}
			}

			proposal.push_back(Pair("mining", active));
			proposal.push_back(Pair("status", status));
			proposal.push_back(Pair("scope", scope));
			proposal.push_back(Pair("until", t_until));
			proposal.push_back(Pair("consensus_type", consensus_type));

			Object con;
			con.push_back(Pair("tstakers1", t1_votes));
			con.push_back(Pair("tstakers2", t2_votes));
			con.push_back(Pair("ostakers", o_votes));
			proposal.push_back(Pair("consensus_votes", con));
			proposal.push_back(Pair("notary", ProposalToNotary(datas)));
			return proposal;
		}
	}

	if (scope == "bridge") {
		if (datas.size() == 10) {
			int t_now   = GetTime();
			int t_until = std::atoi(datas[1].c_str());
			if (t_now > t_until) {
				status = "Expired";
				active = false;
			}

			string action = datas[2];
			if (action == "add") {
				string name = datas[3];

				if (bridges.count(name)) {
					status = "Inactive, the bridge is active";
					active = false;
				}

				string symb       = datas[4];
				string urls_txt   = datas[5];
				int    chain_id   = std::atoi(datas[6].c_str());
				string contract   = datas[7];
				int    pegsteps   = std::atoi(datas[8].c_str());
				int    microsteps = std::atoi(datas[9].c_str());

				set<string> urls;
				boost::split(urls, urls_txt, boost::is_any_of(","));

				proposal.push_back(Pair("mining", active));
				proposal.push_back(Pair("status", status));
				proposal.push_back(Pair("scope", scope));
				proposal.push_back(Pair("until", t_until));
				proposal.push_back(Pair("action", action));
				Object bridge;
				bridge.push_back(Pair("name", name));
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
				proposal.push_back(Pair("notary", ProposalToNotary(datas)));
				return proposal;
			}
		}

		if (datas.size() == 4) {
			int t_now   = GetTime();
			int t_until = std::atoi(datas[1].c_str());
			if (t_now > t_until) {
				status = "Expired";
				active = false;
			}

			string action = datas[2];

			if (action == "remove") {
				string name = datas[3];

				if (bridges.count(name) == 0) {
					status = "Inactive, the bridge is not listed";
					active = false;
				}

				proposal.push_back(Pair("mining", active));
				proposal.push_back(Pair("status", status));
				proposal.push_back(Pair("scope", scope));
				proposal.push_back(Pair("until", t_until));
				proposal.push_back(Pair("action", action));
				Object bridge;
				bridge.push_back(Pair("name", name));
				proposal.push_back(Pair("bridge", bridge));
				proposal.push_back(Pair("notary", ProposalToNotary(datas)));
				return proposal;
			}
		}
	}

	if (scope == "merkle") {
		if (datas.size() == 6) {
			int t_now   = GetTime();
			int t_until = std::atoi(datas[1].c_str());
			if (t_now > t_until) {
				status = "Expired";
				active = false;
			}

			string action = datas[2];
			if (action == "add") {
				string  brname     = datas[3];
				string  merkle     = datas[4];
				string  amount_txt = datas[5];
				int64_t amount     = strtoll(amount_txt.c_str(), 0, 0);

				if (bridges.count(brname) == 0) {
					status = "Inactive, the bridge is not listed";
					active = false;
				}
				if (f_merkle_in_has(merkle)) {
					status = "Inactive, the merkle is active";
					active = false;
				}

				proposal.push_back(Pair("mining", active));
				proposal.push_back(Pair("status", status));
				proposal.push_back(Pair("scope", scope));
				proposal.push_back(Pair("until", t_until));
				proposal.push_back(Pair("action", action));
				proposal.push_back(Pair("bridge", brname));
				proposal.push_back(Pair("merkle", merkle));
				proposal.push_back(Pair("amount", amount));
				proposal.push_back(Pair("notary", ProposalToNotary(datas)));
				return proposal;
			}
		}
	}

	if (scope == "timelockpass") {
		if (datas.size() == 4) {
			int t_now   = GetTime();
			int t_until = std::atoi(datas[1].c_str());
			if (t_now > t_until) {
				status = "Expired";
				active = false;
			}

			string action = datas[2];
			if (action == "add") {
				string pubkey_txt = datas[3];

				proposal.push_back(Pair("mining", active));
				proposal.push_back(Pair("status", status));
				proposal.push_back(Pair("scope", scope));
				proposal.push_back(Pair("until", t_until));
				proposal.push_back(Pair("action", action));
				proposal.push_back(Pair("pubkey", pubkey_txt));
				proposal.push_back(Pair("notary", ProposalToNotary(datas)));
				return proposal;
			}
		}
	}

	if (scope == "onbehalf") {
		if (datas.size() == 7) {
			int t_now   = GetTime();
			int t_until = std::atoi(datas[1].c_str());
			if (t_now > t_until) {
				status = "Expired";
				active = false;
			}

			string action = datas[2];
			if (action == "add") {
				vector<unsigned char> propdata = ParseHex(datas[4]);
				string                notary   = string(propdata.begin(), propdata.end());
				// notary can override status+active

				proposal.push_back(Pair("scope", scope));
				proposal.push_back(Pair("until", t_until));
				proposal.push_back(Pair("action", action));

				string         pubkeys_txt = datas[3];
				string         nonce       = datas[5];
				string         sighex      = datas[6];
				vector<string> pubkeys;
				boost::split(pubkeys, pubkeys_txt, boost::is_any_of(";"));
				if (pubkeys.size() > 0) {
					Array jpubs;
					for (const string& pubk : pubkeys)
						jpubs.push_back(pubk);
					if (pubkeys.size() == 1) {
						std::vector<unsigned char> pubkey_vch = ParseHex(pubkeys[0]);
						CPubKey                    cpubkey(pubkey_vch);
						if (cpubkey.IsValid()) {
							CKeyID keyID = cpubkey.GetID();
							proposal.push_back(Pair("address", CBitcoinAddress(keyID).ToString()));
						}
					}
					if (pubkeys.size() == 2) {
						bool                 valid = true;
						std::vector<CPubKey> cpubkeys;
						for (const string& pubkey : pubkeys) {
							std::vector<unsigned char> pubkey_vch = ParseHex(pubkey);
							CPubKey                    cpubkey(pubkey_vch);
							cpubkeys.push_back(cpubkey);
							if (!cpubkey.IsValid())
								valid = false;
						}
						if (valid) {
							CScript msigscript;
							msigscript.SetMultisig(2, cpubkeys);
							CScriptID mscriptID = msigscript.GetID();
							proposal.push_back(
								Pair("address", CBitcoinAddress(mscriptID).ToString()));
						}
					}
					string         phash;
					string         address_override_skip;
					vector<string> pdata =
						NotaryToProposal(notary, bridges, phash, address_override_skip);
					bool   pactive = true;
					string pstatus;
					Object proposal_obj =
						ProposalToJson(pdata, sTrustedStakers1, sTrustedStakers2, consensus,
									   bridges, f_merkle_in_has, pactive, pstatus);
					if (active && !pactive) {
						active = pactive;
						status = pstatus;
					}
					Object proposal_json;
					proposal.push_back(Pair("mining", active));
					proposal.push_back(Pair("status", status));
					proposal_json.push_back(Pair("k", jpubs));
					proposal_json.push_back(Pair("m", notary));
					proposal_json.push_back(Pair("n", nonce));
					proposal_json.push_back(Pair("s", sighex));
					proposal.push_back(Pair("json", proposal_json));
					proposal.push_back(Pair("proposal", proposal_obj));
					proposal.push_back(Pair("notary", ProposalToNotary(datas)));
					return proposal;
				}
			}
		}
	}

	active = false;
	status = "N/A";
	return proposal;
}

string ProposalToNotary(vector<string> datas) {
	string skip;
	string notary;

	string scope = datas[0];
	string until = datas[1];

	if (scope == "tstakers1" || scope == "tstakers2") {
		if (datas.size() < 4)
			return skip;
		string action      = datas[2];
		string address_txt = datas[3];

		notary += "**T**";
		if (scope == "tstakers1") {
			notary += "1";
		}
		if (scope == "tstakers2") {
			notary += "2";
		}
		if (action == "add") {
			notary += "1";
		} else if (action == "remove") {
			notary += "2";
		} else {
			notary.clear();
		}
		if (!notary.empty()) {
			CBitcoinAddress address(address_txt);
			if (address.IsValid())
				notary += address_txt;
			else
				notary.clear();
		}
	}

	if (scope == "consensus") {
		if (datas.size() < 6)
			return skip;

		string consensus_type = datas[2];
		int    t1_votes       = std::atoi(datas[3].c_str());
		int    t2_votes       = std::atoi(datas[4].c_str());
		int    o_votes        = std::atoi(datas[5].c_str());

		if (consensus_type == "tstakers" || consensus_type == "consensus" ||
			consensus_type == "bridge" || consensus_type == "merkle" ||
			consensus_type == "timelockpass") {
			notary += "**N**";

			if (consensus_type == "tstakers") {
				notary += "T";
			}
			if (consensus_type == "consensus") {
				notary += "N";
			}
			if (consensus_type == "bridge") {
				notary += "B";
			}
			if (consensus_type == "merkle") {
				notary += "M";
			}
			if (consensus_type == "timelockpass") {
				notary += "X";
			}

			notary += "[";
			notary += std::to_string(t1_votes) + ",";
			notary += std::to_string(t2_votes) + ",";
			notary += std::to_string(o_votes);
			notary += "]";
		}
	}

	if (scope == "bridge") {
		if (datas.size() < 3)
			return skip;

		string action = datas[2];
		if (action == "add") {
			if (datas.size() < 10)
				return skip;

			string name       = datas[3];
			string symb       = datas[4];
			string urls_txt   = datas[5];
			int    chain_id   = std::atoi(datas[6].c_str());
			string contract   = datas[7];
			int    pegsteps   = std::atoi(datas[8].c_str());
			int    microsteps = std::atoi(datas[9].c_str());

			set<string> urls;
			boost::split(urls, urls_txt, boost::is_any_of(","));

			Object jb;
			jb.push_back(Pair("n", name));
			jb.push_back(Pair("s", symb));
			Array jurls;
			for (const string& url : urls) {
				jurls.push_back(url);
			}
			jb.push_back(Pair("l", jurls));
			jb.push_back(Pair("i", chain_id));
			jb.push_back(Pair("c", contract));
			jb.push_back(Pair("p", pegsteps));
			jb.push_back(Pair("m", microsteps));

			string jraw = json_spirit::write_string(Value(jb), false);
			notary += "**B**1" + jraw;
		}

		if (action == "remove") {
			if (datas.size() < 4)
				return skip;
			string name = datas[3];

			Object jb;
			jb.push_back(Pair("n", name));

			string jraw = json_spirit::write_string(Value(jb), false);
			notary += "**B**2" + jraw;
		}
	}

	if (scope == "merkle") {
		if (datas.size() < 3)
			return skip;

		string action = datas[2];
		if (action == "add") {
			if (datas.size() < 6)
				return skip;

			string brname = datas[3];
			string merkle = datas[4];
			string amount = datas[5];

			string brhash;
			{
				CDataStream ss(SER_GETHASH, 0);
				ss << brname;
				brhash = Hash(ss.begin(), ss.end()).GetHex();
			}
			notary += "**M**" + brhash + "0x" + merkle + ":" + amount;
		}
	}

	if (scope == "timelockpass") {
		if (datas.size() < 3)
			return skip;

		string action = datas[2];
		if (action == "add") {
			if (datas.size() < 4)
				return skip;
			string pubkey_txt = datas[3];
			notary += "**X**1" + pubkey_txt;
		}
		if (action == "remove") {
			if (datas.size() < 4)
				return skip;
			string pubkey_txt = datas[3];
			notary += "**X**2" + pubkey_txt;
		}
	}

	if (scope == "onbehalf") {
		if (datas.size() != 7)
			return skip;

		string action = datas[2];
		if (action == "add") {
			string                pubkeys_txt = datas[3];
			vector<unsigned char> propdata    = ParseHex(datas[4]);
			string                prop        = string(propdata.begin(), propdata.end());
			string                nonce       = datas[5];
			string                sighex      = datas[6];
			vector<string>        pubkeys;
			boost::split(pubkeys, pubkeys_txt, boost::is_any_of(";"));
			if (pubkeys.size() > 0) {
				Object jobj;
				Array  jkeys;
				for (const string& pubk : pubkeys)
					jkeys.push_back(pubk);
				jobj.push_back(Pair("k", jkeys));
				jobj.push_back(Pair("m", prop));
				jobj.push_back(Pair("n", atoi(nonce)));
				jobj.push_back(Pair("s", sighex));
				string jraw = json_spirit::write_string(Value(jobj), false);
				notary += "**S**" + jraw;
			}
		}
	}

	return notary;
}

vector<string> NotaryToProposal(string                      notary,
								map<string, vector<string>> bridges,
								string&                     phash,
								string&                     address_override) {
	vector<string> skip;
	vector<string> pdatas;

	if (boost::starts_with(notary, "**T**")) {
		string data1 = notary.substr(5);
		if (data1.substr(0, 1) == "1" || data1.substr(0, 1) == "2") {      // tstakers1|2
			if (data1.substr(1, 1) == "1" || data1.substr(1, 1) == "2") {  // tstakers1|2 add|remove
				string scope = "tstakers1";
				if (data1.substr(0, 1) == "2")
					scope = "tstakers2";
				string action = "add";
				if (data1.substr(1, 1) == "2")
					action = "remove";
				string address_txt = data1.substr(2);
				string until       = "0";  // as skip

				CBitcoinAddress address(address_txt);
				if (!address.IsValid())
					return skip;

				{
					CDataStream ss(SER_GETHASH, 0);
					ss << scope;
					ss << action;
					ss << address_txt;
					phash = Hash(ss.begin(), ss.end()).GetHex();
				}

				pdatas.clear();
				pdatas.push_back(scope);
				pdatas.push_back(until);
				pdatas.push_back(action);
				pdatas.push_back(address_txt);
			}
		}
	}

	if (boost::starts_with(notary, "**N**")) {
		string scope            = "consensus";
		string data1            = notary.substr(5);
		string consensus_type_t = data1.substr(0, 1);
		string vb               = data1.substr(1, 1);
		string ve               = data1.substr(data1.length() - 1, 1);
		if (vb == "[" && ve == "]") {
			string         votes_txt = data1.substr(2, data1.length() - 3);
			vector<string> votes;
			boost::split(votes, votes_txt, boost::is_any_of(","));
			if (votes.size() == 3) {
				int t1_votes = std::atoi(votes[0].c_str());
				int t2_votes = std::atoi(votes[1].c_str());
				int o_votes  = std::atoi(votes[2].c_str());

				bool ok1 = all_of(votes[0].begin(), votes[0].end(), ::isdigit);
				bool ok2 = all_of(votes[1].begin(), votes[1].end(), ::isdigit);
				bool ok3 = all_of(votes[2].begin(), votes[2].end(), ::isdigit);

				if (ok1 && ok2 && ok3) {
					if (consensus_type_t == "T" || consensus_type_t == "N" ||
						consensus_type_t == "B" || consensus_type_t == "M" ||
						consensus_type_t == "X") {
						// TODO check ranges and diff

						string until = "0";  // as skip
						string consensus_type;
						if (consensus_type_t == "T")
							consensus_type = "tstakers";
						if (consensus_type_t == "N")
							consensus_type = "consensus";
						if (consensus_type_t == "B")
							consensus_type = "bridge";
						if (consensus_type_t == "M")
							consensus_type = "merkle";
						if (consensus_type_t == "X")
							consensus_type = "timelockpass";

						{
							CDataStream ss(SER_GETHASH, 0);
							ss << scope;
							ss << consensus_type;
							ss << t1_votes;
							ss << t2_votes;
							ss << o_votes;
							phash = Hash(ss.begin(), ss.end()).GetHex();
						}

						pdatas.clear();
						pdatas.push_back(scope);
						pdatas.push_back(until);
						pdatas.push_back(consensus_type);
						pdatas.push_back(votes[0]);
						pdatas.push_back(votes[1]);
						pdatas.push_back(votes[2]);
					}
				}
			}
		}
	}

	if (boost::starts_with(notary, "**B**")) {
		string data1 = notary.substr(5);
		if (data1.substr(0, 1) == "1" || data1.substr(0, 1) == "2") {  // add|remove
			string action = "add";
			if (data1.substr(0, 1) == "2")
				action = "remove";

			string json_txt = data1.substr(1);

			if (action == "add") {
				try {
					json_spirit::Value jval;
					json_spirit::read_string(json_txt, jval);
					const json_spirit::Object& jobj = jval.get_obj();
					if (!jobj.empty()) {
						string             scope  = "bridge";
						string             until  = "0";  // as skip
						string             name   = json_spirit::find_value(jobj, "n").get_str();
						string             symb   = json_spirit::find_value(jobj, "s").get_str();
						json_spirit::Array jlinks = json_spirit::find_value(jobj, "l").get_array();
						int                chain_id = json_spirit::find_value(jobj, "i").get_int();
						string             contract = json_spirit::find_value(jobj, "c").get_str();
						int                pegsteps = json_spirit::find_value(jobj, "p").get_int();
						int         microsteps      = json_spirit::find_value(jobj, "m").get_int();
						set<string> urls;

						{
							CDataStream ss(SER_GETHASH, 0);
							ss << scope;
							ss << action;
							ss << name;
							ss << symb;
							for (const auto& jlink : jlinks) {
								string url = jlink.get_str();
								ss << url;
								urls.insert(url);
							}
							ss << chain_id;
							ss << contract;
							ss << pegsteps;
							ss << microsteps;
							phash = Hash(ss.begin(), ss.end()).GetHex();
						}

						pdatas.clear();
						pdatas.push_back(scope);
						pdatas.push_back(until);
						pdatas.push_back(action);
						pdatas.push_back(name);
						pdatas.push_back(symb);
						pdatas.push_back(boost::algorithm::join(urls, ";"));
						pdatas.push_back(std::to_string(chain_id));
						pdatas.push_back(contract);
						pdatas.push_back(std::to_string(pegsteps));
						pdatas.push_back(std::to_string(microsteps));
					}
				} catch (std::exception& e) {
					// skip (exception json)
				}
			}

			if (action == "remove") {
				try {
					json_spirit::Value jval;
					json_spirit::read_string(json_txt, jval);
					const json_spirit::Object& jobj = jval.get_obj();
					if (!jobj.empty()) {
						string scope = "bridge";
						string until = "0";  // as skip
						string name  = json_spirit::find_value(jobj, "n").get_str();

						{
							CDataStream ss(SER_GETHASH, 0);
							ss << scope;
							ss << action;
							ss << name;
							phash = Hash(ss.begin(), ss.end()).GetHex();
						}

						pdatas.clear();
						pdatas.push_back(scope);
						pdatas.push_back(until);
						pdatas.push_back(action);
						pdatas.push_back(name);
					}
				} catch (std::exception& e) {
					// skip (exception json)
				}
			}
		}
	}

	if (boost::starts_with(notary, "**M**")) {
		string action = "add";
		string mdata  = notary.substr(5);
		if (mdata.size() >= (64 + 66 + 1 + 1)) {
			string  brhash     = mdata.substr(0, 64);
			string  merkle0x   = mdata.substr(64, 66);
			string  sep        = mdata.substr(64 + 66, 1);
			string  amount_txt = mdata.substr(64 + 66 + 1, -1);
			int64_t amount     = strtoll(amount_txt.c_str(), 0, 0);
			if (boost::starts_with(merkle0x, "0x") && IsHex(merkle0x.substr(2)) && sep == ":" &&
				amount > 0) {
				string merkle = merkle0x.substr(2);
				string scope  = "merkle";
				string until  = "0";  // as skip
				string brname;
				for (const auto& it : bridges) {
					string brname_it = it.first;
					string brhash_it;
					{
						CDataStream ss(SER_GETHASH, 0);
						ss << brname_it;
						brhash_it = Hash(ss.begin(), ss.end()).GetHex();
					}
					if (brhash_it == brhash) {
						brname = brname_it;
					}
				}

				if (!brname.empty()) {
					{
						CDataStream ss(SER_GETHASH, 0);
						ss << scope;
						ss << action;
						ss << brname;
						ss << merkle;
						ss << amount;
						phash = Hash(ss.begin(), ss.end()).GetHex();
					}

					pdatas.clear();
					pdatas.push_back(scope);
					pdatas.push_back(until);
					pdatas.push_back(action);
					pdatas.push_back(brname);
					pdatas.push_back(merkle);
					pdatas.push_back(amount_txt);
				}
			}
		}
	}

	if (boost::starts_with(notary, "**X**")) {
		string data1 = notary.substr(5);
		if (data1.substr(0, 1) == "1" || data1.substr(0, 1) == "2") {  // add|remove
			string action = "add";
			if (data1.substr(0, 1) == "2")
				action = "remove";
			string                     pubkey_txt = data1.substr(1);
			std::vector<unsigned char> pubkey_vch = ParseHex(pubkey_txt);
			CPubKey                    pubkey(pubkey_vch);
			if (pubkey.IsValid()) {
				string scope = "timelockpass";
				string until = "0";  // as skip

				// TODO: findout pubkeys
				// map<string, vector<string>> bridges;
				// if (!pindex->ReadBridgesMap(pegdb, bridges))
				// 	return false;

				{
					CDataStream ss(SER_GETHASH, 0);
					ss << scope;
					ss << action;
					ss << pubkey_txt;
					phash = Hash(ss.begin(), ss.end()).GetHex();
				}

				pdatas.clear();
				pdatas.push_back(scope);
				pdatas.push_back(until);
				pdatas.push_back(action);
				pdatas.push_back(pubkey_txt);
			}
		}
	}

	if (boost::starts_with(notary, "**S**")) {
		string data1    = notary.substr(5);
		string action   = "add";
		string json_txt = data1;

		string         sighex;
		string         proposal;
		int64_t        nonce;
		vector<string> pubkeys;
		string         pub1hex;

		try {
			json_spirit::Value jval;
			json_spirit::read_string(json_txt, jval);
			const json_spirit::Object& jobj = jval.get_obj();
			if (!jobj.empty()) {
				Array pubs = json_spirit::find_value(jobj, "k").get_array();
				sighex     = json_spirit::find_value(jobj, "s").get_str();
				proposal   = json_spirit::find_value(jobj, "m").get_str();
				nonce      = json_spirit::find_value(jobj, "n").get_int64();
				if (!pubs.empty())
					pub1hex = pubs[0].get_str();
				for (const Value& pub : pubs)
					pubkeys.push_back(pub.get_str());
			}
		} catch (std::exception& e) {
			// skip (exception json)
		}

		if (!pub1hex.empty()) {
			std::vector<unsigned char> pubkey_vch = ParseHex(pub1hex);
			CPubKey                    cpubkey(pubkey_vch);
			if (cpubkey.IsValid()) {
				CDataStream ss(SER_GETHASH, 0);
				ss << proposal;
				ss << std::to_string(nonce);
				std::vector<unsigned char> vchSig = ParseHex(sighex);
				if (cpubkey.VerifyCompact(Hash(ss.begin(), ss.end()), vchSig)) {
					// sig is OK,
					// override address
					address_override = "invalid";  // default to be overwritten
					if (pubkeys.size() == 1) {
						std::vector<unsigned char> pubkey_vch = ParseHex(pubkeys[0]);
						CPubKey                    cpubkey(pubkey_vch);
						if (cpubkey.IsValid()) {
							CKeyID keyID     = cpubkey.GetID();
							address_override = CBitcoinAddress(keyID).ToString();
						}
					}
					if (pubkeys.size() == 2) {
						bool                 valid = true;
						std::vector<CPubKey> cpubkeys;
						for (const string& pubkey : pubkeys) {
							std::vector<unsigned char> pubkey_vch = ParseHex(pubkey);
							CPubKey                    cpubkey(pubkey_vch);
							cpubkeys.push_back(cpubkey);
							if (!cpubkey.IsValid())
								valid = false;
						}
						if (valid) {
							CScript msigscript;
							msigscript.SetMultisig(2, cpubkeys);
							CScriptID mscriptID = msigscript.GetID();
							address_override    = CBitcoinAddress(mscriptID).ToString();
						}
					}
					// return parsed message/notary
					string address_override_skip;
					return NotaryToProposal(proposal, bridges, phash, address_override_skip);
				}
			}
		}
	}

	return pdatas;
}
