#ifndef DUAL_SERVER_H
#define DUAL_SERVER_H

#include "libpq/sse_common.h"
#include "libpq/operation_utils.h"
using namespace std;

//dual
extern int isdual;
extern map<string, string> dual_decryptmap;
extern int dual_cntind;

string dual_hmac_sha256(const string& key, const string& message, int flag);
string dual_processdelete(string &tablename, string &keyind, string &ind, string &strmaxcnti);
string dual_Decryptind(string &encind_data, string &label3);

#endif 