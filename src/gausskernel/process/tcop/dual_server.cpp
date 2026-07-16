#include "dual_server.h"
#include "SSE_time.h"
// dual
int isdual = 0;
int dual_cntind = 0;
map<string, string> dual_decryptmap;

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

string dual_processdelete(string &tablename, string &keyind, string &ind, string &strmaxcnti) {
    /*vector of object*/
    vector<string> label1_vec;
    
    /*string of object*/
    string p_label1, p_ind;
    int maxcnti;

    istringstream iss(strmaxcnti);
    iss >> maxcnti;

    /* Generate label1 values */
    tracker.start("dual_server delete computation");
    string label1, blabel1, strcnti;
    for (int i = 1; i <= maxcnti; i++) {
        ostringstream oss_i;
        oss_i << i;
        strcnti = oss_i.str();
        label1 = dual_hmac_sha256(keyind, strcnti, 1);
        blabel1 = base64_encode(label1);
        label1_vec.push_back(blabel1);
    }
    tracker.stop("dual_server delete computation");

    /* SQL generation */
    p_ind = "['" + ind + "']"; 

    if (!maxcnti)
        p_label1 = "['0']";
    else{
        int label1vecsize = label1_vec.size();
        p_label1 = "[";
        for (int i=0; i<label1vecsize; i++){
            if (i != label1vecsize - 1)
                p_label1 += "'" + label1_vec[i] + "', ";
            else
                p_label1 += "'" + label1_vec[i] + "']";
        }
    }
    string modified_sql = "DO $$\n"
                  "DECLARE\n"
                  "    start_time TIMESTAMP;\n"
                  "    end_time TIMESTAMP;\n"
                  "    duration INTERVAL;\n"
                  "    ind_array TEXT[] := ARRAY" + p_ind + ";\n"
                  "    label1_array TEXT[] := ARRAY" + p_label1 + ";\n"
                  "    table_prefix TEXT := '" + tablename + "';\n"
                  "BEGIN\n"
                  "    start_time := clock_timestamp();\n"
                  "    PERFORM dual_delete(ind_array, label1_array, table_prefix);\n"
                  "    end_time := clock_timestamp();\n"
                  "    duration := end_time - start_time;\n"
                  "    RAISE NOTICE 'dual_delete execution time: %', duration;\n"
                  "END $$;\n";
    return modified_sql;
}

string dual_Decryptind(string &encind_data, string &label3){
    encind_data = base64_decode(encind_data);
    encind_data = hexTobinary(encind_data);
    string binlabel3 = hexTobinary(label3);
    string bind = xorstring(encind_data, binlabel3);
    string ind = binary_to_string(bind);
    return ind;
}