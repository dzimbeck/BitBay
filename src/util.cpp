// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"

#include "chainparams.h"
#include "sync.h"
#include "ui_interface.h"
#include "uint256.h"
#include "version.h"

#include <algorithm>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp> // for startswith() and endswith()

// Work around clang compilation problem in Boost 1.46:
// /usr/include/boost/program_options/detail/config_file.hpp:163:17: error: call to function 'to_internal' that is neither visible in the template definition nor found by argument-dependent lookup
// See also: http://stackoverflow.com/questions/10020179/compilation-fail-in-boost-librairies-program-options
//           http://clang.debian.net/status.php?version=3.0&key=CANNOT_FIND_FUNCTION
namespace boost {
    namespace program_options {
        std::string to_internal(const std::string&);
    }
}

#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <stdarg.h>

#ifdef WIN32
#ifdef _MSC_VER
#pragma warning(disable:4786)
#pragma warning(disable:4804)
#pragma warning(disable:4805)
#pragma warning(disable:4717)
#endif
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501
#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501
#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h> /* for _commit */
#include "shlobj.h"
#elif defined(__linux__)
# include <sys/prctl.h>
#endif

using namespace std;

map<string, string> mapArgs;
map<string, vector<string> > mapMultiArgs;
bool fDebug = false;
bool fPrintToConsole = false;
bool fPrintToDebugLog = true;
bool fDaemon = false;
bool fServer = false;
bool fCommandLine = false;
string strMiscWarning;
bool fNoListen = false;
bool fLogTimestamps = false;
volatile bool fReopenDebugLog = false;
uint320 uint320_MAX("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

// Init OpenSSL library multithreading support
static CCriticalSection** ppmutexOpenSSL;
void locking_callback(int mode, int i, const char* file, int line)
{
    if (mode & CRYPTO_LOCK) {
        ENTER_CRITICAL_SECTION(*ppmutexOpenSSL[i]);
    } else {
        LEAVE_CRITICAL_SECTION(*ppmutexOpenSSL[i]);
    }
}

LockedPageManager LockedPageManager::instance;

// Init
class CInit
{
public:
    CInit()
    {
        // Init OpenSSL library multithreading support
        ppmutexOpenSSL = (CCriticalSection**)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(CCriticalSection*));
        for (int i = 0; i < CRYPTO_num_locks(); i++)
            ppmutexOpenSSL[i] = new CCriticalSection();
        CRYPTO_set_locking_callback(locking_callback);

#ifdef WIN32
        // Seed OpenSSL PRNG with current contents of the screen
        RAND_screen();
#endif

        // Seed OpenSSL PRNG with performance counter
        RandAddSeed();
    }
    ~CInit()
    {
        // Securely erase the memory used by the PRNG
        RAND_cleanup();
        // Shutdown OpenSSL library multithreading support
        CRYPTO_set_locking_callback(NULL);
        for (int i = 0; i < CRYPTO_num_locks(); i++)
            delete ppmutexOpenSSL[i];
        OPENSSL_free(ppmutexOpenSSL);
    }
}
instance_of_cinit;








void RandAddSeed()
{
    // Seed with CPU performance counter
    int64_t nCounter = GetPerformanceCounter();
    RAND_add(&nCounter, sizeof(nCounter), 1.5);
    memset(&nCounter, 0, sizeof(nCounter));
}

void RandAddSeedPerfmon()
{
    RandAddSeed();

    // This can take up to 2 seconds, so only do it every 10 minutes
    static int64_t nLastPerfmon;
    if (GetTime() < nLastPerfmon + 10 * 60)
        return;
    nLastPerfmon = GetTime();

#ifdef WIN32
    // Don't need this on Linux, OpenSSL automatically uses /dev/urandom
    // Seed with the entire set of perfmon data
    unsigned char pdata[250000];
    memset(pdata, 0, sizeof(pdata));
    unsigned long nSize = sizeof(pdata);
    long ret = RegQueryValueExA(HKEY_PERFORMANCE_DATA, "Global", NULL, NULL, pdata, &nSize);
    RegCloseKey(HKEY_PERFORMANCE_DATA);
    if (ret == ERROR_SUCCESS)
    {
        RAND_add(pdata, nSize, nSize/100.0);
        OPENSSL_cleanse(pdata, nSize);
        LogPrint("rand", "RandAddSeed() %lu bytes\n", nSize);
    }
#endif
}

uint64_t GetRand(uint64_t nMax)
{
    if (nMax == 0)
        return 0;

    // The range of the random source must be a multiple of the modulus
    // to give every possible output value an equal possibility
    uint64_t nRange = (std::numeric_limits<uint64_t>::max() / nMax) * nMax;
    uint64_t nRand = 0;
    do
        RAND_bytes((unsigned char*)&nRand, sizeof(nRand));
    while (nRand >= nRange);
    return (nRand % nMax);
}

int GetRandInt(int nMax)
{
    return GetRand(nMax);
}

uint256 GetRandHash()
{
    uint256 hash;
    RAND_bytes((unsigned char*)&hash, sizeof(hash));
    return hash;
}

// LogPrintf() has been broken a couple of times now
// by well-meaning people adding mutexes in the most straightforward way.
// It breaks because it may be called by global destructors during shutdown.
// Since the order of destruction of static/global objects is undefined,
// defining a mutex as a global object doesn't work (the mutex gets
// destroyed, and then some later destructor calls OutputDebugStringF,
// maybe indirectly, and you get a core dump at shutdown trying to lock
// the mutex).

static boost::once_flag debugPrintInitFlag = BOOST_ONCE_INIT;
// We use boost::call_once() to make sure these are initialized in
// in a thread-safe manner the first time it is called:
static FILE* fileout = NULL;
static boost::mutex* mutexDebugLog = NULL;

static void DebugPrintInit()
{
    assert(fileout == NULL);
    assert(mutexDebugLog == NULL);

    boost::filesystem::path pathDebug = GetDataDir() / "debug.log";
    fileout = fopen(pathDebug.string().c_str(), "a");
    if (fileout) setbuf(fileout, NULL); // unbuffered

    mutexDebugLog = new boost::mutex();
}

bool LogAcceptCategory(const char* category)
{
    if (category != NULL)
    {
        if (!fDebug)
            return false;

        // Give each thread quick access to -debug settings.
        // This helps prevent issues debugging global destructors,
        // where mapMultiArgs might be deleted before another
        // global destructor calls LogPrint()
        static boost::thread_specific_ptr<set<string> > ptrCategory;
        if (ptrCategory.get() == NULL)
        {
            const vector<string>& categories = mapMultiArgs["-debug"];
            ptrCategory.reset(new set<string>(categories.begin(), categories.end()));
            // thread_specific_ptr automatically deletes the set when the thread ends.
        }
        const set<string>& setCategories = *ptrCategory.get();

        // if not debugging everything and not debugging specific category, LogPrint does nothing.
        if (setCategories.count(string("")) == 0 &&
            setCategories.count(string(category)) == 0)
            return false;
    }
    return true;
}

int LogPrintStr(const std::string &str)
{
    int ret = 0; // Returns total number of characters written
    if (fPrintToConsole)
    {
        // print to console
        ret = fwrite(str.data(), 1, str.size(), stdout);
    }
    else if (fPrintToDebugLog)
    {
        static bool fStartedNewLine = true;
        boost::call_once(&DebugPrintInit, debugPrintInitFlag);

        if (fileout == NULL)
            return ret;

        boost::mutex::scoped_lock scoped_lock(*mutexDebugLog);

        // reopen the log file, if requested
        if (fReopenDebugLog) {
            fReopenDebugLog = false;
            boost::filesystem::path pathDebug = GetDataDir() / "debug.log";
            if (freopen(pathDebug.string().c_str(),"a",fileout) != NULL)
                setbuf(fileout, NULL); // unbuffered
        }

        // Debug print useful for profiling
        if (fLogTimestamps && fStartedNewLine)
            ret += fprintf(fileout, "%s ", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()).c_str());
        if (!str.empty() && str[str.size()-1] == '\n')
            fStartedNewLine = true;
        else
            fStartedNewLine = false;

        ret = fwrite(str.data(), 1, str.size(), fileout);
    }

    return ret;
}

void ParseString(const string& str, char c, vector<string>& v)
{
    if (str.empty())
        return;
    string::size_type i1 = 0;
    string::size_type i2;
    while (true)
    {
        i2 = str.find(c, i1);
        if (i2 == str.npos)
        {
            v.push_back(str.substr(i1));
            return;
        }
        v.push_back(str.substr(i1, i2-i1));
        i1 = i2+1;
    }
}


string FormatMoney(int64_t n, bool fPlus)
{
    // Note: not using straight sprintf here because we do NOT want
    // localized number formatting.
    int64_t n_abs = (n > 0 ? n : -n);
    int64_t quotient = n_abs/COIN;
    int64_t remainder = n_abs%COIN;
    string str = strprintf("%d.%08d", quotient, remainder);

    // Right-trim excess zeros before the decimal point:
    int nTrim = 0;
    for (int i = str.size()-1; (str[i] == '0' && isdigit(str[i-2])); --i)
        ++nTrim;
    if (nTrim)
        str.erase(str.size()-nTrim, nTrim);

    if (n < 0)
        str.insert((unsigned int)0, 1, '-');
    else if (fPlus && n > 0)
        str.insert((unsigned int)0, 1, '+');
    return str;
}


bool ParseMoney(const string& str, int64_t& nRet)
{
    return ParseMoney(str.c_str(), nRet);
}

bool ParseMoney(const char* pszIn, int64_t& nRet)
{
    string strWhole;
    int64_t nUnits = 0;
    const char* p = pszIn;
    while (isspace(*p))
        p++;
    for (; *p; p++)
    {
        if (*p == '.')
        {
            p++;
            int64_t nMult = CENT*10;
            while (isdigit(*p) && (nMult > 0))
            {
                nUnits += nMult * (*p++ - '0');
                nMult /= 10;
            }
            break;
        }
        if (isspace(*p))
            break;
        if (!isdigit(*p))
            return false;
        strWhole.insert(strWhole.end(), *p);
    }
    for (; *p; p++)
        if (!isspace(*p))
            return false;
    if (strWhole.size() > 10) // guard against 63 bit overflow
        return false;
    if (nUnits < 0 || nUnits > COIN)
        return false;
    int64_t nWhole = atoi64(strWhole);
    int64_t nValue = nWhole*COIN + nUnits;

    nRet = nValue;
    return true;
}

// safeChars chosen to allow simple messages/URLs/email addresses, but avoid anything
// even possibly remotely dangerous like & or >
static string safeChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890 .,;_/:?@");
string SanitizeString(const string& str)
{
    string strResult;
    for (std::string::size_type i = 0; i < str.size(); i++)
    {
        if (safeChars.find(str[i]) != std::string::npos)
            strResult.push_back(str[i]);
    }
    return strResult;
}

static const signed char phexdigit[256] =
{ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1,
  -1,0xa,0xb,0xc,0xd,0xe,0xf,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,0xa,0xb,0xc,0xd,0xe,0xf,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, };

static void InterpretNegativeSetting(string name, map<string, string>& mapSettingsRet)
{
    // interpret -nofoo as -foo=0 (and -nofoo=0 as -foo=1) as long as -foo not set
    if (name.find("-no") == 0)
    {
        std::string positive("-");
        positive.append(name.begin()+3, name.end());
        if (mapSettingsRet.count(positive) == 0)
        {
            bool value = !GetBoolArg(name, false);
            mapSettingsRet[positive] = (value ? "1" : "0");
        }
    }
}

void ParseParameters(int argc, const char* const argv[])
{
    mapArgs.clear();
    mapMultiArgs.clear();
    for (int i = 1; i < argc; i++)
    {
        std::string str(argv[i]);
        std::string strValue;
        size_t is_index = str.find('=');
        if (is_index != std::string::npos)
        {
            strValue = str.substr(is_index+1);
            str = str.substr(0, is_index);
        }
#ifdef WIN32
        boost::to_lower(str);
        if (boost::algorithm::starts_with(str, "/"))
            str = "-" + str.substr(1);
#endif
        if (str[0] != '-')
            break;

        mapArgs[str] = strValue;
        mapMultiArgs[str].push_back(strValue);
    }

    // New 0.6 features:
    for(const pair<string,string>& entry : mapArgs)
    {
        string name = entry.first;

        //  interpret --foo as -foo (as long as both are not set)
        if (name.find("--") == 0)
        {
            std::string singleDash(name.begin()+1, name.end());
            if (mapArgs.count(singleDash) == 0)
                mapArgs[singleDash] = entry.second;
            name = singleDash;
        }

        // interpret -nofoo as -foo=0 (and -nofoo=0 as -foo=1) as long as -foo not set
        InterpretNegativeSetting(name, mapArgs);
    }
}

std::string GetArg(const std::string& strArg, const std::string& strDefault)
{
    if (mapArgs.count(strArg))
        return mapArgs[strArg];
    return strDefault;
}

int64_t GetArg(const std::string& strArg, int64_t nDefault)
{
    if (mapArgs.count(strArg))
        return atoi64(mapArgs[strArg]);
    return nDefault;
}

bool GetBoolArg(const std::string& strArg, bool fDefault)
{
    if (mapArgs.count(strArg))
    {
        if (mapArgs[strArg].empty())
            return true;
        return (atoi(mapArgs[strArg]) != 0);
    }
    return fDefault;
}

bool SoftSetArg(const std::string& strArg, const std::string& strValue)
{
    if (mapArgs.count(strArg))
        return false;
    mapArgs[strArg] = strValue;
    return true;
}

bool SoftSetBoolArg(const std::string& strArg, bool fValue)
{
    if (fValue)
        return SoftSetArg(strArg, std::string("1"));
    else
        return SoftSetArg(strArg, std::string("0"));
}

bool WildcardMatch(const char* psz, const char* mask)
{
    while (true)
    {
        switch (*mask)
        {
        case '\0':
            return (*psz == '\0');
        case '*':
            return WildcardMatch(psz, mask+1) || (*psz && WildcardMatch(psz+1, mask));
        case '?':
            if (*psz == '\0')
                return false;
            break;
        default:
            if (*psz != *mask)
                return false;
            break;
        }
        psz++;
        mask++;
    }
}

bool WildcardMatch(const string& str, const string& mask)
{
    return WildcardMatch(str.c_str(), mask.c_str());
}








static std::string FormatException(std::exception* pex, const char* pszThread)
{
#ifdef WIN32
    char pszModule[MAX_PATH] = "";
    GetModuleFileNameA(NULL, pszModule, sizeof(pszModule));
#else
    const char* pszModule = "bitbay";
#endif
    if (pex)
        return strprintf(
            "EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(), pszModule, pszThread);
    else
        return strprintf(
            "UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
}

void PrintException(std::exception* pex, const char* pszThread)
{
    std::string message = FormatException(pex, pszThread);
    LogPrintf("\n\n************************\n%s\n", message);
    fprintf(stderr, "\n\n************************\n%s\n", message.c_str());
    strMiscWarning = message;
    throw;
}

void PrintExceptionContinue(std::exception* pex, const char* pszThread)
{
    std::string message = FormatException(pex, pszThread);
    LogPrintf("\n\n************************\n%s\n", message);
    fprintf(stderr, "\n\n************************\n%s\n", message.c_str());
    strMiscWarning = message;
}

boost::filesystem::path GetDefaultDataDir()
{
    namespace fs = boost::filesystem;
    // Windows < Vista: C:\Documents and Settings\Username\Application Data\BitBay
    // Windows >= Vista: C:\Users\Username\AppData\Roaming\BitBay
    // Mac: ~/Library/Application Support/BitBay
    // Unix: ~/.bitbay
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath(CSIDL_APPDATA) / "BitBay";
#else
    fs::path pathRet;
    char* pszHome = getenv("HOME");
    if (pszHome == NULL || strlen(pszHome) == 0)
        pathRet = fs::path("/");
    else
        pathRet = fs::path(pszHome);
#ifdef MAC_OSX
    // Mac
    pathRet /= "Library/Application Support";
    fs::create_directory(pathRet);
    return pathRet / "BitBay";
#else
    // Unix
    return pathRet / ".bitbay";
#endif
#endif
}

static boost::filesystem::path pathCached[CChainParams::MAX_NETWORK_TYPES+1];
static CCriticalSection csPathCached;

const boost::filesystem::path &GetDataDir(bool fNetSpecific)
{
    namespace fs = boost::filesystem;

    LOCK(csPathCached);

    int nNet = CChainParams::MAX_NETWORK_TYPES;
    if (fNetSpecific) nNet = Params().NetworkID();

    fs::path &path = pathCached[nNet];

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (mapArgs.count("-datadir")) {
        path = fs::system_complete(mapArgs["-datadir"]);
        if (!fs::is_directory(path)) {
            path = "";
            return path;
        }
    } else {
        path = GetDefaultDataDir();
    }
    if (fNetSpecific)
        path /= Params().DataDir();

    fs::create_directory(path);

    return path;
}

void ClearDatadirCache()
{
    std::fill(&pathCached[0], &pathCached[CChainParams::MAX_NETWORK_TYPES+1],
              boost::filesystem::path());
}

boost::filesystem::path GetConfigFile()
{
    boost::filesystem::path pathConfigFile(GetArg("-conf", "bitbay.conf"));
    if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;
    return pathConfigFile;
}

void ReadConfigFile(map<string, string>& mapSettingsRet,
                    map<string, vector<string> >& mapMultiSettingsRet)
{
    boost::filesystem::ifstream streamConfig(GetConfigFile());
    if (!streamConfig.good())
        return; // No bitcoin.conf file is OK

    set<string> setOptions;
    setOptions.insert("*");

    for (boost::program_options::detail::config_file_iterator it(streamConfig, setOptions), end; it != end; ++it)
    {
        // Don't overwrite existing settings so command line settings override bitcoin.conf
        string strKey = string("-") + it->string_key;
        if (mapSettingsRet.count(strKey) == 0)
        {
            mapSettingsRet[strKey] = it->value[0];
            // interpret nofoo=1 as foo=0 (and nofoo=0 as foo=1) as long as foo not set)
            InterpretNegativeSetting(strKey, mapSettingsRet);
        }
        mapMultiSettingsRet[strKey].push_back(it->value[0]);
    }
    // If datadir is changed in .conf file:
    ClearDatadirCache();
}

boost::filesystem::path GetPidFile()
{
    boost::filesystem::path pathPidFile(GetArg("-pid", "bitbayd.pid"));
    if (!pathPidFile.is_complete()) pathPidFile = GetDataDir() / pathPidFile;
    return pathPidFile;
}

#ifndef WIN32
void CreatePidFile(const boost::filesystem::path &path, pid_t pid)
{
    FILE* file = fopen(path.string().c_str(), "w");
    if (file)
    {
        fprintf(file, "%d\n", pid);
        fclose(file);
    }
}
#endif

bool RenameOver(boost::filesystem::path src, boost::filesystem::path dest)
{
#ifdef WIN32
    return MoveFileExA(src.string().c_str(), dest.string().c_str(),
                      MOVEFILE_REPLACE_EXISTING);
#else
    int rc = std::rename(src.string().c_str(), dest.string().c_str());
    return (rc == 0);
#endif /* WIN32 */
}

void FileCommit(FILE *fileout)
{
    fflush(fileout);                // harmless if redundantly called
#ifdef WIN32
    _commit(_fileno(fileout));
#else
    fsync(fileno(fileout));
#endif
}

void ShrinkDebugFile()
{
    // Scroll debug.log if it's getting too big
    boost::filesystem::path pathLog = GetDataDir() / "debug.log";
    FILE* file = fopen(pathLog.string().c_str(), "r");
    if (file && boost::filesystem::file_size(pathLog) > 10 * 1000000)
    {
        // Restart the file with some of the end
        char pch[200000];
        fseek(file, -sizeof(pch), SEEK_END);
        int nBytes = fread(pch, 1, sizeof(pch), file);
        fclose(file);

        file = fopen(pathLog.string().c_str(), "w");
        if (file)
        {
            fwrite(pch, 1, nBytes, file);
            fclose(file);
        }
    }
}

static int64_t nMockTime = 0;  // For unit testing

int64_t GetTime()
{
    if (nMockTime) return nMockTime;

    return time(NULL);
}

void SetMockTime(int64_t nMockTimeIn)
{
    nMockTime = nMockTimeIn;
}

uint32_t insecure_rand_Rz = 11;
uint32_t insecure_rand_Rw = 11;
void seed_insecure_rand(bool fDeterministic)
{
    //The seed values have some unlikely fixed points which we avoid.
    if(fDeterministic)
    {
        insecure_rand_Rz = insecure_rand_Rw = 11;
    } else {
        uint32_t tmp;
        do{
            RAND_bytes((unsigned char*)&tmp,4);
        }while(tmp==0 || tmp==0x9068ffffU);
        insecure_rand_Rz=tmp;
        do{
            RAND_bytes((unsigned char*)&tmp,4);
        }while(tmp==0 || tmp==0x464fffffU);
        insecure_rand_Rw=tmp;
    }
}

string FormatVersion(int nVersion)
{
    if (nVersion%100 == 0)
        return strprintf("%d.%d.%d", nVersion/1000000, (nVersion/10000)%100, (nVersion/100)%100);
    else
        return strprintf("%d.%d.%d.%d", nVersion/1000000, (nVersion/10000)%100, (nVersion/100)%100, nVersion%100);
}

string FormatFullVersion()
{
    return CLIENT_BUILD;
}

// Format the subversion field according to BIP 14 spec (https://en.bitcoin.it/wiki/BIP_0014)
std::string FormatSubVersion(const std::string& name, int nClientVersion, const std::vector<std::string>& comments)
{
    std::ostringstream ss;
    ss << "/";
    ss << name << ":" << FormatVersion(nClientVersion);
    if (!comments.empty())
        ss << "(" << boost::algorithm::join(comments, "; ") << ")";
    ss << "/";
    return ss.str();
}

#ifdef WIN32
boost::filesystem::path GetSpecialFolderPath(int nFolder, bool fCreate)
{
    namespace fs = boost::filesystem;

    char pszPath[MAX_PATH] = "";

    if(SHGetSpecialFolderPathA(NULL, pszPath, nFolder, fCreate))
    {
        return fs::path(pszPath);
    }

    LogPrintf("SHGetSpecialFolderPathA() failed, could not obtain requested path.\n");
    return fs::path("");
}
#endif

void runCommand(std::string strCommand)
{
    int nErr = ::system(strCommand.c_str());
    if (nErr)
        LogPrintf("runCommand error: system(%s) returned %d\n", strCommand, nErr);
}

void RenameThread(const char* name)
{
#if defined(PR_SET_NAME)
    // Only the first 15 characters are used (16 - NUL terminator)
    ::prctl(PR_SET_NAME, name, 0, 0, 0);
#elif 0 && (defined(__FreeBSD__) || defined(__OpenBSD__))
    // TODO: This is currently disabled because it needs to be verified to work
    //       on FreeBSD or OpenBSD first. When verified the '0 &&' part can be
    //       removed.
    pthread_set_name_np(pthread_self(), name);

#elif defined(MAC_OSX) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED)

// pthread_setname_np is XCode 10.6-and-later
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
    pthread_setname_np(name);
#endif

#else
    // Prevent warnings for unused parameters...
    (void)name;
#endif
}

std::string DateTimeStrFormat(const char* pszFormat, int64_t nTime)
{
    // std::locale takes ownership of the pointer
    std::locale loc(std::locale::classic(), new boost::posix_time::time_facet(pszFormat));
    std::stringstream ss;
    ss.imbue(loc);
    ss << boost::posix_time::from_time_t(nTime);
    return ss.str();
}
