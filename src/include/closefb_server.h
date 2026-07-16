#ifndef CLOSEFB_SERVER_H
#define CLOSEFB_SERVER_H

#include "libpq/sse_common.h"
#include "libpq/operation_utils.h"
using namespace std;

extern int isclosefb;
extern int closefb_maxctr;
extern map<string, string> closefb_premap;
extern map<string, string> closefb_decryptmap;

string closefb_sha256(const string& input);
string closefb_recursive_hash(const string& input, int j);

void closefb_paddinghash(string &hash, int len);
string closefb_filter(string &tablename, string &stw, int ctr);
#endif 
