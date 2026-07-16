#include "libpq/dual_client.h"
#include"SSE_time.h"
string master_key = "bcd60b1e3af4a91d5d52834218e89d7f";

// TimeTracker& tracker = TimeTracker::getInstance();

string dual_hmac_sha256(const string& key, const string& message, int flag) {
    unsigned char* result;
    unsigned int len = SHA256_DIGEST_LENGTH;
    string modified_message;
    if(flag==1)
        modified_message = message + "HMAC1";
    else if(flag==2)
        modified_message = message + "HMAC2";
    else
        modified_message = message + "HMAC3";
    // Compute HMAC using SHA256
    result = HMAC(EVP_sha256(), key.c_str(), key.size(),
                  reinterpret_cast<const unsigned char*>(modified_message.c_str()), modified_message.size(), 
                  NULL, NULL);

    // Convert the result to hexadecimal format
    ostringstream oss;
    for (unsigned int i = 0; i < len; i++) {
        oss << hex << setw(2) << setfill('0') << (int)result[i];
    }
    return oss.str();
}



/*merge label1, label2 and data to a tuple*/
string mergeToTuple(string label1, string label2, string data){
    string tuple;
    tuple+="('"+label1+"', '"+label2+"', '"+data+"')";
    return tuple;
}


void dual_processinserttuple(string &dual_sql, set<string> &generated_keys, map<string, string> &map_ukey, map<string, string> &map_ucnt, 
    vector<string> &label1_vec, vector<string> &label2_vec, vector<string> &data_vec, map<string, int> &ind_cnt_map) {
    /* Extract strings from dual_sql */
    vector<string> get_indw;
    extract_strings(dual_sql, get_indw);
    int indw_size = get_indw.size();

    /* Construct base insert SQL */
    string ind = get_indw[0];
    /* Convert ind to binary and compute keyind */
    string bind = string_to_binary(ind), keyind = prf(master_key, ind, 128);

    /* Declare label1, label2, encdata_ind and their base64 versions */
    string label1, label2, label3, encdata_ind;
    string blabel1, blabel2, bencdata_ind;

    int ind_cnt;

    if (ind_cnt_map.find(ind) == ind_cnt_map.end())
        ind_cnt_map[ind] = 1;
    else
        ind_cnt_map[ind] += 1;
    ind_cnt = ind_cnt_map[ind];
    
    /* Declarations */
    ostringstream oss_i, oss_w;
    int ucntw, bind_len, binlabel3_len;
    string binlabel3;
    string strcnti, strUkeyw, strUcntw;
    /* Compute label1 */
    oss_i << ind_cnt;
    strcnti = oss_i.str();
    label1 = dual_hmac_sha256(keyind, strcnti, 1);

    /* Compute label2 */
    strUkeyw = getmapvalue(get_indw[1], map_ukey, 0);
    if (strUkeyw == "") {
        strUkeyw = generate_random_key(32, generated_keys);
        map_ukey[get_indw[1]] = strUkeyw;
    }

    strUcntw = getmapvalue(get_indw[1], map_ucnt, 1);

    /* Read and update ucntw */
    istringstream iss(strUcntw);
    iss >> ucntw;
    ucntw += 1;
    oss_w << ucntw;
    strUcntw = oss_w.str();
    map_ucnt[get_indw[1]] = strUcntw;
    label2 = dual_hmac_sha256(strUkeyw, strUcntw, 2);

    /* Compute encdata_ind */
    label3 = dual_hmac_sha256(strUkeyw, strUcntw, 3);
    binlabel3 = hexTobinary(label3);
    bind_len = bind.length();
    binlabel3_len = binlabel3.length();

    if (bind_len != binlabel3_len)
        cerr << "Length is not matching\n";

    encdata_ind = xorstring(bind, binlabel3);
    encdata_ind = binaryTohex(encdata_ind);

    /* Base64 encoding */
    blabel1 = base64_encode(label1);
    blabel2 = base64_encode(label2);
    bencdata_ind = base64_encode(encdata_ind);

    label1_vec.push_back(blabel1);
    label2_vec.push_back(blabel2);
    data_vec.push_back(bencdata_ind);  
}

string dual_processInsert(string &dual_sql, string &tablename){
    tracker.start("dual_client Rewrite insertion SQL time");
    vector<string> tuplevec;
    tracker.start("dual insert extract val");
    get_Inserttuple(dual_sql, tuplevec);
    tracker.stop("dual insert extract val");

    int tuplevecsize = tuplevec.size();
    /* Define maps */
    map<string, string> map_ukey; 
    map<string, string> map_ucnt; 
    /* Define keys*/
    set<string> generated_keys;

    /* 文件名 */
    string dual_ukey = tablename + "dual_ukey.txt";
    string dual_ucnt = tablename + "dual_ucnt.txt";
    string dual_storekey = tablename + "dual_storekey.txt";

    tracker.start("dual_client insert init");
    /* Read maps into memory */
    readmap(dual_ukey.c_str(), map_ukey);
    readmap(dual_ucnt.c_str(), map_ucnt);

    /* Read keys*/
    readset(dual_storekey.c_str(), generated_keys);
    tracker.stop("dual_client insert init");

    /*vector of object*/
    vector<string> label1_vec;
    vector<string> label2_vec;
    vector<string> data_vec;

    /*string of object*/
    string p_label1 = "[", p_label2 = "[", p_data = "[";

    /*get data_sql*/
    string dual_sqllower = dual_sql;
    transform(dual_sqllower.begin(), dual_sqllower.end(), dual_sqllower.begin(), ::tolower);
    size_t pos = dual_sqllower.find("values");
    string data_sql(dual_sql, pos, dual_sql.length() - pos);
    data_sql.erase(data_sql.end() - 1);
    data_sql = "insert into "+tablename+"_data "+data_sql;
    data_sql = double_quote_values(data_sql);

    map<string, int> ind_cnt_map;
    tracker.start("dual_client Insert computation time");
    for (int i=0; i<tuplevecsize; i++)
        dual_processinserttuple(tuplevec[i], generated_keys, map_ukey, map_ucnt, label1_vec, label2_vec, data_vec, ind_cnt_map);
    tracker.stop("dual_client Insert computation time");

    // tracker.start("dual_client Rewrite insertion SQL time");
    int label1vecsize = label1_vec.size();
    for (int i=0; i<label1vecsize; i++){
        if (i!=label1vecsize - 1){
            p_label1 += "'" + label1_vec[i] + "', ";
            p_label2 += "'" + label2_vec[i] + "', ";
            p_data += "'" + data_vec[i] + "', ";
        }
        else{
            p_label1 += "'" + label1_vec[i] + "']";
            p_label2 += "'" + label2_vec[i] + "']";
            p_data += "'" + data_vec[i] + "']";
        }
    }
    string modified_sql = "DO $$\n"
                  "DECLARE\n"
                  "    start_time TIMESTAMP;\n"
                  "    end_time TIMESTAMP;\n"
                  "    duration INTERVAL;\n"
                  "    label1_array TEXT[] := ARRAY" + p_label1 + ";\n"
                  "    label2_array TEXT[] := ARRAY" + p_label2 + ";\n"
                  "    data_array TEXT[] := ARRAY" + p_data + ";\n"
                  "    table_prefix TEXT := '" + tablename + "';\n"
                  "    data_sql TEXT := '" + data_sql + "';\n"
                  "BEGIN\n"
                  "    start_time := clock_timestamp();\n"
                  "    PERFORM dual_insert(label1_array, label2_array, data_array, table_prefix, data_sql);\n"
                  "    end_time := clock_timestamp();\n"
                  "    duration := end_time - start_time;\n"
                  "    RAISE NOTICE 'dual_insert execution time: %', duration;\n"
                  "END $$;\n";
    tracker.stop("dual_client Rewrite insertion SQL time");

    /* Write maps and seed back to disk */
    tracker.start("dual_client  insert UPDATE state");
    writemap(dual_ukey.c_str(), map_ukey);
    writemap(dual_ucnt.c_str(), map_ucnt);
    writeset(dual_storekey.c_str(), generated_keys);
    tracker.stop("dual_client  insert UPDATE state");
    return modified_sql;
}

string dual_getdtoken(string &dual_sql, string &tablename){
    /* Extract strings from dual_sql */
    tracker.start("dual_client  delete SQL rewrite1");
    vector<string> get_indw;
    extract_strings(dual_sql, get_indw);

    string ind = get_indw[0];

    /* Define keyind */
    string keyind;

    /* 传递dtoken */
    tracker.start("dual_client  delete computation1");
    keyind = prf(master_key, ind, 128);
    tracker.stop("dual_client  delete computation1");
    string modified_sql = "dual_getdtoken (tablename = '"+tablename + "', keyind = '" + keyind +"');";
    tracker.stop("dual_client  delete SQL rewrite1");
    return modified_sql;
}

string dual_delete(string &dual_sql, string &tablename){
    /* Extract strings from dual_sql */
    tracker.start("dual_client  delete SQL rewrite2");
    vector<string> get_indw;
    extract_strings(dual_sql, get_indw);

    string ind = get_indw[0];

    /* Define keyind */
    string keyind;

    tracker.start("dual_client  delete computation2");
    keyind = prf(master_key, ind, 128);
    tracker.stop("dual_client  delete computation2");
    string modified_sql = "dual_delete (tablename = '"+tablename + "', keyind = '" + keyind +"', ind = '" + ind + "');";
    tracker.start("dual_client  delete SQL rewrite2");
    return modified_sql;
}