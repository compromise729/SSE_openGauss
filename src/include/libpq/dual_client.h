#ifndef DUAL_CLIENT_H
#define DUAL_CLIENT_H

#include "libpq/sse_common.h"
#include "libpq/operation_utils.h"
#include <cstring>  
using namespace std;

extern string master_key;

string dual_hmac_sha256(const string& key, const string& message, int flag);

/*merge label1, label2 and data to a tuple*/
string mergeToTuple(string label1, string label2, string data);

void dual_processinserttuple(string &dual_sql, set<string> &generated_keys, map<string, string> &map_ukey, map<string, string> &map_ucnt, 
    vector<string> &label1_vec, vector<string> &label2_vec, vector<string> &data_vec, map<string, int> &ind_cnt_map);

string dual_processInsert(string &dual_sql, string &tablename);

string dual_getdtoken(string &dual_sql, string &tablename);

string dual_delete(string &dual_sql, string &tablename);

#endif