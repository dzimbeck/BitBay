// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>  // for 'map_list_of()'

#include "checkpoints.h"

#include "main.h"
#include "txdb.h"
#include "uint256.h"

static const int nCheckpointSpan = 1500;

namespace Checkpoints {
typedef std::map<int, uint256> MapCheckpoints;

//
// What makes a good checkpoint block?
// + Is surrounded by blocks with reasonable timestamps
//   (no blocks before with a timestamp after, none after with
//    timestamp before)
// + Contains no strange transactions
//
static MapCheckpoints mapCheckpoints = boost::assign::map_list_of(
    2,
    uint256("0x00000919fd3f37cfee56533b5fa514d0149f6805527ffbae28cab8945ee83c83"))(
    1500,
    uint256("0x0000001929222f6bd4d173f786f7da167dc802bccf701ce835cad3cd7e8ee2d1"))(
    3000,
    uint256("0x000000232d8482954627fb4bfdd11cdd0d59ad89e2ecdb26d7e2d01205c31251"))

    // Standard checkpoint
    (50000, uint256("0xcac5161b1444dda0c8de94e3edc25c959efeb75e1578b0f609e5558203d36360"))(
        100000,
        uint256("0x9eb705562041329dc587d5aa3fa0627ec7ff1337bad272857016670bbff8afd5"))(
        150000,
        uint256("0xe4516cb745194ddec207f8f76b26f9c96a93174dd997a21ccaddc7a59a135d20"))(
        200000,
        uint256("0x1d4039f055470eb02c671aa565b7b87788eb588521da04e006a88176296708b7"))(
        250000,
        uint256("0x6f2f813c67209811ae8f86191c4fa15aef4567b11282973d5f03431a21b194be"))(
        300000,
        uint256("0x464a9b1171e2589e5b923ef8cc4ef3a134883fef7cce92a825da9a0c5067921a"))(
        350000,
        uint256("0xe706de8c8ba1d8cf3699e1a9da5a77358e9d144d3a7bed4a117d27b54563c633"))(
        400000,
        uint256("0x752ca9eaf4c96aea105a26b4171a780a150d840a1559ed959ed85547010222ae"))(
        450000,
        uint256("0x3fb36f266e0eb008f55eec48fe98ccd93e13096c4f01c8c37456c97e58d12a8a"))(
        500000,
        uint256("0xbcc4c98bc8d1e5e158bef65ada2844a007d9b13aed1c99e4123839a5a0bb47bc"))(
        550000,
        uint256("0x55e9a554c2921a62b3764e207f27a25f4ef4973013cc58a15df6b1a108aa2aeb"))(
        600000,
        uint256("0x56b86fecad896dca612edcd30d58e74d7f69f89ac20d0ed565678d5e0487bdb2"))(
        650000,
        uint256("0x39ebdc08b33b850dbebe7477b7a605087d3b91b54be0462775875247e393031c"))(
        700000,
        uint256("0x1acd888e8449651f76c957e8b70e26c429581a5044f2d25774967afac4f634bb"))(
        750000,
        uint256("0xdc7139a4c036150631174033de1d728d8748a986d386de16a54e86c05e6f2184"))(
        800000,
        uint256("0x36c9c7a51fe2cdb37dbb48522977ded17d23fe3bb3eb833bf8693d546efaa6b5"))(
        850000,
        uint256("0x812b5d7f17fdac078f15e1fb247985e85b1b42fdc38ddc8ec57c2bf161119136"))(
        900000,
        uint256("0x4a52c0134a858cb5492a97da32c130642acf35afe182835b5f997570bb842ff3"))(
        950000,
        uint256("0xf15c264ee818848c94b25859287b72860724a4c3a4f7d823065286792898b4a0"))(
        1000000,
        uint256("0x53314bb2c9769cd0016e23a558d6ff663122290f30e8383e5c7c751c8f1cf939"))(
        1050000,
        uint256("0x1f2c40a318265be6bc1613e12e67ae8f85a9284e7497fc5e890eb2ccdddb0733"))(
        1006595,
        uint256("0x24cd44ce866cde0d727ee94b3d34739ce737bc83857a72f342a1335db1cd7580"))(
        1100000,
        uint256("0xdcd485cf7c025ca420252558fef4e0e29010575e03a2faa2bc73f2ce61df39e6"))(
        1150000,
        uint256("0x119cbf301cae317c33af7f3438a488b2359c2671983ac2e3fdea6511a3b70477"))(
        1200000,
        uint256("0x867248955379b0f1cbbd8a1b5a2a7d29b0535337c1121e62b55d8de22b5d3700"))(
        1250000,
        uint256("0xd82c3d0251c62e3f1c56f8748dfd7ea1e9033a1c879c7348a686444be752e538"))(
        1300000,
        uint256("0x8aeb92f989e55a9884b6a1920eb37e2e9c587795be78741f322e6d3edb4fb68e"))(
        1350000,
        uint256("0x34018bc8829610201740346ffb292df12e195c11d481324ffbd006261b3ddd21"))(
        1400000,
        uint256("0x660091f2c7c10bf0f9e842a393f6d123184aa9cd14ee8857505ccbc8f31b3aa8"))(
        1450000,
        uint256("0xaa5e0c9ef01b4c6cd214b91592b0bae41b6639dbc049a5848c8c1bb63d0c8644"))(
        1500000,
        uint256("0x9bbc965b323aad1617d91e8e26d57a415bc9bb2368e216bd76eb3d381c3f1e62"))(
        1550000,
        uint256("0x6ff7ff2bfba2d856eae85f0227852eeac4ef8d4c2a64eb17266c5447d547c919"))(
        1600000,
        uint256("0x669102929bde2fd189e99b2d97fb70a92bdf358f166e8a4fe6c56e3fa01444e7"))(
        1650000,
        uint256("0x1c948a2ca3c539b79ec4f067d7275fd5926b7290a638d04363e17b50cf3d91e3"))(
        1700000,
        uint256("0x8e8594580da9122f97cbfde7d377f5a8ac3a14ebebff97c524cf87bfaa6b40a8"))(
        1750000,
        uint256("0xbdea206b11220368cc6eee31b676ecb2479aa235fa759cac4f6a610e4063dd7a"))(
        1800000,
        uint256("0x762d425b063ae2afd6694f66a5b57bb5c814f04adab5425c32747734864f4f05"))(
        1850000,
        uint256("0xe16ac1877f67c09ac7723c5c738020c299638beeb55f926fd115634f8fe6cea1"))(
        1900000,
        uint256("0xbcbe86228a38916348afc35ca36e8e711e88b3cd61c77b5660f9c70d4ac5b150"))(
        1950000,
        uint256("0x741b7c933dfcf8467e97c4532f20501acb75bcb9fbe59473df09eef98784e461"))(
        2000000,
        uint256("0x8a5ed4e2b961c2b09b72e7b371fc1548637525914a27ac58c4c0089a87bed873"))(
        2050000,
        uint256("0xbd66fde550dc98250c9afa0f57e3186cc6de40f8d29015b3870c97da5512bf48"))(
        2100000,
        uint256("0x557b1373d0a1461629158df984eee628dd4366fa7c9998c5420f060c5cfd0159"))(
        2150000,
        uint256("0x77a3230da05c18b0c88f8c46f2e3e01413c92485e6d8f9d3f1503794c128a9bd"))(
        2200000,
        uint256("0x7577aeadba5a386148e9723e8c11391e4742762a466f470487c075b1aca922b9"))(
        2250000,
        uint256("0x8ab96c3aade375b53ea951b13a3c6a97b08ac8f97a17da9b50062fee0f8251ec"))(
        2300000,
        uint256("0x809a6d35dcf827985582b26437c2408825055ef7478a184e4a05653e620ff20f"))(
        2350000,
        uint256("0x40fde0a1ee04631973e41dfd01c404c70c6c935e0a9223c94e966647cc50eaff"))(
        2400000,
        uint256("0xb2a9f3266d8e32628917fc13bb69bc4d63956a7d6d4da3b231d880ce47fee730"))(
        2450000,
        uint256("0x33db9247d43a43934e650a9eb65dc0d882d5d52e18a822807bfd391ddb49dbf9"))(
        2500000,
        uint256("0x9e40d4a6d420fda7d31071405a9a7a0b8d329119e82a2c76c29e3bfa101682e7"))(
        2535000,
        uint256("0xc450420a2ba0a10552c36e1df63b57d7f767c80b03792ece06f5dbe52ea346d4"))(
        2535700,
        uint256("0x29d0a95099938bf54ca69152383d04ef56a50f5b6b8ee16f3ba3e63014b14f33"))(
        2538000,
        uint256("0x4a8442b2f19bd6334db2bdac49c4af8399a2754c58bebf9100e3c4fbd09885f0"))(
        2538500,
        uint256("0x79160a96297f1b0be81d2d4736bc2005008f1fc134992bac9c4f1d7573529417"))(
        2539000,
        uint256("0xf91b0b8dbb471c53db394ccb3d1c96eaed626931d9c7db0c1026309396d6d5a6"))(
        2539500,
        uint256("0xd35dfa490e17b4fb9c969789fee369910336169815ba5bfd63a8c7bd6af33f34"))(
        2540000,
        uint256("0xfa99c6c19e6147d731c730126d4ba64101e1ad4b998ec2266293f32c3a9bbabc"))(
        2540450,
        uint256("0x3b55afa7d4077a9659b6d70eb38ec01fc4027db0f6a0fcbc0613cd0c25ae43a2"))(
        2550000,
        uint256("0xd8218175d524e507ed9b2bcd8ecfe1193459315c4f3ab2c8d6f3ad0d638d81fe"))(
        2600000,
        uint256("0x97e04e05583f352f23a99c95bef4505696790ace8ded7799affd5aea10e35b49"))(
        2650000,
        uint256("0xba3ba267ed16b4591315228e5e8eee60cb6e9c2be72e998ca4922c3724f2e27c"))(
        2700000,
        uint256("0x8770fe1f5e5f4808041b92e18a49042a07f3c33ce14386ebf733772a2eb36154"))(
        2750000,
        uint256("0x2d322382f5cc65a99eac3b9c8299c3da9c4e3e236ce68c2597d227762788680e"))(
        2800000,
        uint256("0x7c5fd71b1db60d25c777c73a4a70da56a1e5ebb2f0e153d13c60818f562fd37d"))(
        2850000,
        uint256("0x041892d5c4fadf34a8211cd6c6bb1413b1f47d54644a8425c88212d57c7f7cf0"))(
        2900000,
        uint256("0xb8cbdf2542196e1c6ecf59bf23c5ed335623d780024f23eb58ec5b3838061800"))(
        2950000,
        uint256("0xd945ba7e771732932e964d9a1e13072bb3e85a73e8184c7cff2e00395cbcd0c4"))(
        3000000,
        uint256("0x492dfc7ea510372648b408bb15e7b062836a632f649f0cbade636dc5788cbbf1"))(
        3050000,
        uint256("0xf227dfc3c12a18720f7b6a5a01e6928695fd58db504a14aa0fb5f5dc0dbefef4"))(
        3100000,
        uint256("0x3bb847ddc73c3faea627c9007e80be79fab66e6a127e315531b012afb3b10d28"))(
        3150000,
        uint256("0x5c37869bcd56dbe4b97441af8ac740d39d3c3f852e678f5946ae02bb3ce40e3c"))(
        3200000,
        uint256("0xb239e2cfc14ea47cfb009a514b98483f070b2525dab9b92b58aa142575e6c7e0"))(
        3250000,
        uint256("0x19e7e5796e5a5d1979328424d0ec51833799738d9c0426a73b36d4c10fd3f41d"))(
        3300000,
        uint256("0x16d7714d7c3975d1e1cb0cdc9a2804e0a7655d97968b31f41de68dc14f2e327d"))(
        3350000,
        uint256("0xb8ddaf3d4ecb2f3667560cb1e2158fb85892aaa7c96d741f97aaa75449657659"))(
        3400000,
        uint256("0xbbd877efe5f6c8518d4a7f6cedc7852b9cc01344e59e11b8acdd0b651fd13ff2"))(
        3450000,
        uint256("0x40fe6f222945ba4cb495444d5629374a406a2df540a2c8097fda607a9f633a14"))(
        3500000,
        uint256("0x2da38f3dc13e96b23b0b892185d002f5b21e799a032f3d20729c14f839e4eff2"))(
        3550000,
        uint256("0x7a0df19b4cc641c3bb06607341bddb1d17548a0f7a7e73883524a2e57dd3ad89"))(
        3600000,
        uint256("0xee3eb9a9969e10da71175d1497bfe11f2eb0b0135aca92552790f472940b1d97"))(
        3650000,
        uint256("0x6c517a01b270374d17fed0acd8e5e1f2601b222e849f93676962def144274141"))(
        3700000,
        uint256("0x86032d4f5f2f2798d58f46c2bc79dbabc95a315c69faa80f9b6689c2a86ffab9"))(
        3750000,
        uint256("0x1f09402e6bce59d30aa442f1b66d463e55fc9ae8194fc48149d9e32f5a4b0cb4"))(
        3800000,
        uint256("0x4673390a443f6110858ce6cf73b3f1136bca8673743209ebcdbd4279cfe94d6c"))(
        3850000,
        uint256("0x6ba66fdca0b16a7ad8c903231fca1ba20fecf90f33800fd88d710fcc28ce0177"))(
        3900000,
        uint256("0x1cfad64998608d9f978dd971c6efe488878e8af045ce9fa54c9d9b32badc5525"))(
        3950000,
        uint256("0x3cdfa9370d355ddac6e99f717f02841b94ce2f4cc5948d6481e7db9fb63d7c0e"))(
        4000000,
        uint256("0x8a39a74dab58ec480b14bf192f168713f62dbd377411673ca7e52685cd8836b9"))(
        4050000,
        uint256("0x10a99d54bcd95335d05f7cf694a4f5bf866c22646442ff7250d02e407eb3bbe1"))(
        4100000,
        uint256("0xb002e7a046c79074c80bf087d6a9cc76e999d66eb65a4addb8e3a94286d766bf"))(
        4150000,
        uint256("0x9e4881f073d4e756eed13078c54aea0158c36e51c738e835809e0728297bb28b"))(
        4200000,
        uint256("0xebfd5759bad5a16bc4c2bb8d1a5ed6a68361bb5a7545d0d7e1dd3379b98b9ee0"))(
        4250000,
        uint256("0xaf97393163e27beddd85c28649fa147e12ff42ce190eb83648f34f5a8c8c8d5d"))(
        4300000,
        uint256("0xce42044965719fd8bf710a6d39e8d8a1d2a717098688af1d397d712fb543bb42"))(
        4350000,
        uint256("0x19ca496ab126268f98b3f9afe2e3ac9e6c49a398c842ad0eac5a767216d6840c"))(
        4400000,
        uint256("0x91046a374edb24f9cc3d2f233589e27e7c24dfa86703c6be949cfef838e44af5"))(
        4450000,
        uint256("0x3677cd760eebb4c57e256772099cc6c55ca461a8eefc6bf494e9beb3d717dcc9"))(
        4500000,
        uint256("0x18bae9fd91d8e833afcf53a171fa44df2e01a50b0d92e4609c0eacd645f35d12"))(
        4550000,
        uint256("0xe33b44c668ba85cadf925d3ba70a267de62a31cc0cdb804e7b8fda34dc9209fd"))(
        4600000,
        uint256("0x748ef872052ae5d7a6bc3590ac9bf869187554bda99ecf4cb39db6d4789241ad"))(
        4650000,
        uint256("0xeeae0501ce501ac7371873cdbe53d7703f018311657099a2ff97f8f215e7f95a"))(
        4700000,
        uint256("0x3e3524762f6a2ca501d0906a9f6100ae2305e94a756aa5ade503088f6f8fc4d4"))(
        4750000,
        uint256("0x1bee031bb398f1411db0d11a49dd91e65f95b38d865485048623db13bf01f9f4")

    );

static MapCheckpoints mapFailspoints = boost::assign::map_list_of(
    20001,
    uint256("0xd1dd921b61df2b6c27e5d3cffd39ab725e58a1fbea197d0ba3d25599522f8a8e"));

// TestNet checkpoints
static MapCheckpoints mapCheckpointsTestnet = boost::assign::map_list_of(
    20000,
    uint256("0x0edb7e77aba40a85862f999f8379fae959c363ecefeac5e9ac00eb3250532409"));

static MapCheckpoints mapFailspointsTestnet = boost::assign::map_list_of(
    20001,
    uint256("0xd1dd921b61df2b6c27e5d3cffd39ab725e58a1fbea197d0ba3d25599522f8a8e"));

bool CheckHardened(int nHeight, const uint256& hash) {
	MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);
	MapCheckpoints& failspoints = (TestNet() ? mapFailspointsTestnet : mapFailspoints);

	// failpoint check
	{
		MapCheckpoints::const_iterator i = failspoints.find(nHeight);
		if (i != checkpoints.end()) {
			if (hash == i->second)
				return false;
		}
	}
	// checkpoint check
	MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
	if (i == checkpoints.end())
		return true;

	return hash == i->second;
}

int GetTotalBlocksEstimate() {
	MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);

	if (checkpoints.empty())
		return 0;
	return checkpoints.rbegin()->first;
}

CBlockIndex* GetLastCheckpoint(const std::map<uint256, CBlockIndex*>& mapBlockIndex) {
	MapCheckpoints& checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);

	MapCheckpoints::reverse_iterator it = checkpoints.rbegin();
	while (it != checkpoints.rend()) {
		const uint256&                                  hash = it->second;
		std::map<uint256, CBlockIndex*>::const_iterator t    = mapBlockIndex.find(hash);
		if (t != mapBlockIndex.end())
			return t->second;

		it++;
	}
	return NULL;
}

// Automatically select a suitable sync-checkpoint
const CBlockIndex* AutoSelectSyncCheckpoint() {
	const CBlockIndex* pindex = pindexBest;
	// Search backward for a block within max span and maturity window
	while (pindex->Prev() && pindex->nHeight + nCheckpointSpan > pindexBest->nHeight)
		pindex = pindex->Prev();
	return pindex;
}

// Check against synchronized checkpoint
bool CheckSync(int nHeight) {
	const CBlockIndex* pindexSync = AutoSelectSyncCheckpoint();

	if (nHeight <= pindexSync->nHeight)
		return false;
	return true;
}

std::vector<int> GetCheckpointsHeights() {
	std::set<int>    sHeight;
	std::vector<int> vHeights;
	MapCheckpoints&  checkpoints = (TestNet() ? mapCheckpointsTestnet : mapCheckpoints);
	MapCheckpoints&  failspoints = (TestNet() ? mapFailspointsTestnet : mapFailspoints);
	{
		MapCheckpoints::iterator it = failspoints.begin();
		while (it != failspoints.end()) {
			sHeight.insert(it->first);
			it++;
		}
	}
	MapCheckpoints::iterator it = checkpoints.begin();
	while (it != checkpoints.end()) {
		sHeight.insert(it->first);
		it++;
	}
	for (auto& it : sHeight) {
		vHeights.push_back(it);
	}
	return vHeights;
}
}  // namespace Checkpoints
