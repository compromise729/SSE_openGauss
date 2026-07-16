#include "libpq/closefb_client.h"
#include "SSE_time.h"

int closefb_maxctr = 1000;

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

void CUpdate(string &stw, map<string, string> &dicw, string &msg, set<string> &generated_keys){
    string stw0, stw1, hstw1, rt0, rt1, rt, hrt0, hrt1;
    stw0 = stw + "0";
    string key = closefb_recursive_hash(stw0, 1);
    string value = getmapvalue(key, dicw, 0);
    if (value == ""){
        string tmp1(32, '0');
        string tmp = msg + tmp1;
        stw1 = stw + "1";
        hstw1 = closefb_recursive_hash(stw1, 1);
        // padding hash
        closefb_paddinghash(hstw1, tmp.length());
        value = xorstring(hexTobinary(hstw1), hexTobinary(tmp));
        value = binaryTohex(value);
        dicw[key] = value;
    }
    else{
        dicw.erase(key);
        stw1 = stw + "1";
        rt = generate_random_key(32, generated_keys);
        string tmp = msg + rt;
        hstw1 = closefb_recursive_hash(stw1, 1);
        // padding hash
        closefb_paddinghash(hstw1, tmp.length());
        dicw[key] = xorstring(hexTobinary(hstw1), hexTobinary(tmp));
        dicw[key] = binaryTohex(dicw[key]);

        rt0 = rt + "0";
        hrt0 = closefb_recursive_hash(rt0, 1);
        rt1 = rt + "1";
        hrt1 = closefb_recursive_hash(rt1, 1);
        hstw1 = closefb_recursive_hash(stw1, 1);
        // padding hash
        closefb_paddinghash(hrt1, value.length());
        closefb_paddinghash(hstw1, value.length());

        string binhrt1 = hexTobinary(hrt1);
        string binhstw1 = hexTobinary(hstw1);
        string binvalue = hexTobinary(value);

        value = xorstring(binhrt1, xorstring(binhstw1, binvalue));
        value = binaryTohex(value);
        dicw[hrt0] = value;
    }
}

void closefb_processinserttuple(string &init_sql, string &closefb_k1, int &ctr, set<string> &generated_keys, map<string, map<string, string> > &DIC, 
    map<string, string> &KW, vector<string> &ind_vec, vector<string> &keyword_vec, int &flag){

    vector<string> get_indw;
    extract_strings(init_sql, get_indw);

    if (!flag){
        ind_vec.push_back(get_indw[0]);
        keyword_vec.push_back(get_indw[1]);
    }
    string stw = getmapvalue(get_indw[1], KW, 0);

    if (stw == ""){
        string ktw = prf(closefb_k1, get_indw[1], 128);
        stw = closefb_recursive_hash(ktw, ctr);
        KW[get_indw[1]] = stw;
        map<string, string> tmp;
        DIC[get_indw[1]] = tmp;
    }
    string indop;
    if (flag)
        indop = get_indw[0] + "add";
    else
        indop = get_indw[0] + "del";
    int cipherlen = 0;
    unsigned char *encindop = aes_encrypt((unsigned char*)indop.c_str(), strlen(indop.c_str()), cipherlen);
    char *hex_encindop = hex2str(encindop, cipherlen);
    string encryptstr_indop(hex_encindop);
    CUpdate(stw, DIC[get_indw[1]], encryptstr_indop, generated_keys);
    free(hex_encindop);
    free(encindop);
    hex_encindop = NULL;
    encindop = NULL;

}

void closefb_RBprocessinserttuple(string &init_sql, string &closefb_k1, int &ctr, set<string> &generated_keys, map<string, map<string, string> > &DIC, 
    map<string, string> &KW){

    vector<string> get_indw;
    extract_strings(init_sql, get_indw);

    string stw = getmapvalue(get_indw[1], KW, 0);

    if (stw == ""){
        string ktw = prf(closefb_k1, get_indw[1], 128);
        stw = closefb_recursive_hash(ktw, ctr);
        KW[get_indw[1]] = stw;
        map<string, string> tmp;
        DIC[get_indw[1]] = tmp;
    }
    string indop;
    indop = get_indw[0] + "add";
    int cipherlen = 0;
    unsigned char *encindop = aes_encrypt((unsigned char*)indop.c_str(), strlen(indop.c_str()), cipherlen);
    char *hex_encindop = hex2str(encindop, cipherlen);
    string encryptstr_indop(hex_encindop);
    CUpdate(stw, DIC[get_indw[1]], encryptstr_indop, generated_keys);
    free(hex_encindop);
    free(encindop);
    hex_encindop = NULL;
    encindop = NULL;
}

string closefb_processInsert(string &init_sql, string &closefb_k1, int &ctr, int &flag, string &tablename){
    vector<string> tuplevec;
    get_Inserttuple(init_sql, tuplevec);
    int tuplevecsize = tuplevec.size();

    map<string, map<string, string> > DIC;
    map<string, string> KW;

    /*vector of object*/
    vector<string> ind_vec;
    vector<string> keyword_vec;
    vector<string> value_vec;
    vector<string> indexkey_vec;
    vector<string> indexvalue_vec;

    /*string of object*/
    string p_ind = "[", p_keyword = "[", p_value = "[", p_indexkey = "[", p_indexvalue = "[";

    string modified_sql;

    set<string> generated_keys;

    tracker.start("closefb_client  init time");
    string closefb_storekey = tablename + "closefb_storekey.txt";
    readset(closefb_storekey.c_str(), generated_keys);
    tracker.stop("closefb_client  init time");

    tracker.start("closefb_client  insert_delete computation");
    if (closefb_k1 == "")
        closefb_k1 = generate_random_key(32, generated_keys);

    for (int i=0; i<tuplevecsize; i++)
        closefb_processinserttuple(tuplevec[i], closefb_k1, ctr, generated_keys, DIC, KW, ind_vec, keyword_vec, flag);
    
    for (map<string, map<string, string> >::iterator it = DIC.begin(); it != DIC.end(); ++it) 
        for (map<string, string>::iterator it1 = it->second.begin(); it1 != it->second.end(); ++it1) {
               string bindexkey = base64_encode(it1->first), bindexvalue = base64_encode(it1->second);
               indexkey_vec.push_back(bindexkey);
               indexvalue_vec.push_back(bindexvalue);
        }
    tracker.stop("closefb_client  insert_delete computation");
    if (!flag){
        int keywordvecsize = keyword_vec.size();
        for (int i=0;i<keywordvecsize; i++){
            if (i!=keywordvecsize- 1){
                p_ind += "'" + ind_vec[i] + "', ";
                p_keyword += "'" + keyword_vec[i] + "', ";
            }   
            else{
                p_ind += "'" + ind_vec[i] + "']";
                p_keyword += "'" + keyword_vec[i] + "']";
            }
        }
    }
    int indexkeysize = indexkey_vec.size();
    for (int i=0; i < indexkeysize; i++){
        if (i!=indexkeysize - 1){
            p_indexkey += "'" + indexkey_vec[i] + "', ";
            p_indexvalue += "'" + indexvalue_vec[i] + "', ";
        }
        else{
            p_indexkey += "'" + indexkey_vec[i] + "']";
            p_indexvalue += "'" + indexvalue_vec[i] + "']";
        }
    }
    if (flag){
        /* get data_sql */
        string init_sqllower = init_sql;
        transform(init_sqllower.begin(), init_sqllower.end(), init_sqllower.begin(), ::tolower);
        size_t pos = init_sqllower.find("values");
        string data_sql(init_sql, pos, init_sql.length() - pos);
        data_sql.erase(data_sql.end() - 1);
        data_sql = "insert into " + tablename + "_data "+ data_sql;
        data_sql = double_quote_values(data_sql);

        modified_sql = "DO $$\n"
                  "DECLARE\n"
                  "    start_time TIMESTAMP;\n"
                  "    end_time TIMESTAMP;\n"
                  "    duration INTERVAL;\n"
                  "    indexkey_array TEXT[] := ARRAY" + p_indexkey + ";\n"
                  "    indexvalue_array TEXT[] := ARRAY" + p_indexvalue + ";\n"
                  "    table_prefix TEXT := '" + tablename + "';\n"
                  "    data_sql TEXT := '" + data_sql + "';\n"
                  "BEGIN\n"
                  "    start_time := clock_timestamp();\n"
                  "    PERFORM closefb_insert(indexkey_array, indexvalue_array, table_prefix, data_sql);\n"
                  "    end_time := clock_timestamp();\n"
                  "    duration := end_time - start_time;\n"
                  "    RAISE NOTICE 'closefb_insert execution time: %', duration;\n"
                  "END $$;\n";
    }
    else{
        modified_sql = "DO $$\n"
                  "DECLARE\n"
                  "    start_time TIMESTAMP;\n"
                  "    end_time TIMESTAMP;\n"
                  "    duration INTERVAL;\n"
                  "    ind_array TEXT[] := ARRAY" + p_ind + ";\n"
                  "    keyword_array TEXT[] := ARRAY" + p_keyword + ";\n"
                  "    indexkey_array TEXT[] := ARRAY" + p_indexkey + ";\n"
                  "    indexvalue_array TEXT[] := ARRAY" + p_indexvalue + ";\n"
                  "    table_prefix TEXT := '" + tablename + "';\n"
                  "BEGIN\n"
                  "    start_time := clock_timestamp();\n"
                  "    PERFORM closefb_delete(ind_array, keyword_array, indexkey_array, indexvalue_array, table_prefix);\n"
                  "    end_time := clock_timestamp();\n"
                  "    duration := end_time - start_time;\n"
                  "    RAISE NOTICE 'closefb_delete execution time: %', duration;\n"
                  "END $$;\n";
    }
    ctr--;
    writeset(closefb_storekey.c_str(), generated_keys);
    return modified_sql;
}

string closefb_RBprocessInsert(string &init_sql, string &closefb_k1, int &ctr, string &tablename){
    vector<string> tuplevec;
    get_Inserttuple(init_sql, tuplevec);
    int tuplevecsize = tuplevec.size();

    map<string, map<string, string> > DIC;
    map<string, string> KW;

    /*vector of object*/
    vector<string> indexkey_vec;
    vector<string> indexvalue_vec;

    string modified_sql;

    set<string> generated_keys;

    string closefb_storekey = tablename + "closefb_storekey.txt";
    readset(closefb_storekey.c_str(), generated_keys);

    if (closefb_k1 == "")
        closefb_k1 = generate_random_key(32, generated_keys);

    for (int i=0; i<tuplevecsize; i++)
        closefb_RBprocessinserttuple(tuplevec[i], closefb_k1, ctr, generated_keys, DIC, KW);
    

    for (map<string, map<string, string> >::iterator it = DIC.begin(); it != DIC.end(); ++it) 
        for (map<string, string>::iterator it1 = it->second.begin(); it1 != it->second.end(); ++it1) {
               string bindexkey = base64_encode(it1->first), bindexvalue = base64_encode(it1->second);
               indexkey_vec.push_back(bindexkey);
               indexvalue_vec.push_back(bindexvalue);
        }

    int indexkeysize = indexkey_vec.size();
    modified_sql = "insert into " + tablename + "_index values ";
    for (int i=0; i < indexkeysize; i++)
        modified_sql += "('" + indexkey_vec[i] + "', '" + indexvalue_vec[i] + "'),";
    modified_sql[modified_sql.length() - 1] = ';';
    writeset(closefb_storekey.c_str(), generated_keys);
    return modified_sql;
}

int closefb_readstatus(const char *file_path, vector<string> &status){
    pthread_mutex_lock(&file_mutex);
    string path=storage_dir_path+ "/"+file_path;
    FILE *file = fopen(path.c_str(), "r"); 
    if (!file) {
        cerr << "Unable to open file:"<<path+"\n";
        pthread_mutex_unlock(&file_mutex);  
        return 1;  
    }

    char buffer[256];  
    while (fgets(buffer, sizeof(buffer), file)) {  
        char data[256];
        if (sscanf(buffer, "%s", data) == 1) {  
            string data_str(data);
            status.push_back(data_str);
        }
    }
    if (ferror(file)) {
        cerr << "Error occurred while reading the file\n";
        fclose(file);  
        pthread_mutex_unlock(&file_mutex); 
        return 1;
    }

    fclose(file);  
    pthread_mutex_unlock(&file_mutex); 
    return 0;
}

int closefb_writestatus(const char *file_path, vector<string> &status){
    pthread_mutex_lock(&file_mutex);  
    string path=storage_dir_path+ "/"+file_path;
    FILE *file = fopen(path.c_str(), "w");  
    if (!file) {
        cerr << "Unable to open file";
        pthread_mutex_unlock(&file_mutex);  
        return 1;  
    }

    for (int i=0;i<status.size();i++){
        string line = status[i] + "\n";
        fwrite(line.c_str(), sizeof(char), line.size(), file);
    }

    fflush(file);  
    fclose(file);  
    pthread_mutex_unlock(&file_mutex);  
    return 0;
}