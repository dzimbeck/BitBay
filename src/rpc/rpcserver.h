// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BITCOINRPC_SERVER_H_
#define _BITCOINRPC_SERVER_H_ 1

#include "rpcprotocol.h"
#include "uint256.h"

#include <list>
#include <map>

class CBlockIndex;

void StartRPCThreads();
void StopRPCThreads();

/*
  Type-check arguments; throws JSONRPCError if wrong type given. Does not check that
  the right number of arguments are passed, just that any passed are the correct type.
  Use like:  RPCTypeCheck(params, boost::assign::list_of(str_type)(int_type)(obj_type));
*/
void RPCTypeCheck(const json_spirit::Array&                 params,
                  const std::list<json_spirit::Value_type>& typesExpected,
                  bool                                      fAllowNull = false);
/*
  Check for expected keys/value types in an Object.
  Use like: RPCTypeCheck(object, boost::assign::map_list_of("name", str_type)("value", int_type));
*/
void RPCTypeCheck(const json_spirit::Object&                            o,
                  const std::map<std::string, json_spirit::Value_type>& typesExpected,
                  bool                                                  fAllowNull = false);

/*
  Run func nSeconds from now. Uses boost deadline timers.
  Overrides previous timer <name> (if any).
 */
void RPCRunLater(const std::string& name, boost::function<void(void)> func, int64_t nSeconds);

typedef json_spirit::Value (*rpcfn_type)(const json_spirit::Array& params, bool fHelp);

class CRPCCommand {
public:
	std::string name;
	rpcfn_type  actor;
	bool        okSafeMode;
	bool        threadSafe;
	bool        reqWallet;
};

/**
 * Bitcoin RPC command dispatcher.
 */
class CRPCTable {
private:
	std::map<std::string, const CRPCCommand*> mapCommands;

public:
	CRPCTable();
	const CRPCCommand* operator[](std::string name) const;
	std::string        help(std::string name) const;

	/**
	 * Execute a method.
	 * @param method   Method to execute
	 * @param params   Array of arguments (JSON objects)
	 * @returns Result of the call.
	 * @throws an exception (json_spirit::Value) when an error happens.
	 */
	json_spirit::Value execute(const std::string& method, const json_spirit::Array& params) const;
};

extern const CRPCTable tableRPC;

extern void InitRPCMining();
extern void ShutdownRPCMining();

extern int64_t            nWalletUnlockTime;
extern int64_t            AmountFromValue(const json_spirit::Value& value);
extern json_spirit::Value ValueFromAmount(int64_t amount);
extern double             GetDifficulty(const CBlockIndex* blockindex = NULL);

extern double GetPoWMHashPS();
extern double GetPoSKernelPS();

extern std::string HelpRequiringPassphrase();
extern void        EnsureWalletIsUnlocked();

//
// Utilities: convert hex-encoded Values
// (throws error if not hex).
//
extern uint256                    ParseHashV(const json_spirit::Value& v, std::string strName);
extern uint256                    ParseHashO(const json_spirit::Object& o, std::string strKey);
extern std::vector<unsigned char> ParseHexV(const json_spirit::Value& v, std::string strName);
extern std::vector<unsigned char> ParseHexO(const json_spirit::Object& o, std::string strKey);

extern json_spirit::Value getconnectioncount(const json_spirit::Array& params,
                                             bool                      fHelp);  // in rpcnet.cpp
extern json_spirit::Value getpeerinfo(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value ping(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value addnode(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getaddednodeinfo(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getnettotals(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value dumpwallet(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value importwallet(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value dumpprivkey(const json_spirit::Array& params,
                                      bool                      fHelp);  // in rpcdump.cpp
extern json_spirit::Value importprivkey(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value importaddress(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value sendalert(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value getsubsidy(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getstakesubsidy(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getmininginfo(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getstakinginfo(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value checkkernel(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getwork(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getworkex(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getblocktemplate(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value submitblock(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value getnewaddress(const json_spirit::Array& params,
                                        bool                      fHelp);  // in rpcwallet.cpp
extern json_spirit::Value getaccountaddress(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value setaccount(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getaccount(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getaddressesbyaccount(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value sendtoaddress(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value sendliquid(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value sendreserve(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value signmessage(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value verifymessage(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getreceivedbyaddress(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getreceivedbyaccount(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getbalance(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value sendfrom(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value sendmany(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value addmultisigaddress(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value addredeemscript(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listreceivedbyaddress(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listreceivedbyaccount(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listtransactions(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listbridgetransactions(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listaddressgroupings(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listaccounts(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listsinceblock(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value gettransaction(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value gettxout(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value backupwallet(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value keypoolrefill(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value walletpassphrase(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value walletpassphrasechange(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value walletlock(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value encryptwallet(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value validateaddress(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getinfo(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value reservebalance(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value checkwallet(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value repairwallet(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value resendtx(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value makekeypair(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value validatepubkey(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getnewpubkey(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value createbootstrap(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listunspent(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listunspent1(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listfrozen(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listfrozen1(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value liststaked(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value balance(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value balancerecords(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value getrawtransaction(const json_spirit::Array& params,
                                            bool fHelp);  // in rcprawtransaction.cpp
extern json_spirit::Value listunspent2(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listfrozen2(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value liststaked2(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value createrawtransaction(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value decoderawtransaction(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value decodescript(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value signrawtransaction(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value sendrawtransaction(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value getbestblockhash(const json_spirit::Array& params,
                                           bool fHelp);  // in rpcblockchain.cpp
extern json_spirit::Value getblockcount(const json_spirit::Array& params,
                                        bool                      fHelp);  // in rpcblockchain.cpp
extern json_spirit::Value getdifficulty(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value settxfee(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getrawmempool(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getblockhash(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getblock(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getblockbynumber(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getcheckpoint(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value getpeginfo(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getfractions(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getfractionsbase64(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getliquidityrate(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value validaterawtransaction(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value getpeglevel(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value makepeglevel(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value listdeposits(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value registerdeposit(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value updatetxout(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value updatepegbalances(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value movecoins(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value moveliquid(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value movereserve(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value removecoins(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value prepareliquidwithdraw(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value preparereservewithdraw(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value checkwithdrawstate(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value accountmaintenance(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value faucet(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value tstakers1(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value tstakers2(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value consensus(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value proposals(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value myproposals(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value addproposal(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value signproposal(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value voteproposal(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value removeproposal(const json_spirit::Array& params, bool fHelp);

extern json_spirit::Value bridges(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value bridgereceipt(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value bridgeautomate(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value merklesin(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value merklesout(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value getbridgepool(const json_spirit::Array& params, bool fHelp);
extern json_spirit::Value timelockpasses(const json_spirit::Array& params, bool fHelp);

#endif
