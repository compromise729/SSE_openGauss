#ifndef JANUSPP_CLIENT_H
#define JANUSPP_CLIENT_H

// #include "libpq/base_client.h"
#include "libpq/sse_common.h"
#include "libpq/operation_utils.h"
using namespace std;

extern string januspp_fixedivhex;
extern int januspp_d;

/*januspp_cachetable token use*/
extern string januspp_Kshex;
/*初始的sk sk0hex and skIhex*/
extern string januspp_sk0hexinit;
extern string januspp_skIhexinit;


string januspp_generate_random_key(int length, set<string> &generated_keys);

string januspp_string_to_hashed_binary(const string& msg);
string januspp_crypto_primitives_hmac(const string& key, const string& msg, int lambda);

string januspp_pad(const string& s, size_t bs);
string januspp_key_trim(const string& key);
string januspp_encrypt(const string& key, const string& raw, const string& iv);

void januspp_processinserttuple(string &init_sql, unordered_map<string, pair<string, string> > &storeskhex, vector<string> &enckeyword_vec, vector<string> &encind_vec, vector<string> &tag_vec, unordered_map<string, pair<string, string> > &keyword_stage);
string januspp_processInsert(string &init_sql, string &tablename);
string januspp_processdelete(string &init_sql, string &tablename);

#endif