#include <sodium.h>  
#include <gmp.h>
#include <string>
#include "libpq/sse_common.h"

extern bool FB_flag;
extern char* FB_bs;
extern int FB_count;
extern std::string FB_st;
extern std::string FB_ut;
extern std::string FB_ct;

std::string FBserver_sodium_hash(const std::string& str); //ut
void homomorphic_add(mpz_t c_result, const mpz_t c1, const mpz_t c2);
std::string FBserver_xor_strings(const std::string& str1, const std::string& str2) ;