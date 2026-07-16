#include "libpq/Client_Mitra.h"
#include <string.h>
#include<iostream>
#include "libpq/sse_common.h"
#include <../../../sse/Mitra/third_party/crypto/src/prg.hpp>

using namespace std;



MitraClient::MitraClient(bool deleteFiles) {
    setupMode = false;
    this->deleteFiles = deleteFiles;
    
    // FileCnt.reserve(120000); // 比预期多预留20%空间，减少rehash可能性
// 设置负载因子，避免频繁rehash
    // FileCnt.max_load_factor(0.7);
    // FileCnt = loadDictionary2("/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
    FileCnt = loadDictionary("/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
    // SrcCnt = loadDictionary2("/opt/og/openGauss-server/sse_storage/mitra_srccnt.mat");
    SrcCnt = loadDictionary("/opt/og/openGauss-server/sse_storage/mitra_srccnt.mat");
}

MitraClient::~MitraClient() {
    
}

void MitraClient::updateRequest(OP op, string keyword, int ind, prf_type& addr, prf_type& val) {
    prf_type k_w;
    memset(k_w.data(), 0, AES_KEY_SIZE);
    copy(keyword.begin(), keyword.end(), k_w.data());//复制keyword到 k_w
    int fileCnt = 0, srcCnt = 0;

    if (FileCnt.find(keyword) == FileCnt.end()) {
        FileCnt[keyword] = 1;
        fileCnt = 1;
        appendDictionaryEntry(keyword,1, "/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
    } else {
        FileCnt[keyword]++;
        fileCnt = FileCnt[keyword];
        writeDictionary(FileCnt, "/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
    }
    if (deleteFiles) {
        if (SrcCnt.find(keyword) == SrcCnt.end()) {
            SrcCnt[keyword] = 0;
            srcCnt = 0;
        } else {
            srcCnt = SrcCnt[keyword];
        }
    }
    prf_type rnd;
    getAESRandomValue(k_w.data(), 0, srcCnt, fileCnt, addr.data());
    getAESRandomValue(k_w.data(), 1, srcCnt, fileCnt, rnd.data());
    val = bitwiseXOR(ind, op, rnd);
    totalUpdateCommSize = (sizeof (prf_type) * 2);
}

void MitraClient::updateRequest(OP op, string keyword, int ind, prf_type& addr, prf_type& val, bool writefile) 
{
    prf_type k_w;
    memset(k_w.data(), 0, AES_KEY_SIZE);
    copy(keyword.begin(), keyword.end(), k_w.data());//复制keyword到 k_w
    int fileCnt = 0, srcCnt = 0;

    if (FileCnt.find(keyword) == FileCnt.end()) {
        FileCnt[keyword] = 1;
        fileCnt = 1;
        // appendDictionaryEntry(keyword,1, "/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
    } else {
        FileCnt[keyword]++;
        fileCnt = FileCnt[keyword];
        // writeDictionary(FileCnt, "/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
    }
    if(writefile)
    {
        writeDictionary(FileCnt, "/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
    }
    if (deleteFiles) {
        if (SrcCnt.find(keyword) == SrcCnt.end()) {
            SrcCnt[keyword] = 0;
            srcCnt = 0;
        } else {
            srcCnt = SrcCnt[keyword];
        }
    }
    prf_type rnd;
    getAESRandomValue(k_w.data(), 0, srcCnt, fileCnt, addr.data());
    getAESRandomValue(k_w.data(), 1, srcCnt, fileCnt, rnd.data());
    val = bitwiseXOR(ind, op, rnd);
    totalUpdateCommSize = (sizeof (prf_type) * 2);
}

prf_type MitraClient::searchRequest(string keyword, vector<string>& KList) {//用这个
    totalSearchCommSize = 0;
    prf_type k_w;
    memset(k_w.data(), 0, AES_KEY_SIZE);
    copy(keyword.begin(), keyword.end(), k_w.data());
    if (FileCnt.find(keyword) == FileCnt.end()) {
        return k_w;
    }
    int fileCnt = FileCnt[keyword], srcCnt = 0;
    if (deleteFiles) {
        srcCnt = SrcCnt[keyword];
        // writeDictionary(SrcCnt, "/opt/og/openGauss-server/sse_storage/mitra_srccnt.mat");
    }
    // KList.reserve(fileCnt);//预分配空间

    for (int i = 1; i <= fileCnt; i++) {
        prf_type rnd;
        getAESRandomValue(k_w.data(), 0, srcCnt, i, rnd.data());
        KList.push_back(base64_encode(string(rnd.begin(),rnd.end())));
    }
    totalSearchCommSize += sizeof (prf_type) * KList.size();
    return k_w;
}

void MitraClient::searchProcess(vector<prf_type> encIndexes, string keyword, vector<int>& finalRes, vector<prf_type>& addrs, vector<prf_type>& vals) {//解密和重加密
    map<int, int> remove;
    int srcCnt = 0;
    int fileCnt = 0;
    prf_type k_w;
    memset(k_w.data(), 0, AES_KEY_SIZE);
    copy(keyword.begin(), keyword.end(), k_w.data());
    if (deleteFiles) {
        srcCnt = SrcCnt[keyword];
    }
    finalRes.reserve(encIndexes.size());
    int cnt = 1;
    for (auto i = encIndexes.begin(); i != encIndexes.end(); i++) {
        prf_type tmp;
        getAESRandomValue(k_w.data(), 1, srcCnt, cnt, tmp.data());
        prf_type decodedString = *i;
        prf_type plaintextBytes = bitwiseXOR(tmp, decodedString);
        int plaintext = (*((int*) &plaintextBytes[0]));
        remove[plaintext] += (2 * plaintextBytes[4] - 1);//如果有del的项，remove字典里这一项对应的值就会变，从而下面过滤掉
        cnt++;
    }
    if (deleteFiles) {
        SrcCnt[keyword]++;
        srcCnt++;
        writeDictionary(SrcCnt, "/opt/og/openGauss-server/sse_storage/mitra_srccnt.mat");
    }
    for (auto const& cur : remove) {
        if (cur.second < 0) {//过滤掉删除的
            finalRes.emplace_back(cur.first);
            if (deleteFiles) {
                fileCnt++;
                prf_type addr, rnd;
                getAESRandomValue(k_w.data(), 0, srcCnt, fileCnt, addr.data());
                getAESRandomValue(k_w.data(), 1, srcCnt, fileCnt, rnd.data());
                prf_type val = bitwiseXOR(cur.first, OP::INS, rnd);
                addrs.push_back(addr);
                vals.push_back(val);
            }
        }
    }
    if (deleteFiles) {
        FileCnt[keyword] = fileCnt;
        writeDictionary(FileCnt, "/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
        totalSearchCommSize += (fileCnt * 2 * sizeof (prf_type));
    }
    totalSearchCommSize += encIndexes.size() * sizeof (prf_type);
}
//解密和重加密
void MitraClient::searchProcess(vector<prf_type> encIndexes, vector<string> join_value, string keyword, vector<int>& finalRes, vector<string>& final_join_value, vector<prf_type>& addrs, vector<prf_type>& vals) {//解密和重加密
    map<int, int> remove;
    int srcCnt = 0;
    int fileCnt = 0;
    prf_type k_w;
    memset(k_w.data(), 0, AES_KEY_SIZE);
    copy(keyword.begin(), keyword.end(), k_w.data());
    if (deleteFiles) {
        srcCnt = SrcCnt[keyword];
    }
    finalRes.reserve(encIndexes.size());
    int cnt = 1;
    for (auto i = encIndexes.begin(); i != encIndexes.end(); i++) {
        prf_type tmp;
        getAESRandomValue(k_w.data(), 1, srcCnt, cnt, tmp.data());
        prf_type decodedString = *i;
        prf_type plaintextBytes = bitwiseXOR(tmp, decodedString);
        int plaintext = (*((int*) &plaintextBytes[0]));
        remove[plaintext] += (2 * plaintextBytes[4] - 1);//如果有del的项，remove字典里这一项对应的值就会变，从而下面过滤掉
        cnt++;
    }
    if (deleteFiles) {
        SrcCnt[keyword]++;
        srcCnt++;
        writeDictionary(SrcCnt, "/opt/og/openGauss-server/sse_storage/mitra_srccnt.mat");
    }
    int joincnt=0;
    for (auto const& cur : remove) {
        if (cur.second < 0) {//过滤掉删除的
            finalRes.emplace_back(cur.first);
            final_join_value.push_back(join_value[joincnt]);
            if (deleteFiles) {
                fileCnt++;
                prf_type addr, rnd;
                getAESRandomValue(k_w.data(), 0, srcCnt, fileCnt, addr.data());
                getAESRandomValue(k_w.data(), 1, srcCnt, fileCnt, rnd.data());
                prf_type val = bitwiseXOR(cur.first, OP::INS, rnd);
                addrs.push_back(addr);
                vals.push_back(val);
            }
        }
        joincnt++;
    }
    if (deleteFiles) {
        FileCnt[keyword] = fileCnt;
        writeDictionary(FileCnt, "/opt/og/openGauss-server/sse_storage/mitra_filecnt.mat");
        totalSearchCommSize += (fileCnt * 2 * sizeof (prf_type));
    }
    totalSearchCommSize += encIndexes.size() * sizeof (prf_type);
}

prf_type MitraClient::bitwiseXOR(int input1, int op, prf_type input2) {
    prf_type result;
    result[3] = input2[3] ^ ((input1 >> 24) & 0xFF);
    result[2] = input2[2] ^ ((input1 >> 16) & 0xFF);
    result[1] = input2[1] ^ ((input1 >> 8) & 0xFF);
    result[0] = input2[0] ^ (input1 & 0xFF);
    result[4] = input2[4] ^ (op & 0xFF);
    for (int i = 5; i < AES_KEY_SIZE; i++) {
        result[i] = (129 % 255) ^ input2[i];
    }
    return result;
}

prf_type MitraClient::bitwiseXOR(prf_type input1, prf_type input2) {
    prf_type result;
    for (unsigned int i = 0; i < input2.size(); i++) {
        result[i] = input1.at(i) ^ input2[i];
    }
    return result;
}

void MitraClient::getAESRandomValue(unsigned char* keyword, int op, int srcCnt, int fileCnt, unsigned char* result) {
    if (deleteFiles) {
        *(int*) (&keyword[AES_KEY_SIZE - 9]) = srcCnt;
    }
    keyword[AES_KEY_SIZE - 5] = op & 0xFF;
    *(int*) (&keyword[AES_KEY_SIZE - 4]) = fileCnt;
    sse::crypto::Prg::derive((unsigned char*) keyword, 0, AES_KEY_SIZE, result);
}

int MitraClient::getFileCntSize() const {
    return FileCnt.size();
}

bool MitraClient::isSetupMode() const {
    return setupMode;
}

double MitraClient::getTotalSearchCommSize() const {
    return totalSearchCommSize;
}

double MitraClient::getTotalUpdateCommSize() const {
    return totalUpdateCommSize;
}

void MitraClient::setSetupMode(bool setupMode) {
    this->setupMode = setupMode;
}
MitraClient* Mitra_client = nullptr;