// Copyright (c) 2024 yshurik
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITBAY_PROPOSALS_H
#define BITBAY_PROPOSALS_H

#include <map>
#include <set>
#include <string>
#include <vector>
#include "chainparams.h"
#include "json/json_spirit_value.h"

json_spirit::Object ProposalToJson(std::vector<std::string>                        datas,
								   std::set<std::string>                           sTrustedStakers1,
								   std::set<std::string>                           sTrustedStakers2,
								   std::map<int, CChainParams::ConsensusVotes>     consensus,
								   std::map<std::string, std::vector<std::string>> bridges,
								   std::function<bool(std::string)>                f_merkle_in_has,
								   bool&                                           active,
								   std::string&                                    status);

std::string ProposalToNotary(std::vector<std::string> datas);

std::vector<std::string> NotaryToProposal(std::string                                     notary,
										  std::map<std::string, std::vector<std::string>> bridges,
										  std::string&                                    phash,
										  std::string& address_override);

#endif
