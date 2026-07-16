#include "closefb_server.h"

int isclosefb = 0;
int closefb_maxctr = 1000;
map<string, string> closefb_premap;
map<string, string> closefb_decryptmap; //indexkey -> msgrt

/* Function to compute the SHA-256 hash */
string closefb_sha256(const string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int len = SHA256_DIGEST_LENGTH;
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, input.c_str(), input.length());
    SHA256_Final(hash, &sha256);
    ostringstream oss;
    for (unsigned int i = 0; i < len; i++) {
        oss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return oss.str();
}

/* Recursive function to compute H^j(kt_w) */
string closefb_recursive_hash(const string& input, int j) {
    if (j == 1) 
        return closefb_sha256(input);
    string previous_hash = closefb_recursive_hash(input, j - 1);
    return closefb_sha256(previous_hash);
}

/* padding hash */
void closefb_paddinghash(string &hash, int len){
    int hashlen = hash.length();
    if (hashlen >= len)
        return;
    string tmp(len - hashlen, '0');
    hash += tmp;
}

string closefb_filter(string &tablename, string &stw, int ctr){
    string stw0, stw1, indexkey, indexvalue;
    string hstw1, msgrt, rt, rt0, rt1, hrt1, encmsg;
    while(ctr <= closefb_maxctr){
        stw0 = stw + "0";
        indexkey = closefb_recursive_hash(stw0, 1);

        if (closefb_premap.find(base64_encode(indexkey)) != closefb_premap.end()){
            indexvalue = base64_decode(closefb_premap[base64_encode(indexkey)]);
            stw1 = stw + "1";
            hstw1 = closefb_recursive_hash(stw1, 1);
            closefb_paddinghash(hstw1, indexvalue.length());
            msgrt = xorstring(hexTobinary(indexvalue), hexTobinary(hstw1));
            msgrt = binaryTohex(msgrt);
            rt = msgrt.substr(msgrt.length() - 32);
            encmsg = msgrt.substr(0, msgrt.length() - 32);
            closefb_decryptmap[base64_encode(indexkey)] = base64_encode(encmsg);
            string nullstr(32, '0');
            while (rt != nullstr) {
                rt0 = rt + "0";
                rt1 = rt + "1";
                indexkey = closefb_recursive_hash(rt0, 1);
                indexvalue = base64_decode(closefb_premap[base64_encode(indexkey)]);

                hrt1 = closefb_recursive_hash(rt1, 1);
                closefb_paddinghash(hrt1, indexvalue.length());
                msgrt = xorstring(hexTobinary(indexvalue), hexTobinary(hrt1));
                msgrt = binaryTohex(msgrt);
                rt = msgrt.substr(msgrt.length() - 32);
                encmsg = msgrt.substr(0, msgrt.length() - 32);
                closefb_decryptmap[base64_encode(indexkey)] = base64_encode(encmsg);
            }
        }
        ctr++;
        stw = closefb_recursive_hash(stw, 1);
    }
    // 构造sql语句
    string modified_sql;
    if (!closefb_decryptmap.size())
        modified_sql = "select *from " + tablename + "_index where indexkey in (NULL)";
    else{
        modified_sql = "select *from " + tablename + "_index where indexkey in (";
        for (map<string, string> ::iterator it = closefb_decryptmap.begin(); it != closefb_decryptmap.end(); ++it) 
            modified_sql += "'" + it->first + "',";
        modified_sql[modified_sql.length()-1] = ')';
        modified_sql += ";";
    }
    return modified_sql;
}


