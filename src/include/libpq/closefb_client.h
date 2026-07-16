#ifndef CLOSEFB_CLIENT_H
#define CLOSEFB_CLIENT_H

#include "libpq/sse_common.h"
#include "libpq/operation_utils.h"
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <climits>

extern int closefb_maxctr;

string closefb_sha256(const string& input);
string closefb_recursive_hash(const string& input, int j);

void closefb_paddinghash(string &hash, int len);

void CUpdate(string &stw, map<string, string> &dicw, string &msg, set<string> &generated_keys);

void closefb_processinserttuple(string &init_sql, string &closefb_k1, int &ctr, set<string> &generated_keys, map<string, map<string, string> > &DIC, 
    map<string, string> &KW, vector<string> &ind_vec, vector<string> &keyword_vec, int &flag);

void closefb_RBprocessinserttuple(string &init_sql, string &closefb_k1, int &ctr, set<string> &generated_keys, map<string, map<string, string> > &DIC, 
    map<string, string> &KW);

string closefb_processInsert(string &init_sql, string &closefb_k1, int &ctr, int &flag, string &tablename);
string closefb_RBprocessInsert(string &init_sql, string &closefb_k1, int &ctr, string &tablename);

int closefb_readstatus(const char *file_path, vector<string> &status);
int closefb_writestatus(const char *file_path, vector<string> &status);
#endif