#include <boost/test/unit_test.hpp>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"

#include <ethc/abi.h>
#include <ethc/hex.h>
#include <ethc/keccak256.h>

//#define HAVE_OPENSSL
//#define MERKLECPP_TRACE_ENABLED 1
#include "merklecpp/merklecpp.h"

#include "pegdata.h"
#include "peg.h"

#define ok(ethcop) BOOST_CHECK(ethcop >= 0)

BOOST_AUTO_TEST_SUITE(merkle_tests)

std::string keccak_string_hash(const std::string& input) {

    uint8_t keccak[32];
    eth_keccak256(keccak, (uint8_t*)input.c_str(), input.length());
    
    char * hex_cstr;
    eth_hex_from_bytes(&hex_cstr, keccak, 32);
        
    std::string hex = "0x"+std::string(hex_cstr);
    free(hex_cstr);

    return hex;
}

static inline bool sha256_keccak_cmp_lt(
    const uint8_t a[32], 
    const uint8_t b[32]) 
{
    for(int i=0;i<32;i++) {
        if (a[i] < b[i]) return true;
        if (a[i] > b[i]) return false;
    }
    return false;
}

static inline void sha256_keccak(
    const merkle::HashT<32>& l,
    const merkle::HashT<32>& r,
    merkle::HashT<32>& out,
    bool & swap)
{
    swap = false;
    uint8_t block[32 * 2];
    if (sha256_keccak_cmp_lt(l.bytes, r.bytes)) {
        memcpy(&block[0], l.bytes, 32);
        memcpy(&block[32], r.bytes, 32);
        
//        char * hex1_cstr;
//        eth_hex_from_bytes(&hex1_cstr, l.bytes, 32);

//        char * hex2_cstr;
//        eth_hex_from_bytes(&hex2_cstr, r.bytes, 32);
        
//        std::cout << "hash order ok " << hex1_cstr << " " << hex2_cstr << std::endl;
        
    } else {
        memcpy(&block[0], r.bytes, 32);
        memcpy(&block[32], l.bytes, 32);
        swap = true;

//        char * hex1_cstr;
//        eth_hex_from_bytes(&hex1_cstr, l.bytes, 32);

//        char * hex2_cstr;
//        eth_hex_from_bytes(&hex2_cstr, r.bytes, 32);
        
//        std::cout << "hash order re " << hex1_cstr << " " << hex2_cstr << std::endl;
    }
    eth_keccak256(out.bytes, block, 32 * 2);
}


BOOST_AUTO_TEST_CASE(merkle_x1)
{
    int x = 10;

    // web3[0].utils.keccak256(web3[0].eth.abi.encodeParameters(['address','uint256['+totalsteps+']','string'],thedata));//[to,array,txid]

    //[0, 2285064261L, 1528644339, 1022619876, 684103824, 457646172, 306152323, 204807256, 95177528, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 505719484, 480934199, 457363637, 434948244, 413631447, 393359383, 374080856, 355747171]

    //    ...BridgeDriver.execute_script("return getLeaf("+str(mydata)+","+myval+");") #mydata is to, reserve, txid and myval is 38(total steps)
    //             (get leaf in javascript driver is...)
    //             web3[0].utils.keccak256(web3[0].eth.abi.encodeParameters(['address','uint256['+totalsteps+']','string'],thedata));//[to,array,txid]
    //
    //Finishing with
    //    Processed Merkle Tree:0x8b8006a6a3db305753991ba2b41f4503cf231122ea1e9bde607c84e28c866605

    // leaf1
    {
        std::string addr = "0x93AAfed0319B064De0acC8233283F293eE6aa8e0";
        std::string txid = "868f1a68b5c405c1aada5bd040d8d291e20ba119c7e81af285a7d90ac14b313f:1";
        std::vector<uint64_t> cfs{0, 2285064261L, 1528644339, 1022619876, 684103824, 457646172, 306152323, 204807256, 95177528, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 505719484, 480934199, 457363637, 434948244, 413631447, 393359383, 374080856, 355747171};
        std::string abi_txt_chk = "0x00000000000000000000000093aafed0319b064de0acc8233283f293ee6aa8e000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000088335045000000000000000000000000000000000000000000000000000000005b1d42f3000000000000000000000000000000000000000000000000000000003cf3f0e40000000000000000000000000000000000000000000000000000000028c69890000000000000000000000000000000000000000000000000000000001b47205c00000000000000000000000000000000000000000000000000000000123f8383000000000000000000000000000000000000000000000000000000000c351c580000000000000000000000000000000000000000000000000000000005ac4b38000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001e24aabc000000000000000000000000000000000000000000000000000000001caa7937000000000000000000000000000000000000000000000000000000001b42d0b50000000000000000000000000000000000000000000000000000000019ecc8940000000000000000000000000000000000000000000000000000000018a783d7000000000000000000000000000000000000000000000000000000001772301700000000000000000000000000000000000000000000000000000000164c0558000000000000000000000000000000000000000000000000000000001534456300000000000000000000000000000000000000000000000000000000000005000000000000000000000000000000000000000000000000000000000000000042383638663161363862356334303563316161646135626430343064386432393165323062613131396337653831616632383561376439306163313462333133663a31000000000000000000000000000000000000000000000000000000000000";
        
        char *addr_cstr = new char[addr.length() + 1];
        strcpy(addr_cstr, addr.c_str());

        char *txid_cstr = new char[txid.length() + 1];
        strcpy(txid_cstr, txid.c_str());
    
        struct eth_abi abi;
        ok(eth_abi_init(&abi, ETH_ABI_ENCODE));
    
        ok(eth_abi_address(&abi, &addr_cstr));
        for(int i=0; i<cfs.size();i++) {
            eth_abi_uint64(&abi, &cfs[i]);
        }
        size_t txid_len = txid.length();
        ok(eth_abi_bytes(&abi, (uint8_t **)(&txid_cstr), &txid_len));

        char *hex;
        size_t hexlen;
        ok(eth_abi_to_hex(&abi, &hex, &hexlen));
        ok(eth_abi_free(&abi));
    
        std::string abi_txt = "0x"+std::string(hex);
    
        //std::cout << abi_txt_chk << std::endl << abi_txt << std::endl;
        BOOST_CHECK(abi_txt == abi_txt_chk);
        
        std::string leaf1_chk = "0x2717fa101ab8ae201e9d852b0522cc7f3486494557b48af679ed1864a407fca0";
        
        const char * abi_txt_cstr = abi_txt.c_str();
        ok(eth_is_hex(abi_txt.c_str(), -1));

        uint8_t *abi_bin; // todo free
        int len_abi_bin = eth_hex_to_bytes(&abi_bin, abi_txt_cstr, abi_txt.size());
        BOOST_CHECK(len_abi_bin > 0);

        uint8_t keccak[32];
        BOOST_CHECK(eth_keccak256(keccak, (uint8_t*)abi_bin, len_abi_bin) >= 0);
    
        char * leaf1_hex_cstr;
        int leaf1_hex_len = eth_hex_from_bytes(&leaf1_hex_cstr, keccak, 32);
        BOOST_CHECK(leaf1_hex_len > 0);
        
        std::string leaf1_hex = "0x"+std::string(leaf1_hex_cstr);
        BOOST_CHECK(leaf1_hex == leaf1_chk);
        
        delete []addr_cstr;
        delete []txid_cstr;
        free(abi_bin);
        free(leaf1_hex_cstr);
    }
    // abi.encodeParameters ['address','uint256['+totalsteps+']','string']
    
    // leaf2
    {
        std::string addr = "0x93AAfed0319B064De0acC8233283F293eE6aa8e0";
        std::string txid = "c26ee493dc9b10dac0ee43d828ce196d0457136b16b2062f853fab790935810e:1";
        std::vector<uint64_t> cfs{0, 4012954773, 5269661183, 3525254523, 2358295702, 1577633212, 1055392061, 706027476, 328103377, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 172730920, 164265388, 156214755, 148558678, 141277823, 134353810, 127769138, 121507181};
        std::string abi_txt_chk = "0x00000000000000000000000093aafed0319b064de0acc8233283f293ee6aa8e0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ef30d495000000000000000000000000000000000000000000000000000000013a18a5ff00000000000000000000000000000000000000000000000000000000d21f1d7b000000000000000000000000000000000000000000000000000000008c90bc96000000000000000000000000000000000000000000000000000000005e08c5bc000000000000000000000000000000000000000000000000000000003ee8013d000000000000000000000000000000000000000000000000000000002a151fd400000000000000000000000000000000000000000000000000000000138e75d1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000a4baa280000000000000000000000000000000000000000000000000000000009ca7dac00000000000000000000000000000000000000000000000000000000094fa5e30000000000000000000000000000000000000000000000000000000008dad35600000000000000000000000000000000000000000000000000000000086bba7f000000000000000000000000000000000000000000000000000000000802139200000000000000000000000000000000000000000000000000000000079d9a3200000000000000000000000000000000000000000000000000000000073e0d6d00000000000000000000000000000000000000000000000000000000000005000000000000000000000000000000000000000000000000000000000000000042633236656534393364633962313064616330656534336438323863653139366430343537313336623136623230363266383533666162373930393335383130653a31000000000000000000000000000000000000000000000000000000000000";
    
        char *addr_cstr = new char[addr.length() + 1];
        strcpy(addr_cstr, addr.c_str());

        char *txid_cstr = new char[txid.length() + 1];
        strcpy(txid_cstr, txid.c_str());

        struct eth_abi abi;
        ok(eth_abi_init(&abi, ETH_ABI_ENCODE));
    
        ok(eth_abi_address(&abi, &addr_cstr));
        for(int i=0; i<cfs.size();i++) {
            eth_abi_uint64(&abi, &cfs[i]);
        }
        size_t txid_len = txid.length();
        ok(eth_abi_bytes(&abi, (uint8_t **)(&txid_cstr), &txid_len));
    
        char *hex;
        size_t hexlen;
        ok(eth_abi_to_hex(&abi, &hex, &hexlen));
        ok(eth_abi_free(&abi));
    
        std::string abi_txt = "0x"+std::string(hex);
    
        //std::cout << abi_txt_chk << std::endl << abi_txt << std::endl;
        BOOST_CHECK(abi_txt == abi_txt_chk);
        
        std::string leaf2_chk = "0x9c8339e32baeafc79f1a34973e1fbd3bdbd2f26bf8fffeab3efe2de625b32657";
        
        const char * abi_txt_cstr = abi_txt.c_str();
        ok(eth_is_hex(abi_txt.c_str(), -1));

        uint8_t *abi_bin; // todo free
        int len_abi_bin = eth_hex_to_bytes(&abi_bin, hex, hexlen);
        BOOST_CHECK(len_abi_bin > 0);

        uint8_t keccak[32];
        BOOST_CHECK(eth_keccak256(keccak, (uint8_t*)abi_bin, len_abi_bin) >= 0);
    
        char * leaf2_hex_cstr; // todo free
        int leaf2_hex_len = eth_hex_from_bytes(&leaf2_hex_cstr, keccak, 32);
        BOOST_CHECK(leaf2_hex_len > 0);
        
        std::string leaf2_hex = "0x"+std::string(leaf2_hex_cstr);
        BOOST_CHECK(leaf2_hex == leaf2_chk);
        
        delete []addr_cstr;
        delete []txid_cstr;
        free(hex);
        free(abi_bin);
        free(leaf2_hex_cstr);
    }
    
    std::string leaf1 = "2717fa101ab8ae201e9d852b0522cc7f3486494557b48af679ed1864a407fca0";
    std::string leaf2 = "9c8339e32baeafc79f1a34973e1fbd3bdbd2f26bf8fffeab3efe2de625b32657";
    std::vector<std::string> leaves = {leaf1, leaf2};
    
    merkle::TreeT<32, sha256_keccak> tree;
    for (const auto & h : leaves) tree.insert(h);
    auto merkle_root = tree.root();
    std::string merkle_root_hex = "0x"+merkle_root.to_string();
    std::cout << "Merkle Tree Root: " << merkle_root_hex << std::endl;
    
    std::string merkle_root_hex_chk = "0x8b8006a6a3db305753991ba2b41f4503cf231122ea1e9bde607c84e28c866605";
    BOOST_CHECK(merkle_root_hex == merkle_root_hex_chk);
}

BOOST_AUTO_TEST_CASE(merkle_x2)
{
    int x = 10;

/*
"zoom",
"word",
"tree",
"boom",
"fine"
 */


    std::string l1 = keccak_string_hash("zoom");
    std::string l2 = keccak_string_hash("word");
    std::string l3 = keccak_string_hash("tree");
    std::string l4 = keccak_string_hash("boom");
    std::string l5 = keccak_string_hash("fine");
    
    std::cout << "L1: " << l1 << std::endl;
    std::cout << "L2: " << l2 << std::endl;
    std::cout << "L3: " << l3 << std::endl;
    std::cout << "L4: " << l4 << std::endl;
    std::cout << "L5: " << l5 << std::endl;
    
    std::set<std::string> ls;
    ls.insert(l1.substr(2));
    ls.insert(l2.substr(2));
    ls.insert(l3.substr(2));
    ls.insert(l4.substr(2));
    ls.insert(l5.substr(2));
    
    merkle::TreeT<32, sha256_keccak> tree;
    for (auto l : ls) tree.insert(l);
    
    auto merkle_root = tree.root();
    std::string merkle_root_hex = "0x"+merkle_root.to_string();
    std::cout << "Merkle Tree Root: " << merkle_root_hex << std::endl;
    
    BOOST_CHECK(l1 == "0xdc4cf0d43db955ee2cd4e743abd69d785d52d0c3a0065f01aa39133ddc8a75b2");
    BOOST_CHECK(merkle_root_hex == "0x7280285c4c978eae570769f24ab364c3a4d17ac41f24ce200d26f1f1c965708b");
    
/*

proof of tree (b25), idx=0
[

"0xdc4cf0d43db955ee2cd4e743abd69d785d52d0c3a0065f01aa39133ddc8a75b2",
"0x5e87a0aa50e1592b6ff212c078aefee112b84a131079e5ab19189eca1b77544d",
"0xfe5fe79c44ff3b4bf0d049828375a7af2f9797ddcb3af2eff5aa745c843ff422"

]
 */
    
    std::vector<std::string> proofs;
    auto leaf_path = tree.path(0); // "tree"
    for (size_t i=0; i< leaf_path->size(); i++) {
        const auto & leaf_path_elm = (*leaf_path)[i];
        std::string proof_i = leaf_path_elm.to_string();
        std::cout << "proof:" << i << " " << proof_i << std::endl;
        proofs.push_back(proof_i);
    }

    BOOST_CHECK(proofs[0] == "dc4cf0d43db955ee2cd4e743abd69d785d52d0c3a0065f01aa39133ddc8a75b2");
    BOOST_CHECK(proofs[1] == "5e87a0aa50e1592b6ff212c078aefee112b84a131079e5ab19189eca1b77544d");
    BOOST_CHECK(proofs[2] == "fe5fe79c44ff3b4bf0d049828375a7af2f9797ddcb3af2eff5aa745c843ff422");
}

BOOST_AUTO_TEST_CASE(merkle_x3)
{
    int x = 10;

    std::string l1 = "5305b95678838ac5f283221fdc9bf342380338ed8779f9787d7cfa6313f46921";
    std::string l2 = "84225a3d51233063c16cc2d18b99770f5e263a1d513e1bf9f554417f9ad248d3";
    std::string l3 = "c26e3c91f030607e60b16577f9effb626b0f58466a39608f14ee90018e8b32a9";
    std::string l4 = "efad5ff499666c50e23ce15eae6511db4a2eafb72fb8dd37ac155384684d27d0";

    std::cout << "L1: " << l1 << std::endl;
    std::cout << "L2: " << l2 << std::endl;
    std::cout << "L3: " << l3 << std::endl;
    std::cout << "L4: " << l4 << std::endl;

    std::set<std::string> ls;
    ls.insert(l1);
    ls.insert(l2);
    ls.insert(l3);
    ls.insert(l4);

    merkle::TreeT<32, sha256_keccak> tree;
    for (auto l : ls) tree.insert(l);

    auto merkle_root = tree.root();
    std::string merkle_root_hex = "0x"+merkle_root.to_string();
    std::cout << "Merkle Tree Root: " << merkle_root_hex << std::endl;

    BOOST_CHECK(merkle_root_hex == "0x9a9a15ae226319e6ad10071cda5887a73233e109b0af6abd6de5ae93a43b2d7b");

    /*

proof
[

5305b9 L - 1b6ace L -- 9a9a15
84225a R /           /
c26e3c L - 0fa783 R /
efad5f R /

]
 */

    std::vector<std::string> proofs;
    auto leaf_path = tree.path(1); // 8422
    for (size_t i=0; i< leaf_path->size(); i++) {
        const auto & leaf_path_elm = (*leaf_path)[i];
        std::string proof_i = leaf_path_elm.to_string();
        std::cout << "proof:" << i << " " << proof_i << std::endl;
        proofs.push_back(proof_i);
    }

    BOOST_CHECK(proofs[0] == "5305b95678838ac5f283221fdc9bf342380338ed8779f9787d7cfa6313f46921");
    BOOST_CHECK(proofs[1] == "0fa78394ff7083770487f4b145014b9e409a35733f02f07ad04df5745fd03bfe");
}

/*
1- "0xa0066f76ebf37e5cd47ca894b87891685d29964116af83b2092b5444890e40d9"
1- "0xc9d1e08af571b3fb32356f6b64ad711a84246c480bb6946cde445ada9ce15fa5",

=0xecc84858472abcf7fcff03be2e00eb2f37fca8e2e2f6f40461ef25777c64bbd1
*/

using namespace std;

BOOST_AUTO_TEST_CASE(merkle_r1)
{
    string branch1 = "a0066f76ebf37e5cd47ca894b87891685d29964116af83b2092b5444890e40d9";
    string branch2 = "c9d1e08af571b3fb32356f6b64ad711a84246c480bb6946cde445ada9ce15fa5";

    char* branch1_cstr = new char[branch1.length() + 1];
    strcpy(branch1_cstr, branch1.c_str());
    uint8_t* branch1_bin_data;
    int len_branch1_bin = eth_hex_to_bytes(&branch1_bin_data, branch1_cstr, branch1.length());
    if (len_branch1_bin != 32) {
        delete[] branch1_cstr;
        if (len_branch1_bin > 0)
            free(branch1_bin_data);
        BOOST_CHECK(false);
        return;
    }
    char* branch2_cstr = new char[branch2.length() + 1];
    strcpy(branch2_cstr, branch2.c_str());
    uint8_t* branch2_bin_data;
    int len_branch2_bin = eth_hex_to_bytes(&branch2_bin_data, branch2_cstr, branch2.length());
    if (len_branch2_bin != 32) {
        delete[] branch1_cstr;
        delete[] branch2_cstr;
        if (len_branch1_bin > 0)
            free(branch1_bin_data);
        if (len_branch2_bin > 0)
            free(branch2_bin_data);
        BOOST_CHECK(false);
        return;
    }

    char*  abi_hex_cstr = NULL;
    size_t abi_hexlen;

    // abi start
    struct eth_abi abi;
    eth_abi_init(&abi, ETH_ABI_ENCODE);
    eth_abi_bytes32(&abi, branch1_bin_data);
    eth_abi_bytes32(&abi, branch2_bin_data);
    eth_abi_to_hex(&abi, &abi_hex_cstr, &abi_hexlen);
    eth_abi_free(&abi);
    // abi end

    uint8_t* abi_bin_data;
    int      len_abi_bin = eth_hex_to_bytes(&abi_bin_data, abi_hex_cstr, abi_hexlen);
    if (len_abi_bin <= 0) {
        BOOST_CHECK(false);
        return;
    }
    uint8_t keccak[32];
    int     keccak_ok = eth_keccak256(keccak, (uint8_t*)abi_bin_data, len_abi_bin);
    if (keccak_ok <= 0) {
        BOOST_CHECK(false);
        return;
    }
    char* out_branch_hex_cstr;
    int   leaf_hex_len = eth_hex_from_bytes(&out_branch_hex_cstr, keccak, 32);
    if (leaf_hex_len < 0) {
        BOOST_CHECK(false);
        return;
    }
    string out_branch = string(out_branch_hex_cstr);
    delete[] branch1_cstr;
    delete[] branch2_cstr;
    free(branch1_bin_data);
    free(branch2_bin_data);
    free(abi_hex_cstr);
    free(abi_bin_data);
    free(out_branch_hex_cstr);

    std::cout << "hash branch:" << out_branch << std::endl;
    BOOST_CHECK(out_branch == "ecc84858472abcf7fcff03be2e00eb2f37fca8e2e2f6f40461ef25777c64bbd1");
}

BOOST_AUTO_TEST_CASE(merkle_r2)
{
    string inp_leaf_hex = "a0066f76ebf37e5cd47ca894b87891685d29964116af83b2092b5444890e40d9";
    vector<string> proofs;
    proofs.push_back("c9d1e08af571b3fb32356f6b64ad711a84246c480bb6946cde445ada9ce15fa5");
    string out_root_hex;
    bool ok = ComputeMintMerkleRoot(inp_leaf_hex, proofs, out_root_hex);
    BOOST_CHECK(ok);
    std::cout << "hash branch:" << out_root_hex << std::endl;
    BOOST_CHECK(out_root_hex == "ecc84858472abcf7fcff03be2e00eb2f37fca8e2e2f6f40461ef25777c64bbd1");
}

BOOST_AUTO_TEST_CASE(merkle_r3)
{
    string inp_leaf_hex = "196f07b6a6c4ffce308dcd52718709d7d004e880b28d5137ce031b615677516e";
    vector<string> proofs;
    proofs.push_back("5b65a8bab16f67b9ac7d236b1742ee7f51c609a1c9397436a36999c9c5d2fbd2");
    proofs.push_back("ecc84858472abcf7fcff03be2e00eb2f37fca8e2e2f6f40461ef25777c64bbd1");
    proofs.push_back("d1654ca022398e95c7c430b13d6a61d46840fe8eaae1e4fd0221ca96dead1fde");

    string out_root_hex;
    bool ok = ComputeMintMerkleRoot(inp_leaf_hex, proofs, out_root_hex);
    BOOST_CHECK(ok);
    std::cout << "hash branch:" << out_root_hex << std::endl;
    BOOST_CHECK(out_root_hex == "28b2bc124313241cbe20f357409bd3fa05d867345796adae1c836c49651712d5");
    // "root":"0x28b2bc124313241cbe20f357409bd3fa05d867345796adae1c836c49651712d5"}

}


BOOST_AUTO_TEST_SUITE_END()
