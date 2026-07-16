#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <utility>
#include <random>
#include <iomanip>
#include <gmp.h>
#include <sodium.h>  
#include <unordered_map>

using namespace std;

extern std::string FB_storage_dir_path  ;

struct FB_UpdateRequest
{   
    string ut;
    string e;
    string ct;
};
struct FB_SearchRequest
{   
    string st;
    int count;
};


map<string, pair<int, string>> read_file(const string &filename);
unordered_map<string, pair<int, string>> read_file2(const string &filename);
void FB_write_file(const string &filename, map<string, pair<int, string>> &CT);
void FB_write_file(const string &filename, unordered_map<string, pair<int, string>> &CT);
void append_file(const string &filename, string key, int cnt, string st);

string generate_random_string(size_t length); //st
string sodium_hash(const std::string& str); //ut
std::string xor_strings(const std::string& str1, const std::string& str2); //st ^ st -> ct
std::string generate_binary_string(const std::string& input);
std::vector<int> generate_ind(const std::string& binary_string);
void generate_key(mpz_t key, const std::string &keyword, int counter);
void encrypt(mpz_t c, const mpz_t m, const std::string &keyword, int counter);
void decrypt(mpz_t m, const mpz_t c, const std::string &keyword, int counter);

class FBDSSEClient
{
public:
    static constexpr int N=1000000;
    static constexpr size_t kKeywordIndexSize = 32;
    FBDSSEClient();
	~FBDSSEClient();

    std::string get_and_increment(std::map<std::string, pair<int, string>>& keymap, std::string keyword, int& kw_counter);
    std::string re_get_and_increment(std::map<std::string, pair<int, string>>& keymap, std::string keyword, int& kw_counter);
    std::string get_and_increment(std::unordered_map<std::string, pair<int, string>>& keymap, std::string keyword, int& kw_counter);
    std::string get_and_increment(std::unordered_map<std::string, pair<int, string>>& keymap, std::string keyword, int& kw_counter, bool writefile);
    std::string re_get_and_increment(std::unordered_map<std::string, pair<int, string>>& keymap, std::string keyword, int& kw_counter);
    FB_UpdateRequest FB_insert(string keyword, string ind);
    FB_UpdateRequest FB_insert(string keyword, string ind, bool writefile);
    FB_UpdateRequest FB_re_insert(string keyword, string inds);
    FB_SearchRequest FB_search(string keyword);

private:
	unordered_map<std::string, pair<int, string>> counter_map;  //keyword   count ST_new

};

FBDSSEClient* init_FBDSSEClient();

extern FBDSSEClient* FBdsse_Client;