#include <boost/test/unit_test.hpp>

// #include <ethc/abi.h>
// #include <ethc/hex.h>
// #include <ethc/keccak256.h>

// #include "pegdata.h"
// #include "peg.h"

// #define ok(ethcop) BOOST_CHECK(ethcop >= 0)

#include "base58.h"
#include "chainparams.h"
#include "script.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(mintser_tests)

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

BOOST_AUTO_TEST_CASE(ser1_r3) {
	// 6f3274755ab76be859e1d862b16aa2327b5a890e9b
	// 38 2222386834 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 654549167 622913094
	// 592827502 564216403 537007534 511132170 0 0 1 section 11 nonce
	// 83fc607b88486b8607e35cb997b1c67776efaadf from
	// c5f465164bf9dea6c91ccba519ce8fbd15845b338c681a78707868a06b544c00 leaf
	// 0 proof size

	CBitcoinAddress addr_dest;
	string          addr_dest_hex = "6f3274755ab76be859e1d862b16aa2327b5a890e9b";
	vector<int64_t> sections;
	int             section_peg = 1;
	int             nonce       = 11;
	string          from        = "83fc607b88486b8607e35cb997b1c67776efaadf";
	string          leaf = "c5f465164bf9dea6c91ccba519ce8fbd15845b338c681a78707868a06b544c00";
	vector<string>  proofs;

	string scriptSigHexInp =
	    "156f3274755ab76be859e1d862b16aa2327b5a890e9b01260592ee7684010000000000000000000000000000"
	    "00000000000000000000000000000004afa003270446e62025046ed45523045342a12104ae150220040a42771e"
	    "0000515b1483fc607b88486b8607e35cb997b1c67776efaadf20c5f465164bf9dea6c91ccba519ce8fbd15845b"
	    "338c681a78707868a06b544c0000";
	vector<unsigned char> scriptSigData = ParseHex(scriptSigHexInp);
	CScript               scriptSig(scriptSigData.begin(), scriptSigData.end());
	std::cout << "Mint script decoded: " << scriptSig.ToString() << std::endl;

	{
		int section_idx   = 0;
		int sections_size = 0;
		int proofs_size   = 0;
		int proof_idx     = 0;

		int                     idx = 0;
		opcodetype              opcode;
		vector<unsigned char>   vch;
		CScript::const_iterator pc = scriptSig.begin();
		while (pc < scriptSig.end()) {
			if (!scriptSig.GetOp(pc, opcode, vch)) {
				BOOST_CHECK(false);
				return;
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
					section_idx++;
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
					proof_idx++;
				} else {
					std::string str = ValueString(vch);
				}
			} else {
				BOOST_CHECK(false);
				return;
			}
			idx++;
		}
	}

	{
		// std::string sections_json_txt =
		//     "[2222386834, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		//     " "0, 0, 0, 0, 0, 654549167, 622913094, 592827502, 564216403, 537007534, 511132170,0,
		//     0]";
		// json_spirit::Value jval;
		// json_spirit::read_string(sections_json_txt, jval);
		// const json_spirit::Array& jarray = jval.get_array();

		// string          addr_dest_hex = "6f3274755ab76be859e1d862b16aa2327b5a890e9b";
		// vector<int64_t> sections;
		// for (const auto& jf : jarray) {
		// 	int64_t f = jf.get_int64();
		// 	sections.push_back(f);
		// }
		// int            section_peg = 1;
		// int            nonce       = 11;
		// string         from        = "83fc607b88486b8607e35cb997b1c67776efaadf";
		// string         leaf = "c5f465164bf9dea6c91ccba519ce8fbd15845b338c681a78707868a06b544c00";
		// vector<string> proofs;

		CScript proofSig;
		{
			proofSig << ParseHex(addr_dest_hex);
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
		string proofSigHex;
		{
			CDataStream ss(SER_DISK, 0);
			ss << proofSig;
			proofSigHex = HexStr(ss.begin(), ss.end());
		}
		string scriptSigHex;
		{
			CDataStream ss(SER_DISK, 0);
			ss << scriptSig;
			scriptSigHex = HexStr(ss.begin(), ss.end());
		}
		// std::cout << "Mint script serialized proof  : " << proofSigHex << std::endl;
		// std::cout << "Mint script serialized decoded: " << scriptSigHex << std::endl;
		// std::cout << "Mint script serialized input  : " << scriptSigHexInp << std::endl;
		BOOST_CHECK(proofSig == scriptSig);
		BOOST_CHECK(proofSigHex == scriptSigHex);
		BOOST_CHECK(scriptSigHex == "94" + scriptSigHexInp);
	}
}

// 6f3274755ab76be859e1d862b16aa2327b5a890e9b 38 6517354130 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
// 0 0 0 0 0 0 0 0 0 654549167 622913094 592827502 564216403 537007534 511132170 0 0 1 11
// 83fc607b88486b8607e35cb997b1c67776efaadf
// c5f465164bf9dea6c91ccba519ce8fbd15845b338c681a78707868a06b544c00 0
// 6f3274755ab76be859e1d862b16aa2327b5a890e9b 38 2222386834 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
// 0 0 0 0 0 0 0 0 0 654549167 622913094 592827502 564216403 537007534 511132170 0 0 1 11
// 83fc607b88486b8607e35cb997b1c67776efaadf
// c5f465164bf9dea6c91ccba519ce8fbd15845b338c681a78707868a06b544c00 0

// 94156f3274755ab76be859e1d862b16aa2327b5a890e9b01260592ee768401000000000000000000000000000000000000000000000000000000000004afa003270446e62025046ed45523045342a12104ae150220040a42771e0000000014
//     83fc607b88486b8607e35cb997b1c67776efaadf20
//     c5f465164bf9dea6c91ccba519ce8fbd15845b338c681a78707868a06b544c0000

// 94156f3274755ab76be859e1d862b16aa2327b5a890e9b01260592ee768401000000000000000000000000000000000000000000000000000000000004afa003270446e62025046ed45523045342a12104ae150220040a42771e0000515b14
//     83fc607b88486b8607e35cb997b1c67776efaadf20
//     c5f465164bf9dea6c91ccba519ce8fbd15845b338c681a78707868a06b544c0000

BOOST_AUTO_TEST_SUITE_END()
