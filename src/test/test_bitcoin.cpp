#define BOOST_TEST_MODULE Bitcoin Test Suite
#include <boost/test/unit_test.hpp>

#include "main.h"
#include "wallet.h"

CWallet* pwalletMain;
CClientUIInterface uiInterface;
bool fConfChange = false;
unsigned int nNodeLifespan = 7;
unsigned int nMinerSleep = 500;
bool fUseFastIndex = true;

extern bool fPrintToConsole;
extern void noui_connect();
extern void InitParamsOnStart();

struct TestingSetup {
    TestingSetup() {
        fPrintToConsole = true; // don't want to write to debug.log file
        noui_connect();
        InitParamsOnStart();
        pwalletMain = new CWallet();
        RegisterWallet(pwalletMain);
    }
    ~TestingSetup()
    {
        delete pwalletMain;
        pwalletMain = NULL;
    }
};

BOOST_GLOBAL_FIXTURE(TestingSetup);

void Shutdown(void* parg)
{
  exit(0);
}

void StartShutdown()
{
  exit(0);
}

