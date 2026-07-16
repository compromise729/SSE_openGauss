#ifndef JANUSPP_SERVER_H
#define JANUSPP_SERVER_H

#include "libpq/sse_common.h"
#include "libpq/operation_utils.h"
using namespace std;

extern string januspp_fixedivhex;
extern int januspp_d;


// server端存储用于解密
extern int isjanuspp;
extern string januspp_tkn;
extern string januspp_enckeyword;
extern string januspp_msk;
extern string januspp_delcntstr;
extern string januspp_skI;
extern vector<string> januspp_encindvec;
extern vector<string> januspp_tagvec;
extern set<string> januspp_cacheset;//ind
extern vector<string> januspp_punckeyvec;//punckey

extern map<string, string> januspp_decryptmap;//encind->ind
extern vector<string> januspp_reinsertvec;


string januspp_crypto_primitives_hmac(const string& key, const string& msg, int lambda);
string januspp_pad(const string& s, size_t bs);
string januspp_key_trim(const string& key);
string januspp_encrypt(const string& key, const string& raw, const string& iv);
string januspp_getresult(string &tablename);
#endif 
