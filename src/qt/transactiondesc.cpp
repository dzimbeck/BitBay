
#include <string>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>

#include "transactiondesc.h"

#include "bitcoinunits.h"
#include "guiutil.h"

#include "base58.h"
#include "main.h"
#include "paymentserver.h"
#include "timedata.h"
#include "transactionrecord.h"
#include "txdb.h"
#include "ui_interface.h"
#include "wallet.h"

using namespace json_spirit;

QString TransactionDesc::FormatTxStatus(const CWalletTx& wtx) {
	AssertLockHeld(cs_main);
	if (!IsFinalTx(wtx, nBestHeight + 1)) {
		if (wtx.nLockTime < LOCKTIME_THRESHOLD)
			return tr("Open for %n more block(s)", "", wtx.nLockTime - nBestHeight);
		else
			return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.nLockTime));
	} else {
		int nDepth = wtx.GetDepthInMainChain();
		if (nDepth < 0)
			return tr("conflicted");
		else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
			return tr("%1/offline").arg(nDepth);
		else if (nDepth < 10)
			return tr("%1/unconfirmed").arg(nDepth);
		else
			return tr("%1 confirmations").arg(nDepth);
	}
}

QString TransactionDesc::toHTML(CWallet* wallet, CWalletTx& wtx, TransactionRecord* rec, int unit) {
	QString strHTML;

	LOCK2(cs_main, wallet->cs_wallet);
	strHTML.reserve(4000);
	strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

	int64_t nTime   = wtx.GetTxTime();
	int64_t nCredit = wtx.GetCredit();
	int64_t nDebit  = wtx.GetDebit();
	int64_t nNet    = nCredit - nDebit;

	strHTML += "<b>" + tr("Status") + ":</b> " + FormatTxStatus(wtx);
	int nRequests = wtx.GetRequestCount();
	if (nRequests != -1) {
		if (nRequests == 0)
			strHTML += tr(", has not been successfully broadcast yet");
		else if (nRequests > 0)
			strHTML += tr(", broadcast through %n node(s)", "", nRequests);
	}
	strHTML += "<br>";

	strHTML += "<b>" + tr("Date") + ":</b> " + (nTime ? GUIUtil::dateTimeStr(nTime) : "") + "<br>";

	//
	// From
	//
	if (wtx.IsCoinBase() || wtx.IsCoinStake()) {
		strHTML += "<b>" + tr("Source") + ":</b> " + tr("Generated") + "<br>";
	} else if (wtx.mapValue.count("from") && !wtx.mapValue["from"].empty()) {
		// Online transaction
		strHTML +=
			"<b>" + tr("From") + ":</b> " + GUIUtil::HtmlEscape(wtx.mapValue["from"]) + "<br>";
	} else {
		// Offline transaction
		if (nNet > 0) {
			// Credit
			if (CBitcoinAddress(rec->address).IsValid()) {
				CTxDestination address = CBitcoinAddress(rec->address).Get();
				if (wallet->mapAddressBook.count(address)) {
					strHTML += "<b>" + tr("From") + ":</b> " + tr("unknown") + "<br>";
					strHTML += "<b>" + tr("To") + ":</b> ";
					strHTML += GUIUtil::HtmlEscape(rec->address);
					if (!wallet->mapAddressBook[address].empty())
						strHTML += " (" + tr("own address") + ", " + tr("label") + ": " +
								   GUIUtil::HtmlEscape(wallet->mapAddressBook[address]) + ")";
					else
						strHTML += " (" + tr("own address") + ")";
					strHTML += "<br>";
				}
			}
		}
	}

	bool is_to_ext = false;

	//
	// To
	//
	if (wtx.mapValue.count("to") && !wtx.mapValue["to"].empty()) {
		// Online transaction
		std::string strAddress = wtx.mapValue["to"];
		if (boost::starts_with(strAddress, "EXT:"))
			is_to_ext = true;
		strHTML += "<b>" + tr("To") + ":</b> ";
		CTxDestination dest = CBitcoinAddress(strAddress).Get();
		if (wallet->mapAddressBook.count(dest) && !wallet->mapAddressBook[dest].empty())
			strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[dest]) + " ";
		strHTML += GUIUtil::HtmlEscape(strAddress) + "<br>";
	}

	//
	// Amount
	//
	if (wtx.IsCoinBase() && nCredit == 0) {
		//
		// Coinbase
		//
		int64_t nUnmatured = 0;
		for (const CTxOut& txout : wtx.vout) {
			nUnmatured += wallet->GetCredit(txout);
		}
		strHTML += "<b>" + tr("Credit") + ":</b> ";
		if (wtx.IsInMainChain())
			strHTML += BitcoinUnits::formatWithUnit(unit, nUnmatured) + " (" +
					   tr("matures in %n more block(s)", "", wtx.GetBlocksToMaturity()) + ")";
		else
			strHTML += "(" + tr("not accepted") + ")";
		strHTML += "<br>";
	} else if (nNet > 0) {
		//
		// Credit
		//
		strHTML +=
			"<b>" + tr("Credit") + ":</b> " + BitcoinUnits::formatWithUnit(unit, nNet) + "<br>";
	} else {
		bool fAllFromMe = true;
		for (const CTxIn& txin : wtx.vin) {
			fAllFromMe = fAllFromMe && wallet->IsMine(txin);
		}

		bool fAllToMe = true;
		for (const CTxOut& txout : wtx.vout) {
			fAllToMe = fAllToMe && wallet->IsMine(txout);
		}

		if (fAllFromMe) {
			//
			// Debit
			//
			for (const CTxOut& txout : wtx.vout) {
				if (wallet->IsMine(txout))
					continue;

				if (!wtx.mapValue.count("to") || wtx.mapValue["to"].empty()) {
					// Offline transaction
					CTxDestination address;
					if (ExtractDestination(txout.scriptPubKey, address)) {
						strHTML += "<b>" + tr("To") + ":</b> ";
						if (wallet->mapAddressBook.count(address) &&
							!wallet->mapAddressBook[address].empty())
							strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address]) + " ";
						string strAddress = CBitcoinAddress(address).ToString();
						if (boost::starts_with(strAddress, "EXT:"))
							is_to_ext = true;
						strHTML += GUIUtil::HtmlEscape(strAddress);
						strHTML += "<br>";
					}
				}

				strHTML += "<b>" + tr("Debit") + ":</b> " +
						   BitcoinUnits::formatWithUnit(unit, -txout.nValue) + "<br>";
			}

			if (fAllToMe) {
				// Payment to self
				int64_t nChange = wtx.GetChange();
				int64_t nValue  = nCredit - nChange;
				strHTML += "<b>" + tr("Debit") + ":</b> " +
						   BitcoinUnits::formatWithUnit(unit, -nValue) + "<br>";
				strHTML += "<b>" + tr("Credit") + ":</b> " +
						   BitcoinUnits::formatWithUnit(unit, nValue) + "<br>";
			}

			int64_t nTxFee = nDebit - wtx.GetValueOut();
			if (nTxFee > 0)
				strHTML += "<b>" + tr("Transaction fee") + ":</b> " +
						   BitcoinUnits::formatWithUnit(unit, -nTxFee) + "<br>";
		} else {
			//
			// Mixed debit transaction
			//
			for (const CTxIn& txin : wtx.vin) {
				if (wallet->IsMine(txin))
					strHTML += "<b>" + tr("Debit") + ":</b> " +
							   BitcoinUnits::formatWithUnit(unit, -wallet->GetDebit(txin)) + "<br>";
			}
			for (const CTxOut& txout : wtx.vout) {
				if (wallet->IsMine(txout))
					strHTML += "<b>" + tr("Credit") + ":</b> " +
							   BitcoinUnits::formatWithUnit(unit, wallet->GetCredit(txout)) +
							   "<br>";
			}
		}
	}

	strHTML += "<b>" + tr("Net amount") + ":</b> " +
			   BitcoinUnits::formatWithUnit(unit, nNet, true) + "<br>";

	//
	// Message
	//
	if (wtx.mapValue.count("message") && !wtx.mapValue["message"].empty())
		strHTML += "<br><b>" + tr("Message") + ":</b><br>" +
				   GUIUtil::HtmlEscape(wtx.mapValue["message"], true) + "<br>";
	if (wtx.mapValue.count("comment") && !wtx.mapValue["comment"].empty())
		strHTML += "<br><b>" + tr("Comment") + ":</b><br>" +
				   GUIUtil::HtmlEscape(wtx.mapValue["comment"], true) + "<br>";

	strHTML += "<b>" + tr("Transaction ID") + ":</b> " +
			   QString::fromStdString(wtx.GetHash().ToString()) + "<br>";

	if (wtx.IsCoinBase() || wtx.IsCoinStake())
		strHTML += "<br>" +
				   tr("Generated coins must mature 510 blocks before they can be spent. When you "
					  "generated this block, it was broadcast to the network to be added to the "
					  "block chain. If it fails to get into the chain, its state will change to "
					  "\"not accepted\" and it won't be spendable. This may occasionally happen if "
					  "another node generates a block within a few seconds of yours.") +
				   "<br>";

	if (is_to_ext) {  // receipt

		uint256             bhash_bridge_cycle;
		map<string, string> brhash_to_merkle;
		{
			uint    nTxNum = 0;
			uint256 blockhash;
			uint256 txhash = wtx.GetHash();
			{
				LOCK(cs_main);
				CTxDB    txdb("r");
				CPegDB   pegdb("r");
				CTxIndex txindex;
				if (txdb.ReadTxIndex(txhash, txindex)) {
					txindex.GetHeightInMainChain(&nTxNum, txhash, &blockhash);
					if (mapBlockIndex.find(blockhash) != mapBlockIndex.end()) {
						CBlockIndex* pblockindex = mapBlockIndex.ref(blockhash);
						if (pblockindex) {
							CBlockIndex* blockcycle = pblockindex->BridgeCycleBlock();
							bhash_bridge_cycle      = blockcycle->GetBlockHash();
							set<string> bridge_hashes;
							pegdb.ReadBridgeCycleBridgeHashes(bhash_bridge_cycle, bridge_hashes);

							for (const string& brhash : bridge_hashes) {
								string merkle_data;
								pegdb.ReadBridgeCycleMerkle(bhash_bridge_cycle, brhash,
															merkle_data);

								string         merkle;
								vector<string> args;
								boost::split(args, merkle_data, boost::is_any_of(":"));
								if (args.size() == 2) {
									merkle                   = args[0];
									brhash_to_merkle[brhash] = merkle;
								}
							}
						}
					}
				}
			}
		}

		string receipt_txt;

		int nout = 0;
		for (const CTxOut& txout : wtx.vout) {
			if (!wallet->IsMine(txout)) {
				if (!wtx.mapValue.count("to") || wtx.mapValue["to"].empty()) {
					CTxDestination address;
					if (ExtractDestination(txout.scriptPubKey, address)) {
						string strAddress = CBitcoinAddress(address).ToString();
						if (boost::starts_with(strAddress, "EXT:")) {
							vector<string> strAddress_args;
							boost::split(strAddress_args, strAddress, boost::is_any_of(":"));
							if (strAddress_args.size() == 3) {
								string brhash  = strAddress_args[1];
								string address = strAddress_args[2];
								string merkle  = brhash_to_merkle[brhash];

								Object receipt;

								string txid    = wtx.GetHash().ToString();
								string txoutid = txid + ":" + std::to_string(nout);
								receipt.push_back(Pair("txid", txoutid));
								receipt.push_back(Pair("address", address));
								receipt.push_back(Pair("root", "0x" + merkle));

								Array reserve;
								Array proof;

								LOCK(cs_main);
								CPegDB pegdb("r");

								bool        is_receipt_ready = false;
								set<string> burns_data;
								pegdb.ReadBridgeCycleBurnsToBridge(bhash_bridge_cycle, brhash,
																   burns_data);
								for (const string& burn_data : burns_data) {
									vector<string> burn_datas;
									boost::split(burn_datas, burn_data, boost::is_any_of(":"));
									if (burn_datas.size() >= 5) {
										// txout:nout
										string b_txid = burn_datas[0];
										string b_nout = burn_datas[1];
										if (b_txid == txid && b_nout == std::to_string(nout)) {
											int b_num_reserve = std::atoi(burn_datas[4].c_str());
											if (burn_datas.size() >=
												size_t(5 + b_num_reserve + 1)) {
												for (int i = 0; i < b_num_reserve; i++) {
													int64_t v = strtoll(burn_datas[5 + i].c_str(),
																		NULL, 10);
													reserve.push_back(v);
													is_receipt_ready = true;
												}
												int b_num_proof = std::atoi(
													burn_datas[5 + b_num_reserve].c_str());
												if (burn_datas.size() ==
													size_t(5 + b_num_reserve + 1 + b_num_proof)) {
													for (int i = 0; i < b_num_proof; i++) {
														string p =
															"0x" +
															burn_datas[5 + b_num_reserve + 1 + i];
														proof.push_back(p);
													}
												}
											}
										}
									}
								}
								receipt.push_back(Pair("reserve", reserve));
								receipt.push_back(Pair("proof", proof));

								receipt_txt = json_spirit::write_string(Value(receipt), false);
								receipt_txt += "<br><br>";

								if (!is_receipt_ready) {
									receipt_txt = "Receipt is not ready yet, wait for next blocks";
									receipt_txt += "<br><br>";
								}
							}
						}
					}
				}
			}
			nout++;
		}

		strHTML += "<b>" + tr("Cross-chain Receipt") + ":</b> " +
				   QString::fromStdString(receipt_txt) + "<br>";
	}

	//
	// Debug view
	//
	if (fDebug) {
		strHTML += "<hr><br>" + tr("Debug information") + "<br><br>";
		for (const CTxIn& txin : wtx.vin) {
			if (wallet->IsMine(txin))
				strHTML += "<b>" + tr("Debit") + ":</b> " +
						   BitcoinUnits::formatWithUnit(unit, -wallet->GetDebit(txin)) + "<br>";
		}
		for (const CTxOut& txout : wtx.vout) {
			if (wallet->IsMine(txout))
				strHTML += "<b>" + tr("Credit") + ":</b> " +
						   BitcoinUnits::formatWithUnit(unit, wallet->GetCredit(txout)) + "<br>";
		}

		strHTML += "<br><b>" + tr("Transaction") + ":</b><br>";
		strHTML += GUIUtil::HtmlEscape(wtx.ToString(), true);

		CTxDB txdb("r");  // To fetch source txouts

		strHTML += "<br><b>" + tr("Inputs") + ":</b>";
		strHTML += "<ul>";

		for (const CTxIn& txin : wtx.vin) {
			COutPoint prevout = txin.prevout;

			CTransaction prev;
			if (txdb.ReadDiskTx(prevout.hash, prev)) {
				if (prevout.n < prev.vout.size()) {
					strHTML += "<li>";
					const CTxOut&  vout = prev.vout[prevout.n];
					CTxDestination address;
					if (ExtractDestination(vout.scriptPubKey, address)) {
						if (wallet->mapAddressBook.count(address) &&
							!wallet->mapAddressBook[address].empty())
							strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address]) + " ";
						strHTML += QString::fromStdString(CBitcoinAddress(address).ToString());
					}
					strHTML = strHTML + " " + tr("Amount") + "=" +
							  BitcoinUnits::formatWithUnit(unit, vout.nValue);
					strHTML = strHTML +
							  " IsMine=" + (wallet->IsMine(vout) ? tr("true") : tr("false")) +
							  "</li>";
				}
			}
		}

		strHTML += "</ul>";
	}

	strHTML += "</font></html>";
	return strHTML;
}
