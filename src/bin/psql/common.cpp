/*
 * psql - the PostgreSQL interactive terminal
 *
 * Copyright (c) 2000-2012, PostgreSQL Global Development Group
 *
 * src/bin/psql/common.c
 */
#include "libpq/sse_common.h"
#include "libpq/Client_Sophos.h"
#include "libpq/Client_Mitra.h"
#include "libpq/Client_FBDSSE.h"
#include "libpq/dual_client.h"
#include "libpq/closefb_client.h"
#include "libpq/januspp_client.h"
#include "libpq/SSE_map.h"
#include "SSE_time.h"
#include <gmp.h>
#include "settings.h"
#include "postgres_fe.h"
#include "common.h"

#include <ctype.h>
#include <signal.h>
#ifndef WIN32
#include <unistd.h>   /* for write() */
#include <sys/mman.h> /* for mmap/munmap */
#else
#include <io.h> /* for _write() */
#include <win32.h>
#endif

#include "portability/instr_time.h"
#include "pqsignal.h"
#include "command.h"
#include "copy.h"
#include "mbprint.h"
#include <sys/wait.h>
#include <poll.h>


// #include "libpq/libpq-fe.h"
// #include "libpq/libpq-int.h"
#ifndef WIN32
// #include "libpq/libpq-int.h"
#include "libpq/libpq-fe.h"
#include "libpq/libpq-int.h"
#endif

using namespace std;
bool canAddHist = true;
SSEflag common_sseflag;
SSEdelflag common_delsseflag;
// TimeTracker& tracker = TimeTracker::getInstance();
/*dual select operation*/
static string dual_processselect(string &dual_sql, string &tablename);

/* closefb select operation */
static string closefb_processselect(string &closefb_sql, string &tablename);
/* closefb rebuildEDB */
static void closefb_rebuildEDB(string &closefb_k1, int &ctr, string &tablename);
/*januspp select operation*/
static string januspp_processselect(string &januspp_sql, string &table_name);

static bool ExecQueryUsingCursor(const char* query, double* elapsed_msec);
static bool command_no_begin(const char* query);
static bool is_select_command(const char* query);
static void RecordGucStmt(PGresult* results, const char* query);
static void set_proc_title();
static bool is_explain_command(const char* query);
static int file_lock(int fd, unsigned int operation);
static bool CheckPoolerConnectionStatus();

static void set_searchpath_for_tmptbl(PGconn* conn);
static bool AcceptResult(const PGresult* result, bool print_error = true);
bool GetPrintResult(PGresult** results, bool is_explain, bool is_print, const char* query, bool print_error = true);

/* Mutexes for child processes in parallel mode.
 * When a child gets a result successfully, it try to print the result to "pset.queryFout".
 * All the results should be printed by sequence, so a traffic light is needed.
 * We used a file lock(of pset.queryFout) before, but in some unknown cases, the stdout/stderr
 * may be locked by other processes. Even though the owner has exited, the lock is not released

 * correctly, which blocks all the child processes.
 * So here we use a mutex instead.
 */
struct parallelMutex_t {
    pthread_mutex_t mut;
    pthread_mutexattr_t mutAttr;
};

static int CreateMutexForParallel();
static int LockMutexForParallel();
static int UnlockMutexForParallel();
static int DestroyMutexForParallel();

#define IsInteractiveMode() ((stdout == pset.queryFout) || (stderr == pset.queryFout))

static string dual_processselect(string &dual_sql, string &tablename) {
    tracker.start("dual_client Rewrite select SQL all time----------");
    tracker.start("dual_client Rewrite select SQL time");
    const char *query;
    PGresult* results = NULL;
    string modified_sql;

    map<string, string> map_cnt;
    map<string, string> map_ucnt;
    map<string, string> map_key;
    map<string, string> map_ukey;

    set<string> generated_keys;

    vector<string> getw;
    string strcnt, strucnt;
    string searchw, tmp;
    int cnt = 0, ucnt = 0;

    string dual_cnt = tablename + "dual_cnt.txt";
    string dual_ucnt = tablename + "dual_ucnt.txt";
    string dual_key = tablename + "dual_key.txt";
    string dual_ukey = tablename + "dual_ukey.txt";
    string dual_storekey = tablename + "dual_storekey.txt";

    tracker.start("dual_client search init");
    readmap(dual_cnt.c_str(), map_cnt);
    readmap(dual_ucnt.c_str(), map_ucnt);
    readmap(dual_key.c_str(), map_key);
    readmap(dual_ukey.c_str(), map_ukey);
    readset(dual_storekey.c_str(), generated_keys);
    tracker.stop("dual_client search init");

    tracker.start("dual_client search computation");
    extract_strings(dual_sql, getw);
    searchw = getw[0];
    strcnt = getmapvalue(searchw, map_cnt, 1);
    strucnt = getmapvalue(searchw, map_ucnt, 1);
    istringstream isscnt(strcnt);
    isscnt >> cnt;
    istringstream issucnt(strucnt);
    issucnt >> ucnt;

    /* get strukey and strkey */
    string strkey = getmapvalue(searchw, map_key, 0);
    string strukey = getmapvalue(searchw, map_ukey, 0);
    tracker.stop("dual_client search computation");

    /* nkey and init map */
    string nkey = generate_random_key(32, generated_keys);

    /*传递search token*/
    string searchtoken = "dual_getsearchtoken (tablename = '"+tablename + "', key = '" + strkey +"', cnt = '" + strcnt + "', ukey = '" + strukey + "', ucnt = '"
    + strucnt + "', nkey = '" + nkey + "');";
    query = strdup(searchtoken.c_str());
    tracker.stop("dual_client Rewrite select SQL time");
    results = PQexec(pset.db, query);
    int resultsize = results->ntups;

    /* vector of object */
    tracker.start("dual_client Rewrite select_delete SQL time");
    vector<string> ind_vec;
    string p_ind = "ARRAY[";

    string deletecurrent_sql = "delete from " + tablename + "_index where label2 in (";
    string reinsert_sql = "dual_reinsert values ";
    for (int i=0; i<resultsize;i++){
        string label1(results->tuples[i][0].value);
        string label2(results->tuples[i][1].value);
        string ind(results->tuples[i][2].value);//获得解密后的ind
        ind_vec.push_back(ind);
        if (i==resultsize-1){
            reinsert_sql += "('" + label1 + "', '" + ind + "')";
            deletecurrent_sql += "'" + label2 + "');";
        }
        else{
            reinsert_sql += "('" + label1 + "', '" + ind + "'), ";
            deletecurrent_sql += "'" + label2 + "', ";
        }
    }
    tracker.stop("dual_client Rewrite select_delete SQL time");
    PQclear(results);
    
    if (resultsize) 
    {
        // 执行删除语句
        query = strdup(deletecurrent_sql.c_str());
        results = PQexec(pset.db, query);
        PQclear(results);

        // 执行重插入语句
        tracker.start("dual_client Rewrite select_reinsert SQL time");
        reinsert_sql += "where tablename = '" + tablename + "', nkey = '" + nkey + "';";
        query = strdup(reinsert_sql.c_str());
        results = PQexec(pset.db, query);
        PQclear(results);
        ostringstream oss;
        oss<<resultsize;
        string strncnt = oss.str();
        map_cnt[searchw] = strncnt;
        map_ucnt[searchw] = "0";
        map_ukey[searchw] = generate_random_key(32, generated_keys);//生成下一次插入的key
        map_key[searchw] = nkey;
        writemap(dual_ukey.c_str(), map_ukey);
        writemap(dual_cnt.c_str(), map_cnt);
        writemap(dual_ucnt.c_str(), map_ucnt);
        writemap(dual_key.c_str(), map_key);
        writeset(dual_storekey.c_str(), generated_keys);
        for (int i = 0; i < resultsize; i++) {
            if (i != resultsize - 1) {
                p_ind += "'" + ind_vec[i] + "', ";
            } else {
                p_ind += "'" + ind_vec[i] + "']";
                modified_sql = "select * from dual_select(";
                auto it = sse_table_map.find(tablename);
                string recordcol = "t(";
                for (const auto& col : it->second) 
                    recordcol += col.col_name + " TEXT,";
                recordcol.erase(recordcol.end()-1);
                recordcol += ");";
                modified_sql += p_ind + ", '"  + tablename + "') AS " + recordcol;
            }
        }
    } else 
        modified_sql = "select * from " + tablename + "_data where ind in (NULL);";
    tracker.stop("dual_client Rewrite select_reinsert SQL time");
    tracker.stop("dual_client Rewrite select SQL all time----------");
    return modified_sql;
}

/* closefb select operation */
static string closefb_processselect(string &closefb_sql, string &tablename) {
    const char *query;
    PGresult* results = NULL;
    string modified_sql;

    string closefb_k1, strctr;
    int ctr, status_size;
    
    vector<string> status;
    string w, ktw, stw;
    
    tracker.start("closefb_client select init time");
    string closefb_status = tablename + "closefb_status.txt";
    closefb_readstatus(closefb_status.c_str(), status);
    status_size = status.size();
    tracker.stop("closefb_client select init time");
    
    tracker.start("closefb_client Rewrite select SQL time");
    if (!status_size) {
        modified_sql = "select * from " + tablename + "_data where ind in (NULL);";
    } 
    else 
    {
        /*preselect*/
        string preselect = "closefb_preselect (tablename = '" + tablename + "');";
        query = strdup(preselect.c_str());
        results = PQexec(pset.db, query);
        PQclear(results);

        closefb_k1 = status[0];
        strctr = status[1];
        istringstream iss(strctr);
        iss >> ctr;
        
        vector<string> getw;
        extract_strings(closefb_sql, getw);
        w = getw[0];
        
        if (!ctr) ctr = 1;
        
        ktw = prf(closefb_k1, w, 128);
        stw = closefb_recursive_hash(ktw, ctr);
        
        string tmpctr;
        ostringstream oss;
        oss << ctr;
        tmpctr = oss.str();
        string searchtoken = "closefb_getsearchtoken (tablename = '" + tablename + "', stw = '" + stw + "', ctr = '" + tmpctr + "');";
        query = strdup(searchtoken.c_str());
        results = PQexec(pset.db, query);
        int resultsize = results->ntups;
        
        map<string, int> indres;
        tracker.start("closefb_client select computation time");
        for (int i = 0; i < resultsize; i++) {
            string encmsg(results->tuples[i][1].value);
            encmsg = base64_decode(encmsg);
            char *indop = (char *)malloc(strlen(encmsg.c_str()) + 1);
            strcpy(indop, encmsg.c_str());
            int plainindop_len = 0;
            unsigned char *plainindop = aes_decrypt(str2hex(indop), strlen(indop) / 2, plainindop_len);
            indop[plainindop_len] = '\0';
            memcpy(indop, (char *)plainindop, plainindop_len);
            
            string strindop(indop);
            string ind = strindop.substr(0, strindop.length() - 3);
            string op = strindop.substr(strindop.length() - 3);
            
            if (indres.find(ind) == indres.end())
                indres[ind] = 0;
            
            if (op == "add")
                indres[ind]++;
            else
                indres[ind] --;
            free(plainindop);
            free(indop);
            indop = NULL;
            plainindop = NULL;
        }
        tracker.stop("closefb_client select computation time");
        PQclear(results);
        
        vector<string> ind_vec;
        string p_ind = "ARRAY[";
        for (auto it = indres.begin(); it != indres.end(); ++it) {
            if (it->second > 0)
                ind_vec.push_back(it->first);
        }
        
        if (ind_vec.empty()) {
            modified_sql = "select * from " + tablename + "_data where ind in (NULL);";
        } else {
            for (size_t i = 0; i < ind_vec.size(); i++) {
                p_ind += "'" + ind_vec[i] + "'" + (i != ind_vec.size() - 1 ? ", " : "]");
            }
            auto it = sse_table_map.find(tablename);
            string recordcol = "t(";
            for (const auto& col : it->second) 
                recordcol += col.col_name + " TEXT,";
            recordcol.erase(recordcol.end()-1);
            recordcol += ");";
            modified_sql = "select * from closefb_select(" + p_ind + ", '" + tablename + "') AS " + recordcol; //recordcol 列名还原
        }
        tracker.stop("closefb_client Rewrite select SQL time");
    }
    return modified_sql;
}

/* januspp select operation*/
static string januspp_processselect(string &januspp_sql, string &tablename){
    const char *query;
    PGresult* results = NULL;

    vector<string> getw; //w
    extract_strings(januspp_sql, getw);
    string w = getw[0], modified_sql;

    unordered_map<string, pair<string, string> > storeskhex;

    tracker.start("januspp_client select init time");
    string januspp_skhex = tablename + "januspp_skhex.txt";
    string januspp_keywordstage = tablename + "januspp_keywordstage.txt";
    string januspp_storekey = tablename + "januspp_storekey.txt";

    readPairmap(januspp_skhex.c_str(), storeskhex);
    string sk0hex, skIhex, sk0, skI, fixediv;
    if (storeskhex.find(w) != storeskhex.end()){
        sk0hex = storeskhex[w].first;
        skIhex = storeskhex[w].second;
    }
    else{
        sk0hex = januspp_sk0hexinit;
        skIhex = januspp_skIhexinit;
    }
    
    // Initialize variables
    sk0 = hexTobytes(sk0hex);
    skI = hexTobytes(skIhex);
    fixediv = hexTobytes(januspp_fixedivhex);
    // Read keyword_stage
    unordered_map<string, pair<string, string>> keyword_stage;
    readPairmap(januspp_keywordstage.c_str(), keyword_stage);

    tracker.stop("januspp_client select init time");
    
    tracker.start("januspp_client Rewrite select SQL time");
    tracker.start("januspp_client select computation time");
    // token 让服务器获得ct和cache中的ind,并且拿到punckey
    string tkn = base64_encode(januspp_crypto_primitives_hmac(januspp_Kshex, w, 256));
    string enc_keyword = base64_encode(januspp_crypto_primitives_hmac(sk0, w, 256));
    // Key recovery
    string msk, delcnt;
    if (keyword_stage.find(enc_keyword) == keyword_stage.end()) {
        msk = januspp_encrypt(januspp_key_trim(sk0), w, fixediv);
        delcnt = "0";
    } else {
        msk = base64_decode(keyword_stage[enc_keyword].first);
        delcnt= keyword_stage[enc_keyword].second;
    }

    tracker.stop("januspp_client select computation time");

    string searchtoken = "januspp_getsearchtoken (tablename = '" + tablename +"', tkn = '" + tkn + "', enc_keyword = '" + enc_keyword + "', curkey = '" + base64_encode(msk) + "', curdeletioncnt = '" + delcnt + "', skIhex = '" + skIhex + "');";
    query = strdup(searchtoken.c_str());
    results = PQexec(pset.db, query);
    PQclear(results);
    
    // 让服务器开始计算结果
    string getresult = "januspp_getresult (tablename = '" + tablename + "');";
    query = strdup(getresult.c_str());
    results = PQexec(pset.db, query);

    // 存储结果
    vector<string> ind_vec;
    for (int i=0;i<results->ntups;i++){
        string ind(results->tuples[i][0].value);
        ind_vec.push_back(ind);
    }
    PQclear(results);

    // 服务器重新初始化cache, 删除ind
    string initcache = "januspp_initcache (tablename = '" + tablename + "');";
    query = strdup(initcache.c_str());
    results = PQexec(pset.db, query);
    PQclear(results);

    // 服务器重插入ind到cache
    string reinsert = "januspp_reinsert (tablename = '" + tablename + "');";
    query = strdup(reinsert.c_str());
    results = PQexec(pset.db, query);
    PQclear(results);

    // Construct SQL query
    int indvec_size = ind_vec.size();
    if (!indvec_size) {
        modified_sql = "select *from " + tablename + "_data where ind in (NULL);";
    } 
    else {
        string p_ind = "ARRAY[";
        for (int i = 0; i < indvec_size; i++) 
            p_ind += "'" + ind_vec[i] + (i != indvec_size - 1 ? "', " : "']");
        auto it = sse_table_map.find(tablename);
        string recordcol = "t(";
        for (const auto& col : it->second) 
            recordcol += col.col_name + " TEXT,";
        recordcol.erase(recordcol.end()-1);
        recordcol += ");";
        modified_sql = "select * from januspp_select(" + p_ind + ", '" + tablename + "') AS " + recordcol;
    }
    /*generate new sk*/
    set<string> generated_keys;
    readset(januspp_storekey.c_str(), generated_keys);
    sk0hex = januspp_generate_random_key(64, generated_keys);
    skIhex = januspp_generate_random_key(64, generated_keys);
    writeset(januspp_storekey.c_str(), generated_keys);
    storeskhex[w] = make_pair(sk0hex, skIhex);
    writePairmap(januspp_skhex.c_str(), storeskhex);

    tracker.stop("januspp_client Rewrite select SQL time");
    return modified_sql;
}


/* closefb rebuildEDB */
static void closefb_rebuildEDB(string &closefb_k1, int &ctr, string &tablename) {
    string modified_sql, selectdata_sql;
    const char *query;
    PGresult* results = NULL, * tmpresults = NULL;

    string nclosefb_k1 = "";
    int nctr = closefb_maxctr;
    
    /*delete EDB*/
    modified_sql = "delete from " + tablename + "_index;";
    query = strdup(modified_sql.c_str());
    results = PQexec(pset.db, query);
    PQclear(results);
    
    /* select from datatable */
    selectdata_sql = "select ind, keyword from " + tablename + "_data;";
    query = strdup(selectdata_sql.c_str());
    results = PQexec(pset.db, query);

    for (int i=0; i<results->ntups; i++){
        string ind(results->tuples[i][0].value);
        string keyword(results->tuples[i][1].value);
        modified_sql = "insert into " + tablename + "values ";
        modified_sql += "('" + ind + "', '" + keyword + "');";
        modified_sql = closefb_RBprocessInsert(modified_sql, nclosefb_k1, nctr, tablename);
        query = strdup(modified_sql.c_str());
        tmpresults = PQexec(pset.db, query);
        PQclear(tmpresults);
    }
    PQclear(results);
    closefb_k1 = nclosefb_k1;
    ctr = nctr - 1;
}

/*
 * "Safe" wrapper around strdup()
 */
char* pg_strdup(const char* string)
{
    char* tmp = NULL;

    if (NULL == string) {
        fprintf(stderr, _("%s: pg_strdup: cannot duplicate null pointer (internal error)\n"), pset.progname);
        exit(EXIT_FAILURE);
    }
    tmp = strdup(string);
    if (NULL == tmp) {
        psql_error("out of memory\n");
        exit(EXIT_FAILURE);
    }
    return tmp;
}

void* pg_malloc(size_t size)
{
    void* tmp = NULL;

    /* Avoid unportable behavior of malloc(0) */
    if (size == 0)
        size = 1;
    tmp = malloc(size);
    if (NULL == tmp) {
        psql_error("out of memory\n");
        exit(EXIT_FAILURE);
    }
    return tmp;
}

void* pg_malloc_zero(size_t size)
{
    void* tmp = NULL;
    errno_t rc = 0;

    tmp = pg_malloc(size);
    rc = memset_s(tmp, size, 0, size);
    check_memset_s(rc);
    return tmp;
}

void* pg_calloc(size_t nmemb, size_t size)
{
    void* tmp = NULL;

    if (nmemb == 0 || size == 0) {
        psql_error("out of memory\n");
        exit(EXIT_FAILURE);
    }
    tmp = calloc(nmemb, size);
    if (NULL == tmp) {
        psql_error("out of memory\n");
        exit(EXIT_FAILURE);
    }
    return tmp;
}

void* psql_realloc(void* ptr, size_t oldSize, size_t newSize)
{
    void* tmp = NULL;
    errno_t rc;

    if (oldSize > newSize) {
        return NULL;
    }

    /* When malloc failed gsql will exit, with no memory leak for ptr. */
    tmp = pg_malloc(newSize);
    if (tmp == NULL) {
        psql_error("out of memory\n");
        exit(EXIT_FAILURE);
    }
    rc = memcpy_s(tmp, newSize, ptr, oldSize);
    securec_check_c(rc, "\0", "\0");

    free(ptr);
    ptr = NULL;
    return tmp;
}

/*
 * setQFout
 * -- handler for -o command line option and \o command
 *
 * Tries to open file fname (or pipe if fname starts with '|')
 * and stores the file handle in pset)
 * Upon failure, sets stdout and returns false.
 */
bool setQFout(const char* fname)
{
    bool status = true;
    char fnametmp[MAXPGPATH] = {'\0'};

    /* Close old file/pipe */
    if ((pset.queryFout != NULL) && pset.queryFout != stdout && pset.queryFout != stderr) {
        if (pset.queryFoutPipe)
            pclose(pset.queryFout);
        else
            fclose(pset.queryFout);
    }
    if (NULL != fname) {
        errno_t err = EOK;
        err = strcpy_s(fnametmp, sizeof(fnametmp), fname);
        check_strcpy_s(err);
    }
    /* If no filename, set stdout */
    if (NULL == fname || fname[0] == '\0') {
        pset.queryFout = stdout;
        pset.queryFoutPipe = false;
    } else if (*fname == '|') {
        canonicalize_path(fnametmp);
        pset.queryFout = popen(fnametmp + 1, "w");
        pset.queryFoutPipe = true;
    } else {
        canonicalize_path(fnametmp);
        pset.queryFout = fopen(fnametmp, "w");
        pset.queryFoutPipe = false;
    }

    if (NULL == (pset.queryFout)) {
        psql_error("%s: %s\n", fname, strerror(errno));
        pset.queryFout = stdout;
        pset.queryFoutPipe = false;
        status = false;
    }

    /* Direct signals */
#ifndef WIN32
    pqsignal(SIGPIPE, pset.queryFoutPipe ? SIG_IGN : SIG_DFL);
#endif

    return status;
}

/*
 * Error reporting for scripts. Errors should look like
 *	 psql:filename:lineno: message
 *
 */
void psql_error(const char* fmt, ...)
{
    va_list ap;

    fflush(stdout);
    if (pset.queryFout != stdout)
        fflush(pset.queryFout);

    if (NULL != pset.inputfile)
        fprintf(stderr, "%s:%s:" UINT64_FORMAT ": ", pset.progname, pset.inputfile, pset.lineno);
    va_start(ap, fmt);
    vfprintf(stderr, _(fmt), ap);
    va_end(ap);
}

/*
 * for backend Notice messages (INFO, WARNING, etc)
 */
void NoticeProcessor(void* arg, const char* message)
{
    (void)arg; /* not used */
    psql_error("%s", message);
}

/*
 * Code to support query cancellation
 *
 * Before we start a query, we enable the SIGINT signal catcher to send a
 * cancel request to the backend. Note that sending the cancel directly from
 * the signal handler is safe because PQcancel() is written to make it
 * so. We use write() to report to stderr because it's better to use simple
 * facilities in a signal handler.
 *
 * On win32, the signal canceling happens on a separate thread, because
 * that's how SetConsoleCtrlHandler works. The PQcancel function is safe
 * for this (unlike PQrequestCancel). However, a CRITICAL_SECTION is required
 * to protect the PGcancel structure against being changed while the signal
 * thread is using it.
 *
 * SIGINT is supposed to abort all long-running psql operations, not only
 * database queries.  In most places, this is accomplished by checking
 * cancel_pressed during long-running loops.  However, that won't work when
 * blocked on user input (in readline() or fgets()).  In those places, we
 * set sigint_interrupt_enabled TRUE while blocked, instructing the signal
 * catcher to longjmp through sigint_interrupt_jmp.  We assume readline and
 * fgets are coded to handle possible interruption.  (XXX currently this does
 * not work on win32, so control-C is less useful there)
 */
volatile bool sigint_interrupt_enabled = false;

sigjmp_buf sigint_interrupt_jmp;

static PGcancel* volatile cancelConn = NULL;

#ifdef WIN32
static CRITICAL_SECTION cancelConnLock;
#endif

#define write_stderr(str) write(fileno(stderr), str, strlen(str))

#ifndef WIN32

static void handle_sigint(SIGNAL_ARGS)
{
    int save_errno = errno;
    int rc;
    char errbuf[256];

    // When receiving cancel request, stop retry right now.
    //
    ResetQueryRetryController();

    /* if we are waiting for input, longjmp out of it */
    if (sigint_interrupt_enabled) {
        sigint_interrupt_enabled = false;
        siglongjmp(sigint_interrupt_jmp, 1);
    }

    /* else, set cancel flag to stop any long-running loops */
    cancel_pressed = true;

    /* and send QueryCancel if we are processing a database query */
    if (cancelConn != NULL) {
        if (PQcancel(cancelConn, errbuf, sizeof(errbuf))) {
            rc = write_stderr("Cancel request sent\n");
            (void)rc; /* ignore errors, nothing we can do here */
        } else {
            rc = write_stderr("Could not send cancel request: ");
            (void)rc; /* ignore errors, nothing we can do here */
            rc = write_stderr(errbuf);
            (void)rc; /* ignore errors, nothing we can do here */
        }
    }

    errno = save_errno; /* just in case the write changed it */
}

void setup_cancel_handler(void)
{
    pqsignal(SIGINT, handle_sigint);
}

void ignore_quit_signal(void)
{
    pqsignal(SIGQUIT, SIG_IGN);
}

#else  /* WIN32 */

static BOOL WINAPI consoleHandler(DWORD dwCtrlType)
{
    char errbuf[256];

    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
        /*
         * Can't longjmp here, because we are in wrong thread :-(
         */
        /* set cancel flag to stop any long-running loops */
        cancel_pressed = true;

        /* and send QueryCancel if we are processing a database query */
        EnterCriticalSection(&cancelConnLock);
        if (cancelConn != NULL) {
            if (PQcancel(cancelConn, errbuf, sizeof(errbuf)))
                write_stderr("Cancel request sent\n");
            else {
                write_stderr("Could not send cancel request: ");
                write_stderr(errbuf);
            }
        }
        LeaveCriticalSection(&cancelConnLock);

        return TRUE;
    } else
        /* Return FALSE for any signals not being handled */
        return FALSE;
}

void setup_cancel_handler(void)
{
    InitializeCriticalSection(&cancelConnLock);

    SetConsoleCtrlHandler(consoleHandler, TRUE);
}
#endif /* WIN32 */

/* ConnectionUp
 *
 * Returns whether our backend connection is still there.
 */
static bool ConnectionUp(void)
{
    return PQstatus(pset.db) != CONNECTION_BAD;
}

/* CheckConnection
 *
 * Verify that we still have a good connection to the backend, and if not,
 * see if it can be restored.
 *
 * Returns true if either the connection was still there, or it could be
 * restored successfully; false otherwise.	If, however, there was no
 * connection and the session is non-interactive, this will exit the program
 * with a code of EXIT_BADCONN.
 */
static bool CheckConnection(void)
{
    bool OK = false;

    OK = ConnectionUp();
    if (!OK) {
        if (!pset.cur_cmd_interactive) {
            psql_error("connection to server was lost\n");
            exit(EXIT_BADCONN);
        }

        fputs(_("The connection to the server was lost. Attempting reset: "), stderr);
        PQreset(pset.db);
        OK = ConnectionUp();
        if (!OK) {
            fputs(_("Failed.\n"), stderr);
            PQfinish(pset.db);
            pset.db = NULL;
            ResetCancelConn();
            UnsyncVariables();
        } else {
            fputs(_("Succeeded.\n"), stderr);
        }
    }

    return OK;
}

/*
 * SetCancelConn
 *
 * Set cancelConn to point to the current database connection.
 */
void SetCancelConn(void)
{
    PGcancel* oldCancelConn = NULL;

#ifdef WIN32
    EnterCriticalSection(&cancelConnLock);
#endif

    /* Free the old one if we have one */
    oldCancelConn = cancelConn;
    /* be sure handle_sigint doesn't use pointer while freeing */
    cancelConn = NULL;

    if (oldCancelConn != NULL)
        PQfreeCancel(oldCancelConn);

    cancelConn = PQgetCancel(pset.db);

#ifdef WIN32
    LeaveCriticalSection(&cancelConnLock);
#endif
}

/*
 * ResetCancelConn
 *
 * Free the current cancel connection, if any, and set to NULL.
 */
void ResetCancelConn(void)
{
    PGcancel* oldCancelConn = NULL;

#ifdef WIN32
    EnterCriticalSection(&cancelConnLock);
#endif

    oldCancelConn = cancelConn;
    /* be sure handle_sigint doesn't use pointer while freeing */
    cancelConn = NULL;

    if (oldCancelConn != NULL)
        PQfreeCancel(oldCancelConn);

#ifdef WIN32
    LeaveCriticalSection(&cancelConnLock);
#endif
}

/*
 * AcceptResult
 *
 * Checks whether a result is valid, giving an error message if necessary;
 * and ensures that the connection to the backend is still up.
 *
 * Returns true for valid result, false for error state.
 */
static bool AcceptResult(const PGresult* result, bool print_error)
{
    bool OK = false;

    if (NULL == result)
        OK = false;
    else
        switch (PQresultStatus(result)) {
            case PGRES_COMMAND_OK:
            case PGRES_TUPLES_OK:
            case PGRES_EMPTY_QUERY:
            case PGRES_COPY_IN:
            case PGRES_COPY_OUT:
                /* Fine, do nothing */
                OK = true;
                break;

            case PGRES_BAD_RESPONSE:
            case PGRES_NONFATAL_ERROR:
            case PGRES_FATAL_ERROR:
                OK = false;
                break;

            default:
                OK = false;
                psql_error("unexpected PQresultStatus: %d\n", PQresultStatus(result));
                break;
        }

    if (!OK) {
        const char* error = PQerrorMessage(pset.db);

        if (strlen(error)) {
            // If the query need retry, should not report error.
            //
            if (pset.max_retry_times > 0 && PQTRANS_IDLE == PQtransactionStatus(pset.db) &&
                IsQueryNeedRetry((const char*)pset.db->last_sqlstate) && pset.retry_times < pset.max_retry_times) {
                // Cache the sqlstate and set retry on.
                //
                errno_t ss_rc = strcpy_s(pset.retry_sqlstate, sizeof(pset.retry_sqlstate), pset.db->last_sqlstate);
                securec_check_c(ss_rc, "\0", "\0");
                pset.retry_on = true;
            } else if (print_error) {
                psql_error("%s", error);
            }
        }

        (void)CheckConnection();
    }

    return OK;
}

/*
 * PSQLexec
 *
 * This is the way to send special queries (those not directly entered
 * by the user). It is subject to -E but not -e.
 *
 * In autocommit-off mode, a new transaction block is started if start_xact
 * is true; nothing special is done when start_xact is false.  Typically,
 * start_xact = false is used for SELECTs and explicit BEGIN/COMMIT commands.
 *
 * Caller is responsible for handling the ensuing processing if a COPY
 * command is sent.
 *
 * Note: we don't bother to check PQclientEncoding; it is assumed that no
 * caller uses this path to issue "SET CLIENT_ENCODING".
 */
PGresult* PSQLexec(const char* query, bool start_xact)
{
    PGresult* res = NULL;
    errno_t rc = 0;

    if (NULL == pset.db) {
        psql_error("You are currently not connected to a database.\n");
        return NULL;
    }

    if (pset.echo_hidden != PSQL_ECHO_HIDDEN_OFF) {
        printf(_("********* QUERY **********\n"
                 "%s\n"
                 "**************************\n\n"),
            query);
        fflush(stdout);
        if (pset.logfile != NULL) {
            fprintf(pset.logfile,
                _("********* QUERY **********\n"
                  "%s\n"
                  "**************************\n\n"),
                query);
            fflush(pset.logfile);
        }

        if (pset.echo_hidden == PSQL_ECHO_HIDDEN_NOEXEC)
            return NULL;
    }

    SetCancelConn();

    if (start_xact && !pset.autocommit && PQtransactionStatus(pset.db) == PQTRANS_IDLE) {

        res = PQexec(pset.db, "START TRANSACTION ");
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            psql_error("%s", PQerrorMessage(pset.db));
            PQclear(res);
            ResetCancelConn();
            return NULL;
        }
        PQclear(res);
    }

    res = PQexec(pset.db, query);

#ifndef WIN32
    /* Clear password related memory to avoid leaks when core. */
    if (pset.cur_cmd_interactive) {
        if ((pset.db != NULL) && (pset.db->last_query != NULL)) {
            rc = memset_s(pset.db->last_query, strlen(pset.db->last_query), 0, strlen(pset.db->last_query));
            securec_check_c(rc, "\0", "\0");
        }
    }
#endif

    ResetCancelConn();

    if (!AcceptResult(res)) {
        PQclear(res);
        res = NULL;
    }

    return res;
}

/*
 * PrintNotifications: check for asynchronous notifications, and print them out
 */
static void PrintNotifications(void)
{
    PGnotify* notify = NULL;

    while ((notify = PQnotifies(pset.db)) != NULL) {
        /* for backward compatibility, only show payload if nonempty */
        if (notify->extra[0])
            fprintf(pset.queryFout,
                _("Asynchronous notification \"%s\" with payload \"%s\" received from server process with PID %lu.\n"),
                notify->relname,
                notify->extra,
                notify->be_pid);
        else
            fprintf(pset.queryFout,
                _("Asynchronous notification \"%s\" received from server process with PID %lu.\n"),
                notify->relname,
                notify->be_pid);
        fflush(pset.queryFout);
        PQfreemem(notify);
    }
}

/*
 * PrintQueryTuples: assuming query result is OK, print its tuples
 *
 * Returns true if successful, false otherwise.
 */
static bool PrintQueryTuples(const PGresult* results)
{
    printQueryOpt my_popt = pset.popt;

    /* write output to \g argument, if any */
    if (NULL != pset.gfname) {
        /* keep this code in sync with ExecQueryUsingCursor */
        FILE* queryFout_copy = pset.queryFout;
        bool queryFoutPipe_copy = pset.queryFoutPipe;

        pset.queryFout = stdout; /* so it doesn't get closed */

        /* open file/pipe */
        if (!setQFout(pset.gfname)) {
            pset.queryFout = queryFout_copy;
            pset.queryFoutPipe = queryFoutPipe_copy;
            return false;
        }

        printQuery(results, &my_popt, pset.queryFout, pset.logfile);

        /* close file/pipe, restore old setting */
        setQFout(NULL);

        pset.queryFout = queryFout_copy;
        pset.queryFoutPipe = queryFoutPipe_copy;

        free(pset.gfname);
        pset.gfname = NULL;
    } else
        printQuery(results, &my_popt, pset.queryFout, pset.logfile);

    return true;
}

/*
 * ProcessResult: utility function for use by SendQuery() only
 *
 * When our command string contained a COPY FROM STDIN or COPY TO STDOUT,
 * PQexec() has stopped at the PGresult associated with the first such
 * command.  In that event, we'll marshal data for the COPY and then cycle
 * through any subsequent PGresult objects.
 *
 * When the command string contained no affected COPY command, this function
 * degenerates to an AcceptResult() call.
 *
 * Changes its argument to point to the last PGresult of the command string,
 * or NULL if that result was for a COPY FROM STDIN or COPY TO STDOUT.
 *
 * Returns true on complete success, false otherwise.  Possible failure modes
 * include purely client-side problems; check the transaction status for the
 * server-side opinion.
 *
 * print_error: Should we print error message to stderr in gsql?
 */
static bool ProcessResult(PGresult** results, bool is_explain, bool print_error)
{
    PGresult* next_result = NULL;
    bool success = true;
    bool first_cycle = true;

    if (is_explain && (*results = PQgetResult(pset.db)) == NULL && ConnectionUp())
        return success;

    do {
        ExecStatusType result_status;
        bool is_copy = false;

        if (!AcceptResult(*results, print_error)) {
            /*
             * Failure at this point is always a server-side failure or a
             * failure to submit the command string.  Either way, we're
             * finished with this command string.
             */
            success = false;
            break;
        }

        result_status = PQresultStatus(*results);
        switch (result_status) {
            case PGRES_EMPTY_QUERY:
            case PGRES_COMMAND_OK:
            case PGRES_TUPLES_OK:
                is_copy = false;
                break;

            case PGRES_COPY_OUT:
            case PGRES_COPY_IN:
                is_copy = true;
                break;

            default:
                /* AcceptResult() should have caught anything else. */
                is_copy = false;
                psql_error("unexpected PQresultStatus: %d\n", result_status);
                break;
        }

        if (is_copy) {
            /*
             * Marshal the COPY data.  Either subroutine will get the
             * connection out of its COPY state, then call PQresultStatus()
             * once and report any error.
             */
            SetCancelConn();
            if (result_status == PGRES_COPY_OUT)
                success = handleCopyOut(pset.db, pset.queryFout) && success;
            else
                success = handleCopyIn(pset.db, pset.cur_cmd_source, PQbinaryTuples(*results)) && success;
            ResetCancelConn();

            /*
             * Call PQgetResult() once more.  In the typical case of a
             * single-command string, it will return NULL.	Otherwise, we'll
             * have other results to process that may include other COPYs.
             */
            PQclear(*results);
            *results = next_result = PQgetResult(pset.db);
        } else if (is_explain || first_cycle)
            /* fast path: no COPY commands; PQexec visited all results */
            break;
        else if (!is_explain && ((next_result = PQgetResult(pset.db)) != NULL)) {
            /* non-COPY command(s) after a COPY: keep the last one */
            PQclear(*results);
            *results = next_result;
        }

        first_cycle = false;
    } while (NULL != next_result);

    /* may need this to recover from conn loss during COPY */
    if (!first_cycle && !CheckConnection())
        return false;

    return success;
}

/*
 * PrintQueryStatus: report command status as required
 *
 * Note: Utility function for use by PrintQueryResults() only.
 */
static void PrintQueryStatus(PGresult* results)
{
    char buf[16];
    errno_t rc = EOK;

    if (!pset.quiet) {
        if (pset.popt.topt.format == PRINT_HTML) {
            fputs("<p>", pset.queryFout);
            html_escaped_print(PQcmdStatus(results), pset.queryFout);
            fputs("</p>\n", pset.queryFout);
        } else
            fprintf(pset.queryFout, "%s\n", PQcmdStatus(results));
    }

    if (NULL != pset.logfile)
        fprintf(pset.logfile, "%s\n", PQcmdStatus(results));

    rc = sprintf_s(buf, sizeof(buf), "%u", (unsigned int)PQoidValue(results));
    check_sprintf_s(rc);
    if (!SetVariable(pset.vars, "LASTOID", buf)) {
        psql_error("set variable %s failed.\n", "LASTOID");
    }
}

/*
 * PrintQueryResults: print out query results as required
 *
 * Note: Utility function for use by SendQuery() only.
 *
 * Returns true if the query executed successfully, false otherwise.
 */
static bool PrintQueryResults(PGresult* results)
{
    bool success = false;
    const char* cmdstatus = NULL;
    int ret = 0;

    if (NULL == results)
        return false;

    /* Lock queryFout for write in parallel execute. */
    if (pset.parallel) {
        if (IsInteractiveMode()) {
            ret = LockMutexForParallel();
        } else {
            ret = file_lock(fileno(pset.queryFout), LOCK_EX);
        }
        if (ret == -1) {
            psql_error("acquiring lock on output file failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    switch (PQresultStatus(results)) {
        case PGRES_TUPLES_OK:
            /* print the data ... */
            success = PrintQueryTuples(results);
            /* if it's INSERT/UPDATE/DELETE RETURNING, also print status */
            cmdstatus = PQcmdStatus(results);
            if(!common_sseflag.fides&&!common_delsseflag.fides)
            {
                if (strncmp(cmdstatus, "INSERT", 6) == 0 || strncmp(cmdstatus, "UPDATE", 6) == 0 ||
                strncmp(cmdstatus, "DELETE", 6) == 0)
                PrintQueryStatus(results);
            }
            break;

        case PGRES_COMMAND_OK:
            if(!common_sseflag.fides&&!common_delsseflag.fides)
            {
                PrintQueryStatus(results);
            }
            success = true;
            break;

        case PGRES_EMPTY_QUERY:
            success = true;
            break;

        case PGRES_COPY_OUT:
        case PGRES_COPY_IN:
            /* nothing to do here */
            success = true;
            break;

        case PGRES_BAD_RESPONSE:
        case PGRES_NONFATAL_ERROR:
        case PGRES_FATAL_ERROR:
            success = false;
            break;

        default:
            success = false;
            psql_error("unexpected PQresultStatus: %d\n", PQresultStatus(results));
            break;
    }

    fflush(pset.queryFout);

    if (pset.parallel) {
        if (IsInteractiveMode()) {
            (void)UnlockMutexForParallel();
        } else {
            (void)file_lock(fileno(pset.queryFout), LOCK_UN);
        }
    }

    return success;
}

// check if the query need to retry by error code.
//
bool IsQueryNeedRetry(const char* sqlstate)
{
    int i = 0;

    // If connection status is not OK, retry is on and retry sleep is on.
    //
    if (CheckPoolerConnectionStatus()) {
        pset.retry_sleep = false;
    } else {
        pset.retry_sleep = true;
        return true;
    }

    // If the sqlstate match any one of errcodes list, retry is on.
    //
    for (i = 0; i < (int)pset.errcodes_list.size(); i++) {
        if (pg_strncasecmp(sqlstate, pset.errcodes_list[i], strlen(sqlstate)) == 0)
            return true;
    }

    // If the new error code is different from the cached one , stop retry.
    //
    if (pset.retry_times > 0 && pg_strncasecmp(sqlstate, pset.retry_sqlstate, strlen(sqlstate)) != 0)
        ResetQueryRetryController();

    return false;
}

// Get the Pooler connection status from Coordinator.
// When node failure or primary-standby switch happens, the connection status must be abnormal.
//
static bool CheckPoolerConnectionStatus()
{
    PGconn* conn = NULL;
    PGresult* res = NULL;
    bool status = true;
    char* decode_pwd = NULL;
    GS_UINT32 pwd_len = 0;
    errno_t rc = EOK;
    char* old_conninfo_values = NULL;

    // Decode the password for retry inner connection.
    if (pset.connInfo.values[3] != NULL) {
        decode_pwd = SEC_decodeBase64(pset.connInfo.values[3], &pwd_len);
        if (decode_pwd == NULL) {
            fprintf(stderr, "%s: decode retry connect messages failed.", pset.progname);
            exit(EXIT_BADCONN);
        }
        old_conninfo_values = pset.connInfo.values[3];
        pset.connInfo.values[3] = decode_pwd;
    }

    // Get a new connection for checking pooler connection status.
    // If use the old one, the error in pset.db may be covered.
    //
    conn = PQconnectdbParams(pset.connInfo.keywords, pset.connInfo.values, true);

    // Clear sensitive memory of decode_pwd as soon as possible.
    if (decode_pwd != NULL) {
        rc = memset_s(decode_pwd, strlen(decode_pwd), 0, strlen(decode_pwd));
        securec_check_c(rc, "\0", "\0");
        if (decode_pwd != NULL) {
            OPENSSL_free(decode_pwd);
            decode_pwd = NULL;
        }

        // Revert the old value for next retry connection.
        pset.connInfo.values[3] = old_conninfo_values;
    }

    if (CONNECTION_BAD == PQstatus(conn)) {
        fprintf(stderr, "%s: %s", pset.progname, (char*)PQerrorMessage(conn));
        PQfinish(conn);
        return true;
    }

    res = PQexec(conn, "SELECT * FROM PGXC_POOL_CONNECTION_STATUS();");

    if ((res != NULL) && PGRES_TUPLES_OK == PQresultStatus(res)) {
        // If return value equals to "f", means that the connection status is abnormal.
        //
        if ((res->tuples != NULL) && pg_strcasecmp(res->tuples[0]->value, "f") == 0)
            status = false;
    }

    PQclear(res);
    PQfinish(conn);

    return status;
}

// Reset query retry controller.
//
void ResetQueryRetryController()
{
    pset.retry_times = 0;
    errno_t rc = memset_s(pset.retry_sqlstate, sizeof(pset.retry_sqlstate), 0, sizeof(pset.retry_sqlstate));
    securec_check_c(rc, "\0", "\0");
    pset.retry_on = false;
    pset.retry_sleep = false;
}

bool QueryRetryController(const char* query)
{
    bool success = false;

    pset.retry_times = 0;

    for (;;) {
        // If get a SIGINT signal in retry loop, the QueryRetryController is reset.
        // When come to the next loop, retry_on flag is false. So just break the loop
        // without printing the retry log.
        //
        if (pset.retry_on)
            printf(_("INFO: query retry %d time(s).\n"), ++pset.retry_times);

        // If Pooler connection status is not OK, no hurry, just sleep 1 minute.
        // CM server may take a few of minutes to judge node failure and failover the standby.
        //
        if ((pset.db != NULL) && pset.retry_sleep)
            pg_usleep(60000000);

        if (pset.retry_on) {
            success = SendQuery(query);
        } else {
            break;
        }
        // If retry succeeds or retry times reaches the maximum, break the loop here.
        //
        if (success || pset.retry_times == pset.max_retry_times) {
            ResetQueryRetryController();
            break;
        }
    }

    return success;
}

bool GetPrintResult(PGresult** results, bool is_explain, bool is_print, const char* query, bool print_error)
{
    bool OK = false;
    bool return_value = true;
    do {
        OK = ProcessResult(results, is_explain, print_error);

        if (*results == NULL)
            break;

        /* but printing results isn't: */
        if (OK && is_print) {
            OK = PrintQueryResults(*results);
            /* record the set stmts when needed. */
            RecordGucStmt(*results, query);
        }

        if (is_explain) {
            PQclear(*results);
            *results = NULL;
        }

        /* if is_explain is true and OK is false, we should save the result for returning */
        if (!OK && is_explain) {
            return_value = false;
        }
    } while (is_explain);

    return OK && return_value;
}

/*
 * SendQuery: send the query string to the backend
 * (and print out results)
 *
 * Note: This is the "front door" way to send a query. That is, use it to
 * send queries actually entered by the user. These queries will be subject to
 * single step mode.
 * To send "the door in back" queries (generated by slash commands, etc.) in a
 * controlled way, use PSQLexec().
 *
 * print_error: Should gsql print error message to stderr with this query ?
 *
 * Returns true if the query executed successfully, false otherwise.
 */


bool SendQuery(const char* query, bool is_print, bool print_error)
{
    //set flag and extract keyword
    common_sseflag.resetSSEflags();
    common_delsseflag.resetSSEdelflags();
    common_sse_join=false;
    string join_mode="";//inner left right cross
    vector<SSEflag> join_sse_modes;
    string querystr(query);
    string querystr_lower(query);
    transform(querystr_lower.begin(), querystr_lower.end(), querystr_lower.begin(), (int(*)(int))tolower);
    string qs_seven(querystr_lower,0,7);

    set<std::string> join_table_name;
    std::map<std::string, std::set<std::string>> join_cond_map;


    string table_name= getTableNameFromSQL(querystr_lower);
    /*
     * 4.1 Minimal-baseline guard for Fides user input.
     * Runs BEFORE any SSE prefix injection, flag change, counter update or
     * request generation. Only plain single-table SELECT on a registered
     * Fides table is examined; JOIN / DELETE / INSERT / UPDATE, ordinary
     * non-SSE tables, internal re-insert requests and physical _data table
     * lookups never enter this branch.
     */
    if (qs_seven == "select " && getTableNameFromJoinSQL(querystr_lower).empty()) {
        auto zyc_map_it = sse_table_map.find(table_name);
        if (zyc_map_it != sse_table_map.end()) {
            std::string zyc_mode;
            std::string zyc_sse_col = "keyword";
            for (const auto& zyc_col : zyc_map_it->second) {
                if (zyc_col.kind != sse_kind::UNDEFINED) {
                    zyc_mode = sse_kind_to_string(zyc_col.kind);
                    zyc_sse_col = zyc_col.col_name;
                    break;
                }
            }
            std::transform(zyc_mode.begin(), zyc_mode.end(), zyc_mode.begin(),
                           (int(*)(int))tolower);
            std::transform(zyc_sse_col.begin(), zyc_sse_col.end(), zyc_sse_col.begin(),
                           (int(*)(int))tolower);
            if (zyc_mode == "fides") {
                /* Mask everything inside single quotes so that words such as
                 * AND / OR / LIKE inside a keyword are not treated as SQL
                 * operators. Both strings keep identical length and offsets. */
                std::string zyc_outside;
                bool zyc_in_quote = false;
                for (std::string::size_type i = 0; i < querystr_lower.size(); ++i) {
                    if (querystr_lower[i] == '\'') {
                        zyc_in_quote = !zyc_in_quote;
                        zyc_outside += ' ';
                    } else {
                        zyc_outside += zyc_in_quote ? ' ' : querystr_lower[i];
                    }
                }

                /* Keep existing JOIN / comma-list paths untouched. */
                if (zyc_outside.find(" join ") == std::string::npos &&
                    zyc_outside.find(',') == std::string::npos) {
                    const char* zyc_reason = NULL;

                    /* (1) projection must be SELECT * */
                    std::string::size_type zyc_sel = zyc_outside.find("select");
                    std::string::size_type zyc_from = zyc_outside.find(" from ", zyc_sel);
                    if (zyc_sel == std::string::npos || zyc_from == std::string::npos) {
                        zyc_reason = "not a plain single-table SELECT";
                    } else {
                        std::string zyc_proj =
                            zyc_outside.substr(zyc_sel + 6, zyc_from - zyc_sel - 6);
                        std::string::size_type pa = zyc_proj.find_first_not_of(" \t\r\n");
                        std::string::size_type pb = zyc_proj.find_last_not_of(" \t\r\n");
                        std::string zyc_proj_trim =
                            (pa == std::string::npos) ? "" : zyc_proj.substr(pa, pb - pa + 1);
                        if (zyc_proj_trim != "*")
                            zyc_reason = "column projection is not supported";
                    }

                    /* (2) WHERE must be exactly one <sse_col>='<string>' equality */
                    if (zyc_reason == NULL) {
                        std::string::size_type zyc_where = zyc_outside.find(" where ");
                        if (zyc_where == std::string::npos) {
                            zyc_reason = "missing WHERE <keyword>='...' condition";
                        } else if (zyc_outside.find(" and ", zyc_where) != std::string::npos ||
                                   zyc_outside.find(" or ", zyc_where) != std::string::npos ||
                                   zyc_outside.find(" like ", zyc_where) != std::string::npos) {
                            zyc_reason = "compound condition / LIKE is not supported";
                        } else {
                            std::string zyc_cond = querystr_lower.substr(zyc_where + 7);
                            std::string::size_type zyc_eq = zyc_cond.find('=');
                            if (zyc_eq == std::string::npos) {
                                zyc_reason = "only equality on the SSE keyword column is supported";
                            } else {
                                std::string zyc_lhs = zyc_cond.substr(0, zyc_eq);
                                std::string::size_type la = zyc_lhs.find_first_not_of(" \t\r\n");
                                std::string::size_type lb = zyc_lhs.find_last_not_of(" \t\r\n");
                                std::string zyc_lhs_trim =
                                    (la == std::string::npos) ? "" : zyc_lhs.substr(la, lb - la + 1);
                                bool zyc_col_ok = (zyc_lhs_trim == zyc_sse_col);
                                if (!zyc_col_ok && zyc_lhs_trim.size() > zyc_sse_col.size() + 1) {
                                    std::string::size_type off =
                                        zyc_lhs_trim.size() - zyc_sse_col.size();
                                    zyc_col_ok = (zyc_lhs_trim[off - 1] == '.' &&
                                                  zyc_lhs_trim.compare(off, zyc_sse_col.size(),
                                                                       zyc_sse_col) == 0);
                                }
                                std::string::size_type zyc_q1 = zyc_cond.find('\'', zyc_eq + 1);
                                if (!zyc_col_ok || zyc_q1 == std::string::npos ||
                                    zyc_cond.find_first_not_of(" \t\r\n", zyc_eq + 1) != zyc_q1) {
                                    zyc_reason = "keyword value must be a quoted string";
                                } else {
                                    std::string::size_type zyc_q2 =
                                        zyc_cond.find('\'', zyc_q1 + 1);
                                    if (zyc_q2 == std::string::npos) {
                                        zyc_reason = "unterminated quoted keyword";
                                    } else {
                                        std::string zyc_tail = zyc_cond.substr(zyc_q2 + 1);
                                        if (zyc_tail.find_first_not_of(" \t\r\n;") !=
                                            std::string::npos)
                                            zyc_reason =
                                                "extra conditions / clauses are not supported";
                                    }
                                }
                            }
                        }
                    }

                    if (zyc_reason != NULL) {
                        psql_error("unsupported SSE single-table input (%s); "
                                   "rejected before any request was sent: %s\n",
                                   zyc_reason, query);
                        return false;
                    }
                }
            }
        }
    }
    if(qs_seven=="insert ")
    {
        // table_name=getTableNameFromInsertSQL(querystr_lower);
        auto it = sse_table_map.find(table_name);
        if(it != sse_table_map.end())
        {
            string sse_mode="";
            for (const auto& col : it->second) {
                if (col.kind != sse_kind::UNDEFINED) {
                    sse_mode= sse_kind_to_string(col.kind);// 找到第一个不为 UNDEFINED 的 kind
                    break;
                }
            }
            if(!sse_mode.empty())
            {
                transform(sse_mode.begin(), sse_mode.end(), sse_mode.begin(), (int(*)(int))tolower);
                querystr_lower=sse_mode + "_" + querystr_lower; 
                querystr=sse_mode + "_" + querystr; 
            }
        }
    }
    if(qs_seven=="delete ")
    { 
        if(!table_name.empty())
        {
            auto it = sse_table_map.find(table_name);
            if(it != sse_table_map.end())
            {
                string sse_mode="";
                for (const auto& col : it->second) {
                    if (col.kind != sse_kind::UNDEFINED) {
                        sse_mode= sse_kind_to_string(col.kind);// 找到第一个不为 UNDEFINED 的 kind
                        break;
                    }
                }
                if(!sse_mode.empty())
                {
                    transform(sse_mode.begin(), sse_mode.end(), sse_mode.begin(), (int(*)(int))tolower);
                    if(sse_mode=="fbdsse")
                    {
                        auto [fbdssetableName, indValue, keywordValue] = getTableNameAndConditionsFromDeleteSQL(querystr_lower);
                        FB_UpdateRequest del_req;
                        del_req=FBdsse_Client->FB_insert(keywordValue, indValue);
                        //ut e ct join     join is empty
                        querystr="insert into " + fbdssetableName + "_index VALUES ('" + del_req.ut + "', '" + del_req.e + "', '" + del_req.ct +"');";
                        querystr_lower=querystr;
                        transform(querystr_lower.begin(), querystr_lower.end(), querystr_lower.begin(), (int(*)(int))tolower);
                        const char* query_changed = querystr.c_str();
                        query=query_changed;
                    }
                    else
                    {
                        querystr_lower=sse_mode + "_" + querystr_lower; 
                        querystr=sse_mode + "_" + querystr; 
                    }
                }
            }
        }

    }
    if(qs_seven=="select ")
    {
        join_table_name=getTableNameFromJoinSQL(querystr_lower);
        if(!join_table_name.empty())//if join
        {
            join_mode=getJoinType(querystr_lower);
            for (const auto& value : join_table_name) 
            {
                auto it = sse_table_map.find(value);
                if(it !=sse_table_map.end())//check sse join      可以在这加一个sse_join_table_name
                {
                    common_sse_join=true;
                    for (const auto& col : it->second) {
                        if (col.kind != sse_kind::UNDEFINED) 
                        {
                            SSEflag join_sse_mode;
                            // join_sse_mode.resetSSEflags();
                            join_sse_mode.setSSEflag(sse_kind_to_string(col.kind));
                            join_sse_modes.push_back(join_sse_mode);
                            break;
                        }
                    }
                }
                else
                {
                    SSEflag join_sse_mode;
                    // join_sse_mode.resetSSEflags();
                    join_sse_modes.push_back(join_sse_mode);
                    break;
                }
            }
        }
        else
        {
            // table_name=getTableNameFromSelectSQL(querystr_lower);
            auto it = sse_table_map.find(table_name);
            if(it != sse_table_map.end())
            {
                string sse_mode="";
                for (const auto& col : it->second) {
                    if (col.kind != sse_kind::UNDEFINED) {
                        sse_mode= sse_kind_to_string(col.kind);// 找到第一个不为 UNDEFINED 的 kind
                        common_sseflag.setSSEflag(sse_mode);
                        break;
                    }
                }
                if(!sse_mode.empty())
                {
                    transform(sse_mode.begin(), sse_mode.end(), sse_mode.begin(), (int(*)(int))tolower);
                    querystr_lower=sse_mode + "_" + querystr_lower; 
                    querystr=sse_mode + "_" + querystr; 
                }
            }
        }
    }

    if(!common_sse_join)
    {
        const char* query_changed = querystr.c_str();
        query=query_changed;
    }

    

    std::vector<std::string> join_select;
    string join_temp="";
    string child_temp="";
    if(common_sse_join)
    {
        join_cond_map=getWhereConditions(querystr);//tablename是小写
        //genju tablename shuliang shengcheng sse_select
        for (const auto& value : join_table_name) //遍历join的表
        {
            child_temp="select * from " + value;
            auto it = sse_table_map.find(value);
            auto join_it = join_cond_map.find(value);
            if(it != sse_table_map.end() && join_it != join_cond_map.end())// sse table and has where clause
            {
                set<string> join_filter_col=join_cond_map[value];
                string sse_mode="";
                string col_name="";
                for (const auto& cond : join_filter_col) //检查where条件是否是sse列
                {
                    int col_end = cond.find("=");
                    col_name=string(cond, 0, col_end);
    	            for (const auto& col : it->second) //遍历表中列
                    {
                        if (col_name==col.col_name && col.kind != sse_kind::UNDEFINED) {
                            sse_mode= sse_kind_to_string(col.kind);// 找到where条件列 and 不为 UNDEFINED 的 kind
                            break;
                        }
                    }
                    child_temp=child_temp+" where " + cond;//目前只支持一表一个条件
                }
                if(!sse_mode.empty())
                {
                    transform(sse_mode.begin(), sse_mode.end(), sse_mode.begin(), (int(*)(int))tolower);
                    child_temp=sse_mode + "_" + child_temp; 
                }
            }
            child_temp=child_temp + ";";
            join_temp=join_temp+child_temp;
        }
        join_select=getMulSelectFromJoin(join_temp);
        // querystr_lower=join_temp;
        // querystr=join_temp;
        // transform(querystr_lower.begin(), querystr_lower.end(), querystr_lower.begin(), (int(*)(int))tolower);
    }
    


    string qs_12(querystr_lower, 0, 12);
    string common_keyword;
    string common_tableName;

    PGresult* results = NULL;

    if(!common_sse_join)
    {
        string modified_sql;
        if(qs_12 == "fides_select")
        {
            common_tableName=getTableNameFromSelectSQL(querystr_lower);
            common_sseflag.fides=true;
            int keyword_begin = querystr_lower.find("'");
		    int keyword_end = querystr_lower.find("'", keyword_begin + 1);
    	    string keyword(querystr, keyword_begin + 1, keyword_end - keyword_begin - 1);
            common_keyword=keyword;
        }
        else if(qs_12 == "mitra_select")
        {
            common_tableName=getTableNameFromSelectSQL(querystr);
            common_sseflag.mitra=true;
            int keyword_begin = querystr_lower.find("'");
	    	int keyword_end = querystr_lower.find("'", keyword_begin + 1);
    	    string keyword(querystr, keyword_begin + 1, keyword_end - keyword_begin - 1);
            common_keyword=keyword;
        }
        else if(qs_12 == "mitra_delete")
        {
            common_delsseflag.mitra=true;
        }
        else if(qs_12 == "fbdsse_selec")
        {
            common_tableName=getTableNameFromSelectSQL(querystr_lower);
            common_sseflag.fbdsse=true;
            int keyword_begin = querystr_lower.find("'");
		    int keyword_end = querystr_lower.find("'", keyword_begin + 1);
    	    string keyword(querystr, keyword_begin + 1, keyword_end - keyword_begin - 1);
            common_keyword=keyword;
        }
        else if(!querystr_lower.find("dual_insert"))
        { // dual_insert into t1 values ...
            modified_sql = dual_processInsert(querystr, table_name);
            query = strdup(modified_sql.c_str());
        }
        else if (!querystr_lower.find("dual_delete"))
        {// dual_delete from t1 where ind = ...
            int cntind = 0;
            modified_sql = dual_getdtoken(querystr, table_name);
            while(1){
                query = strdup(modified_sql.c_str());
                results = PQexec(pset.db, query);
                if (!results->ntups)
                    break;
                cntind++;
            }
            // 通知服务器最大的cntind，让它执行删除操作
            modified_sql = dual_delete(querystr, table_name);
            query = strdup(modified_sql.c_str());
        }
        else if (!querystr_lower.find("dual_select"))
        {// dual_select from t1 where keyword = ...
            modified_sql = dual_processselect(querystr, table_name);
            query = strdup(modified_sql.c_str());
        }
        else if(!querystr_lower.find("closefb_insert") || !querystr_lower.find("closefb_delete"))
        { // closefb_insert into t1 values ...  closefb_delete from t1 values ('', '')
            string closefb_k1;
            string strctr;
            int ctr, status_size, flag;
            // read closefb_k1 and ctr into memory
            vector<string> status;
            
            string closefb_status = table_name + "closefb_status.txt";
            closefb_readstatus(closefb_status.c_str(), status);
            status_size = status.size();
            if (!status_size){
                closefb_k1 = "";
                ctr = closefb_maxctr;
            }
            else{
                closefb_k1 = status[0];
                strctr = status[1];
                istringstream iss(strctr);
                iss >> ctr;
            }
            if(!querystr_lower.find("closefb_insert"))
                flag = 1;
            else 
                flag = 0;
            if(!ctr)
                closefb_rebuildEDB(closefb_k1, ctr, table_name);
            
            tracker.start("closefb_client Rewrite insert_delete SQL time");
            modified_sql = closefb_processInsert(querystr, closefb_k1, ctr, flag, table_name);
            tracker.stop("closefb_client Rewrite insert_delete SQL time");
            query = strdup(modified_sql.c_str());
            ostringstream oss_ctr;
            oss_ctr << ctr;
            strctr = oss_ctr.str();

            tracker.start("closefb_client  insert UPDATE state");
            status.clear();
            status.push_back(closefb_k1);
            status.push_back(strctr);
            closefb_writestatus(closefb_status.c_str(), status);
            tracker.start("closefb_client  insert UPDATE state");

        }
        else if(!querystr_lower.find("closefb_select"))
        { // closefb_select * from t1 where keyword = ''
            modified_sql = closefb_processselect(querystr, table_name);
            query = strdup(modified_sql.c_str());
        }
        else if (!querystr_lower.find("januspp_insert"))
        { // januspp_insert into t1 values (), ...
            modified_sql = januspp_processInsert(querystr, table_name);
            query = strdup(modified_sql.c_str());
        }
        else if(!querystr_lower.find("januspp_delete")){ // januspp_delete from t1 where ind = '' and keyword = ''
            modified_sql = januspp_processdelete(querystr, table_name);
            query = strdup(modified_sql.c_str());
        }
        else if(!querystr_lower.find("januspp_select"))
        {
            modified_sql = januspp_processselect(querystr, table_name);
            query = strdup(modified_sql.c_str());
        }
    }

    if(qs_12 == "fides_delete")
    {
        common_delsseflag.fides=true;
    }

    
    
    // PGresult* results = NULL;
    PGTransactionStatusType transaction_status;
    double elapsed_msec = 0;
    bool OK = false;
    bool on_error_rollback_savepoint = false;
    static bool on_error_rollback_warning = false;

    errno_t rc = 0;
#if defined(USE_ASSERT_CHECKING) || defined(FASTCHECK)
    if (pset.parseonly)
        return true;
#endif
    if (NULL == pset.db) {
        psql_error("You are currently not connected to a database.\n");
        return false;
    }

    if (pset.singlestep) {
        char buf[3];

        printf(_("***(Single step mode: verify command)*******************************************\n"
                 "%s\n"
                 "***(press return to proceed or enter x and return to cancel)********************\n"),
            query);
        fflush(stdout);
        if (fgets(buf, sizeof(buf), stdin) != NULL)
            if (buf[0] == 'x') {
                return false;
            }
    } else if (pset.echo == PSQL_ECHO_QUERIES) {
        puts(query);
        fflush(stdout);
    }

    if (pset.logfile != NULL) {
        fprintf(pset.logfile,
            _("********* QUERY **********\n"
              "%s\n"
              "**************************\n\n"),
            query);
        fflush(pset.logfile);
    }

    SetCancelConn();

    transaction_status = PQtransactionStatus(pset.db);

    if (transaction_status == PQTRANS_IDLE && !pset.autocommit && !command_no_begin(query)) {
        results = PQexec(pset.db, "START TRANSACTION");
        if (PQresultStatus(results) != PGRES_COMMAND_OK) {
            psql_error("%s", PQerrorMessage(pset.db));
            PQclear(results);
            ResetCancelConn();
            return false;
        }
        PQclear(results);
        transaction_status = PQtransactionStatus(pset.db);
    }

    if (transaction_status == PQTRANS_INTRANS && pset.on_error_rollback != PSQL_ERROR_ROLLBACK_OFF &&
        (pset.cur_cmd_interactive || pset.on_error_rollback == PSQL_ERROR_ROLLBACK_ON)) {
        if (on_error_rollback_warning == false && pset.sversion < 80000) {
            fprintf(stderr,
                _("The server (version %d.%d) does not support savepoints for ON_ERROR_ROLLBACK.\n"),
                pset.sversion / 10000,
                (pset.sversion / 100) % 100);
            on_error_rollback_warning = true;
        } else {
            results = PQexec(pset.db, "SAVEPOINT pg_psql_temporary_savepoint");
            if (PQresultStatus(results) != PGRES_COMMAND_OK) {
                psql_error("%s", PQerrorMessage(pset.db));
                PQclear(results);
                ResetCancelConn();
                return false;
            }
            PQclear(results);
            on_error_rollback_savepoint = true;
        }
    }

    if (pset.fetch_count <= 0 || !is_select_command(query)) {
        /* Default fetch-it-all-and-print mode */
        instr_time before, after;
        bool is_explain = false;

        if (pset.timing && is_print)
            INSTR_TIME_SET_CURRENT(before);

        is_explain = is_explain_command(query);

        int Tn=join_select.size();
        std::map<std::string, std::string> t1_join_value_pt;
        std::string join_query="";
        std::string table1, table2;
        if (!is_explain)
        {
            if(!common_sse_join)
            {
                results = PQexec(pset.db, query);
            }
            else
            {
                for(int i=0;i<Tn;i++)
                {
                    querystr_lower=join_select[i];
                    querystr=join_select[i];
                    transform(querystr_lower.begin(), querystr_lower.end(), querystr_lower.begin(), (int(*)(int))tolower);
                    qs_12=string(querystr_lower, 0, 12);
                    if(i!=Tn-1 && (qs_12 == "fides_select" || qs_12=="mitra_select"))
                    {
                        common_tableName=getTableNameFromSelectSQL(querystr_lower);
                        table1=common_tableName;
                        // common_sseflag.fides=true;
                        int keyword_begin = querystr_lower.find("'");
		                int keyword_end = querystr_lower.find("'", keyword_begin + 1);
    	                string keyword(querystr, keyword_begin + 1, keyword_end - keyword_begin - 1);
                        common_keyword=keyword;
                        const char* query_changed = querystr.c_str();
                        query=query_changed;
                        results = PQexec(pset.db, query);

                        //gai   join拦截结果过滤
                        switch (join_sse_modes[i].bool2count())
                        {
                            case 1://fides
                            {// guolv
                                if(results != nullptr && results->attDescs != nullptr && results->attDescs->name != nullptr && strcmp(results->attDescs->name, "fides_setup") != 0)
                                {
                                    vector<string> records;
                                    vector<string> join_value;
                                    for(int i=0;i<results->ntups;i++)
                                    {
                                        // records.push_back(string(*(*(res->tuples+i)+1)));
                                        records.push_back(string(results->tuples[i][1].value));
                                        join_value.push_back( string(results->tuples[i][2].value)  );
                                    }
                                    sse::sophos::filterRecords(records, join_value, results->ntups);
                                    std::string key = generate_aes_key(storage_dir_path + "/" + join_key);
                                    for(int i=0;i<join_value.size();i++)
                                    {
                                        t1_join_value_pt[records[i]]=aes_decrypt(base64_decode(join_value[i]), key);
                                    }

                                    // zheli houmian keyi buyaole 
                                    string fides_temp="fides_SELECTjoi *"+common_keyword+" from " + common_tableName + " WHERE ind IN ('";//Sophos_client执行插入需要keyword
                                    for(int i=0;i<records.size();i++)
                                    {
                                        if(i!=records.size()-1)
                                        {
                                            fides_temp=fides_temp+records[i]+"','";
                                        }
                                        else
                                        {
                                            // fides_temp=fides_temp+records[i]+"');";
                                            fides_temp=fides_temp+records[i]+"') * ('";
                                        }
                                    }

                                    for(int i=0;i<join_value.size();i++)
                                    {
                                        if(i!=join_value.size()-1)
                                        {
                                            fides_temp=fides_temp+join_value[i]+"','";
                                        }
                                        else
                                        {
                                            fides_temp=fides_temp+join_value[i]+"');";
                                        }
                                    }
                                    if(records.size()==0)
                                    {
                                        // fides_temp="SELECT * FROM " + common_tableName + "_data WHERE ind IN (NULL);";
                                        fides_temp=" ;";
                                    }

                                    join_query=join_query+fides_temp;
                                }
                                break;
                            }

                            case 2://mitra
                            {
                                if(results != nullptr && results->attDescs != nullptr && results->attDescs->name != nullptr && strcmp(results->attDescs->name, "fides_setup") != 0)
                                {   
                                    vector<prf_type> records;
                                    vector<string> join_value;
                                    string sind;
                                    prf_type index;
                                    memset(index.data(), 0, AES_KEY_SIZE);
                                    vector<int> finalindex;
                                    vector<prf_type> addrs;
                                    vector<prf_type> vals;
                                    vector<string> final_join_value;
                                    string select_addr_array="ARRAY[''";
                                    string select_val_array="ARRAY[''";
                                    string select_join_array="ARRAY[''";
                                    string mitra_temp="";
                                    for(int i=0;i<results->ntups;i++)
                                    {
                                        sind=base64_decode(string(results->tuples[i][1].value));
                                        copy(sind.begin(), sind.end(), index.begin());
                                        records.push_back(index);
                                        if (!PQgetisnull(results, i, 2))//delete  row  has not  joincol
                                        {
                                            join_value.push_back(string(results->tuples[i][2].value));
                                        }
                                    }

                                    Mitra_client->searchProcess(records, join_value, common_keyword, finalindex, final_join_value, addrs, vals);
                                    std::string key = generate_aes_key(storage_dir_path + "/" + join_key);
                                    for(int i=0;i<join_value.size();i++)
                                    {
                                        t1_join_value_pt[to_string(finalindex[i])]=aes_decrypt(base64_decode(final_join_value[i]), key);
                                    }
                                    for(int i=0;i<finalindex.size();i++)
                                    {
                                        if(i!=finalindex.size()-1)
                                        {
                                            select_join_array=select_join_array+final_join_value[i]+"'', ''";
                                        }
                                        else
                                        {
                                            select_join_array=select_join_array+final_join_value[i]+ "'']";
                                        }
                                    }
                                    for(int i=0;i<addrs.size();i++)
	                                {
		                                if (i!=addrs.size()-1)
		                                {
                                            select_addr_array= select_addr_array + base64_encode(string(addrs[i].begin(),addrs[i].end())) + "'', ''";
			                                select_val_array= select_val_array + base64_encode(string(vals[i].begin(),vals[i].end())) + "'', ''";
		                                }
		                                else
		                                {
                                            select_addr_array = select_addr_array + base64_encode(string(addrs[i].begin(),addrs[i].end())) + "'']";
			                                select_val_array = select_val_array + base64_encode(string(vals[i].begin(),vals[i].end())) + "'']";
		                                }
	                                }

                                    mitra_temp="select * from mitra_reinsert("+select_addr_array+", "+select_val_array+", "+ select_join_array+ ", ''" + common_tableName + "'');";//using function

                                    if(finalindex.size()==0)
                                    {
                                        // fides_temp="SELECT * FROM fides_datatable WHERE ind IN (NULL);";
                                        mitra_temp="SELECT * FROM " + common_tableName + "_data LIMIT 0;";
                                    }
                                    join_query=join_query+mitra_temp;
                                }
                                break;
                            }
                            // default:
                            //     break;

                        }
                    }

                    if(i==Tn-1)
                    {
                        querystr_lower=join_select[i];
                        querystr=join_select[i];
                        transform(querystr_lower.begin(), querystr_lower.end(), querystr_lower.begin(), (int(*)(int))tolower);
                        qs_12=string(querystr_lower, 0, 12);
                        if( qs_12 == "fides_select"||qs_12=="mitra_select")
                        {
                            common_tableName=getTableNameFromSelectSQL(querystr_lower);
                            table2=common_tableName;
                            // common_sseflag.fides=true;
                            int keyword_begin = querystr_lower.find("'");
	                        int keyword_end = querystr_lower.find("'", keyword_begin + 1);
    	                    string keyword(querystr, keyword_begin + 1, keyword_end - keyword_begin - 1);
                            common_keyword=keyword;
                            const char* query_changed = querystr.c_str();
                            query=query_changed;
                            results = PQexec(pset.db, query);
                        }
                    }
                }
            }
        }
        else if (!PQsendQuery(pset.db, query))
            results = NULL;


        //gai   拦截结果过滤
        int condition;
        if(common_sse_join)
        {
            condition=join_sse_modes[Tn-1].bool2count();
        }
        else
        {
            condition=common_sseflag.bool2count();
        }
        switch (condition)
        {
        case 1://fides
        {// guolv
            if(results != nullptr && results->attDescs != nullptr && results->attDescs->name != nullptr && strcmp(results->attDescs->name, "fides_setup") != 0)
            {
                tracker.start("fides data SQL and reinsert rewrite");
                //no  join
                tracker.start("fides filter");
                vector<string> records;
                vector<string> join_value;
                std::map<std::string, std::string> t2_join_value_pt;
                for(int i=0;i<results->ntups;i++)
                {
                    // records.push_back(string(*(*(res->tuples+i)+1)));
                    records.push_back(string(results->tuples[i][1].value));
                    join_value.push_back(string(results->tuples[i][2].value));
                }
                sse::sophos::filterRecords(records, join_value, results->ntups);
                tracker.stop("fides filter");
                std::string key = generate_aes_key(storage_dir_path + "/" + join_key);

                // 用于存储提取出的join匹配结果
                std::map<std::string, std::vector<std::string>> result_map1;
                std::map<std::string, std::vector<std::string>> result_map2;
                std::vector<std::string> left_ind;
                std::vector<std::string> right_ind;
                if(common_sse_join)
                {
                    for(int i=0;i<join_value.size();i++)
                    {
                        t2_join_value_pt[records[i]]= aes_decrypt(base64_decode(join_value[i]), key);
                    }

                    // 遍历map1
                    for (const auto& pair1 : t1_join_value_pt) 
                    {
                        // 遍历map2，比较值是否相等
                        bool just_left=true;
                        for (const auto& pair2 : t2_join_value_pt) 
                        {
                            if (pair1.second == pair2.second) 
                            {  // 如果值相等
                                result_map1[pair1.second].push_back(pair1.first);
                                result_map2[pair2.second].push_back(pair2.first);
                                just_left=false;
                            }
                        }
                        if(just_left)//LEFT JOIN
                        {
                            left_ind.push_back(pair1.first);
                        }
                    }
                    if(join_mode=="RIGHT")
                    {
                        for (const auto& pair2 : t2_join_value_pt) 
                        {
                            bool just_right=true;
                            // 遍历map1，比较值是否相等
                            for (const auto& pair1 : t1_join_value_pt) 
                            {
                                if (pair1.second == pair2.second) 
                                {  // 如果值相等
                                    just_right=false;
                                    continue;
                                }
                            }
                            if(just_right)//RIGHT JOIN
                            {
                                right_ind.push_back(pair2.first);
                            }
                        }
                    }
                }

                //gouzao sql
                string fides_temp;
                string fides_temp2;
                if(common_sse_join)
                {
                    fides_temp="fides_SELECTjoi *"+common_keyword+" from " + common_tableName + " WHERE ind IN ('";//Sophos_client执行插入需要keyword
                }
                else
                {
                    fides_temp="fides_SELECTind *"+common_keyword+"* from " + common_tableName + " WHERE ind IN ('";//Sophos_client执行插入需要keyword
                    fides_temp2="select * from "+common_tableName + "_data where ind IN('";
                }
                
                for(int i=0;i<records.size();i++)
                {
                    if(i!=records.size()-1)
                    {
                        fides_temp=fides_temp+records[i]+"','";
                        fides_temp2=fides_temp2+records[i]+"','";
                    }
                    else
                    {
                        fides_temp=fides_temp+records[i]+"') * ('";
                        fides_temp2=fides_temp2+records[i]+"');";
                    }
                }
                for(int i=0;i<join_value.size();i++)
                {
                    if(i!=join_value.size()-1)
                    {
                        fides_temp=fides_temp+join_value[i]+"','";
                    }
                    else
                    {
                        fides_temp=fides_temp+join_value[i]+"');";
                    }
                }
                if(records.size()==0)
                {
                    if(common_sse_join)
                    {
                        fides_temp=" ;";
                    }
                    else{
                        fides_temp="SELECT * FROM " + common_tableName + "_data LIMIT 0;";
                        fides_temp2="SELECT * FROM "+common_tableName+"_data LIMIT 0;";
                    }
                    
                }
                tracker.stop("fides data SQL and reinsert rewrite");
                if(common_sse_join)
                {
                    string join_select;
                    size_t map_size = result_map1.size();
                    size_t count = 0;
                    
                    if(join_mode=="INNER" || join_mode=="UNKNOWN" || join_mode=="LEFT" || join_mode=="RIGHT")
                    {
                        if(map_size!=0)//inner and left or inner and right
                        {
                            for (const auto& pair : result_map1) 
                            {
                                count++;
                                join_select=join_select+ "select * from "+table1 + "_data, "+table2 + "_data where " + table1 + "_data.ind in(''";
                                for(int i=0; i< pair.second.size();i++)//table1
                                {
                                    if(i != pair.second.size()-1)
                                    {
                                        join_select=join_select+ pair.second[i] + "'', ''" ;
                                    }
                                    else
                                    {
                                        join_select=join_select+ pair.second[i] + "'') AND " + table2 + "_data.ind in(''";
                                    }
                                }
                                // table2
                                std::vector<std::string> t2_values=result_map2[pair.first];
                                for(int i=0; i<t2_values.size(); i++)
                                {
                                    if(i != t2_values.size()-1)
                                    {
                                        join_select=join_select+ t2_values[i] + "'', ''" ;
                                    }
                                    else
                                    {
                                        join_select=join_select+ t2_values[i] + "'')";
                                    }
                                }
                                if (count < map_size)
                                {
                                    join_select=join_select+" UNION ";
                                }
                                else
                                {
                                    if(join_mode=="INNER" || join_mode=="UNKNOWN")
                                    {
                                        join_select=join_select+";";
                                    }
                                    if(join_mode=="LEFT")
                                    {
                                        join_select=join_select+" UNION select * ,NULL AS ind, NULL AS keyword, NULL AS values from " + table1 + "_data where ind in(''";
                                        for(int i=0;i<left_ind.size();i++)
                                        {
                                            if(i!=left_ind.size()-1)
                                            {
                                                join_select=join_select+left_ind[i]+"'', ''";
                                            }
                                            else
                                            {
                                                join_select=join_select+left_ind[i]+"'');";
                                            }
                                        }
                                        if(left_ind.size()==0)
                                        {
                                            join_select=join_select+"'');";
                                        }
                                    }
                                    if(join_mode=="RIGHT")
                                    {
                                        join_select=join_select+" UNION select NULL AS ind, NULL AS keyword, NULL AS values, * from " + table2 + "_data where ind in(''";
                                        for(int i=0;i<right_ind.size();i++)
                                        {
                                            if(i!=right_ind.size()-1)
                                            {
                                                join_select=join_select+right_ind[i]+"'', ''";
                                            }
                                            else
                                            {
                                                join_select=join_select+right_ind[i]+"'');";
                                            }
                                        }
                                        if(right_ind.size()==0)
                                        {
                                            join_select=join_select+"'');";
                                        }
                                    }
                                }
                            }
                        }
                        else//just left or right
                        {
                            if(join_mode=="LEFT")
                            {
                                join_select=join_select+"select * ,NULL AS ind, NULL AS keyword, NULL AS values from " + table1 + "_data where ind in(''";
                                for(int i=0;i<left_ind.size();i++)
                                {
                                    if(i!=left_ind.size()-1)
                                    {
                                        join_select=join_select+left_ind[i]+"'', ''";
                                    }
                                    else
                                    {
                                        join_select=join_select+left_ind[i]+"'');";
                                    }
                                }
                                if(left_ind.size()==0)
                                {
                                    join_select=join_select+"'');";
                                }
                            }
                            if(join_mode=="RIGHT")
                            {
                                join_select=join_select+"select NULL AS ind, NULL AS keyword, NULL AS values, * from " + table2 + "_data where ind in(''";
                                for(int i=0;i<right_ind.size();i++)
                                {
                                    if(i!=right_ind.size()-1)
                                    {
                                        join_select=join_select+right_ind[i]+"'', ''";
                                    }
                                    else
                                    {
                                        join_select=join_select+right_ind[i]+"'');";
                                    }
                                }
                                if(right_ind.size()==0)
                                {
                                    join_select=join_select+"'');";
                                }
                            }
                        }
                    }

                    if(join_mode=="CROSS")
                    {
                        join_select="select * from "+table1 + "_data, "+table2 + "_data where " + table1 + "_data.ind in(''";
                        size_t map1_size = t1_join_value_pt.size();
                        size_t count1 = 0;
                        size_t map2_size = t2_join_value_pt.size();
                        size_t count2 = 0;
                        for (const auto& pair1 : t1_join_value_pt) 
                        {
                            count1++;
                            if (count1 < map1_size)
                            {
                                join_select=join_select+pair1.first+"'', ''";
                            }
                            else
                            {
                                join_select=join_select+pair1.first+"'') and " +table2 + "_data.ind in(''";
                            }
                        }
                        if(map1_size==0)
                        {
                            join_select=join_select + "'') and " + table2 + "_data.ind in(''";
                        }

                        for (const auto& pair2 : t2_join_value_pt) 
                        {
                            count2++;
                            if (count2 < map2_size)
                            {
                                join_select=join_select+pair2.first+"'', ''";
                            }
                            else
                            {
                                join_select=join_select+pair2.first+"'');";
                            }
                        }
                        if(map2_size==0)
                        {
                            join_select=join_select + "'');";
                        }
                    }
                    

                    if(join_select.empty())
                    {
                        join_select="select * from "+table1 + "_data, " + table2  + "_data LIMIT 0;";
                    }
                    join_query=join_query+fides_temp+join_select;
                    const char* fides_query2=join_query.c_str(); 
                    results=PQexec(pset.db, fides_query2);
                }
                else
                {
                    const char* fides_query_re_insert=fides_temp.c_str(); 
                    PGresult* zyc_re_insert_res = PQexec(pset.db, fides_query_re_insert);
                    /* 4.2: distinguish "got a result object" from "SQL succeeded". */
                    if (zyc_re_insert_res == NULL) {
                        psql_error("Fides re-insert returned no result; "
                                   "business data lookup was NOT executed. %s\n",
                                   PQerrorMessage(pset.db));
                        PQclear(results);
                        ResetCancelConn();
                        return false;
                    }
                    if (PQresultStatus(zyc_re_insert_res) != PGRES_COMMAND_OK &&
                        PQresultStatus(zyc_re_insert_res) != PGRES_TUPLES_OK) {
                        psql_error("Fides re-insert failed (%s); "
                                   "business data lookup was NOT executed.\n",
                                   PQresStatus(PQresultStatus(zyc_re_insert_res)));
                        psql_error("%s", PQresultErrorMessage(zyc_re_insert_res));
                        /* Keep the error result for the common display path,
                         * and stop here: no fides_query_search is issued, so
                         * no successful business rows can be printed. */
                        PQclear(results);
                        results = zyc_re_insert_res;
                        break;
                    }
                    PQclear(zyc_re_insert_res);
                    const char* fides_query_search=fides_temp2.c_str(); 
                    results=PQexec(pset.db, fides_query_search);
                }
            }
            break;
        }
        case 2://mitra
        {
            if(results != nullptr && results->attDescs != nullptr && results->attDescs->name != nullptr && strcmp(results->attDescs->name, "fides_setup") != 0)
            {
                tracker.start("mitra DATA and reinsert SQL rewrite");
                vector<prf_type> records;
                vector<string> join_value;
                std::map<std::string, std::string> t2_join_value_pt;
                string sind;
                prf_type index;
                memset(index.data(), 0, AES_KEY_SIZE);
                vector<int> finalindex;
                vector<prf_type> addrs;
                vector<prf_type> vals;
                vector<string> final_join_value;
                string select_ind_array="ARRAY['";
                string select_addr_array;
                string select_val_array;
                string select_join_array;
                if(common_sse_join)
                {
                    select_addr_array="ARRAY[''";
                    select_val_array="ARRAY[''";
                    select_join_array="ARRAY[''";
                }
                else
                {
                    select_addr_array="ARRAY['";
                    select_val_array="ARRAY['";
                    select_join_array="ARRAY['";
                }
                string mitra_temp="";
                string mitra_temp2="select * from "+common_tableName + "_data where ind IN('";
                // 用于存储提取出的join匹配结果
                std::string key = generate_aes_key(storage_dir_path + "/" + join_key);
                std::map<std::string, std::vector<std::string>> result_map1;
                std::map<std::string, std::vector<std::string>> result_map2;
                std::vector<std::string> left_ind;
                std::vector<std::string> right_ind;

                for(int i=0;i<results->ntups;i++)
                {
                    sind=base64_decode(string(results->tuples[i][1].value));
                    copy(sind.begin(), sind.end(), index.begin());
                    records.push_back(index);
                    if (!PQgetisnull(results, i, 2))//delete  row  has not  joincol
                    {
                        join_value.push_back(string(results->tuples[i][2].value));
                    }
                }
                //解密和重加密
                tracker.start("mitra filter");
                // Mitra_client->searchProcess(records, common_keyword, finalindex, addrs, vals);
                Mitra_client->searchProcess(records, join_value, common_keyword, finalindex, final_join_value, addrs, vals);//
                tracker.stop("mitra filter");       
                
                for(int i=0;i<finalindex.size();i++)
                {
                    if(i!=finalindex.size()-1)
                    {
                        if(common_sse_join)
                        {
                            select_join_array=select_join_array+final_join_value[i]+"'', ''";
                        }
                        else
                        {
                            select_ind_array=select_ind_array+to_string(finalindex[i])+"', '";
                            mitra_temp2=mitra_temp2+to_string(finalindex[i])+"', '";
                            select_join_array=select_join_array+final_join_value[i]+"', '";
                        }
                    }
                    else
                    {
                        if(common_sse_join)
                        {
                            select_join_array=select_join_array+final_join_value[i]+ "'']";
                        }
                        else
                        {
                            select_ind_array=select_ind_array+to_string(finalindex[i])+"']";
                            mitra_temp2=mitra_temp2+to_string(finalindex[i])+"');";
                            select_join_array=select_join_array+final_join_value[i]+ "']";
                        }
                    }
                }
                for(int i=0;i<addrs.size();i++)
                {
                    if (i!=addrs.size()-1)
                    {
                        if(common_sse_join)
                        {
                            select_addr_array= select_addr_array + base64_encode(string(addrs[i].begin(),addrs[i].end())) + "'', ''";
                            select_val_array= select_val_array + base64_encode(string(vals[i].begin(),vals[i].end())) + "'', ''";
                        }
                        else
                        {
                            select_addr_array= select_addr_array + base64_encode(string(addrs[i].begin(),addrs[i].end())) + "', '";
                            select_val_array= select_val_array + base64_encode(string(vals[i].begin(),vals[i].end())) + "', '";
                        }                            
                    }
                    else
                    {
                        if(common_sse_join)
                        {
                            select_addr_array = select_addr_array + base64_encode(string(addrs[i].begin(),addrs[i].end())) + "'']";
                            select_val_array = select_val_array + base64_encode(string(vals[i].begin(),vals[i].end())) + "'']";
                        }
                        else
                        {
                            select_addr_array = select_addr_array + base64_encode(string(addrs[i].begin(),addrs[i].end())) + "']";
                            select_val_array = select_val_array + base64_encode(string(vals[i].begin(),vals[i].end())) + "']";
                        }                               
                    }
                }
                tracker.stop("mitra DATA and reinsert SQL rewrite");
                
                if(!common_sse_join)
                {
                    if(finalindex.size()==0)
                    {
                        mitra_temp="SELECT * FROM " + common_tableName + "_data LIMIT 0;";
                        mitra_temp2="SELECT * FROM "+common_tableName+"_data LIMIT 0;";
                    }
                    else
                    {
                        mitra_temp="select * from mitra_re_insert("+select_addr_array+", "+select_val_array+", "+ select_join_array+ ", '" + common_tableName + "');";
                    }
                    const char* mitra_query_re_insert=mitra_temp.c_str(); 
                    PQexec(pset.db, mitra_query_re_insert);
                    const char* mitra_query_search=mitra_temp2.c_str(); 
                    results=PQexec(pset.db, mitra_query_search);
                }
                else// join
                {
                    if(finalindex.size()==0)
                    {
                        mitra_temp="SELECT * FROM " + common_tableName + "_data LIMIT 0;";
                    }
                    else
                    {
                        mitra_temp="select * from mitra_reinsert("+select_addr_array+", "+select_val_array+", "+ select_join_array+ ", ''" + common_tableName + "'');";
                    }
                    join_query=join_query+mitra_temp;
                    for(int i=0;i<final_join_value.size();i++)
                    {
                        t2_join_value_pt[to_string(finalindex[i])]= aes_decrypt(base64_decode(final_join_value[i]), key);
                    }
                    string join_select=generateJoinSQL1(t1_join_value_pt, t2_join_value_pt, result_map1, result_map2, table1, table2, left_ind, right_ind, join_mode);
                    join_query=join_query+join_select+"mitrajoin;";

                    const char* fides_query2=join_query.c_str(); 
                    results=PQexec(pset.db, fides_query2);
                }
            }
            break;
        }

        case 3://fbdsse
        {
            if(results != nullptr && results->attDescs != nullptr && results->attDescs->name != nullptr)
            {
                FB_SearchRequest sreq;
                FB_UpdateRequest re_req;
                sreq = FBdsse_Client->FB_search(common_keyword);//get count

                mpz_t FB_client_bs;
                mpz_inits(FB_client_bs, nullptr);                 
                mpz_set_str(FB_client_bs, results->tuples[0][0].value, 2); 

                tracker.start("FBdsse res decrypt");
                for(int i=1;i<=sreq.count;i++)
                {
                    decrypt(FB_client_bs, FB_client_bs, common_keyword, i);
                }
                tracker.stop("FBdsse res decrypt");

                tracker.start("FBdsse DATA and reinsert rewrite");
                std::string bs(mpz_get_str(NULL, 2, FB_client_bs));
                std::vector<int> result = generate_ind(bs);
                mpz_clears(FB_client_bs, nullptr);

                //现有索引合并为1条，count也为1，重新插入     合并不了，有join
                re_req=FBdsse_Client->FB_re_insert(common_keyword, bs);
                std::string FB_reinsert_sql="insert into "+common_tableName + "_index VALUES (''" + re_req.ut + "'', ''" + re_req.e + "'', ''" + re_req.ct +"'')";

                std::string FBselect="select * from "+common_tableName+"_data where ind in('";
                for(int i=0;i<result.size();i++)
                {
                    if(i!=result.size()-1)
                    {
                        FBselect=FBselect+std::to_string(result[i]) + "', '";
                    }
                    else
                    {
                        FBselect=FBselect+ std::to_string(result[i]);
                    }
                }
                FBselect=FBselect +"')";
                std:: string FBtemp="select  fbdsse_select('" + FB_reinsert_sql + "');";//这个函数语句的text需要两个单引号
                tracker.stop("FBdsse DATA and reinsert rewrite");                

                const char* FB_query_re_insert=FBtemp.c_str(); 
                PQexec(pset.db, FB_query_re_insert);
                const char* FB_query_search=FBselect.c_str(); 
                results=PQexec(pset.db, FB_query_search);
            }
        }
        // default:
        //     break;
        }
    

        OK = GetPrintResult(&results, is_explain, is_print, query, print_error);
#ifndef WIN32
        /* Clear password related memory to avoid leaks when core. */
        if (pset.cur_cmd_interactive) {
            if ((pset.db != NULL) && (pset.db->last_query != NULL)) {
                rc = memset_s(pset.db->last_query, strlen(pset.db->last_query), 0, strlen(pset.db->last_query));
                securec_check_c(rc, "\0", "\0");
            }
        }
#endif

        if (pset.timing && is_print) {
            INSTR_TIME_SET_CURRENT(after);
            INSTR_TIME_SUBTRACT(after, before);
            elapsed_msec = INSTR_TIME_GET_MILLISEC(after);
        }

        // For EXPLAIN PERFORMANCE command, the query is sent by PQsendQuery.
        // But PQsendQuery doesn't wait for it to finish and then goes to the do-while
        // loop to process results. It is more reasonable to put ResetCancelConn here
        // so that EXPLAIN PERFORMANCE command can be canceled immediately.
        //
        ResetCancelConn();
    } else {
        /* Fetch-in-segments mode */
        OK = ExecQueryUsingCursor(query, &elapsed_msec);
        ResetCancelConn();
        results = NULL; /* PQclear(NULL) does nothing */
    }

    /* If we made a temporary savepoint, possibly release/rollback */
    if (on_error_rollback_savepoint) {
        const char* svptcmd = NULL;

        transaction_status = PQtransactionStatus(pset.db);

        switch (transaction_status) {
            case PQTRANS_INERROR:
                /* We always rollback on an error */
                svptcmd = "ROLLBACK TO pg_psql_temporary_savepoint";
                break;

            case PQTRANS_IDLE:
                /* If they are no longer in a transaction, then do nothing */
                break;

            case PQTRANS_INTRANS:

                /*
                 * Do nothing if they are messing with savepoints themselves:
                 * If the user did RELEASE or ROLLBACK, our savepoint is gone.
                 * If they issued a SAVEPOINT, releasing ours would remove
                 * theirs.
                 */
                if ((results != NULL) &&
                    (strcmp(PQcmdStatus(results), "SAVEPOINT") == 0 || strcmp(PQcmdStatus(results), "RELEASE") == 0 ||
                        strcmp(PQcmdStatus(results), "ROLLBACK") == 0))
                    svptcmd = NULL;
                else
                    svptcmd = "RELEASE pg_psql_temporary_savepoint";
                break;

            case PQTRANS_ACTIVE:
            case PQTRANS_UNKNOWN:
            default:
                OK = false;
                /* PQTRANS_UNKNOWN is expected given a broken connection. */
                if (transaction_status != PQTRANS_UNKNOWN || ConnectionUp())
                    psql_error("unexpected transaction status (%d)\n", transaction_status);
                break;
        }

        if (NULL != svptcmd) {
            PGresult* svptres = NULL;

            svptres = PQexec(pset.db, svptcmd);
            if (PQresultStatus(svptres) != PGRES_COMMAND_OK) {
                psql_error("%s", PQerrorMessage(pset.db));
                PQclear(svptres);

                PQclear(results);
                ResetCancelConn();
                return false;
            }
            PQclear(svptres);
        }
    }

    PQclear(results);

    /* Possible microtiming output */
    if (pset.timing && is_print)
        printf(_("Time: %.3f ms\n"), elapsed_msec);

    /* check for events that may occur during query execution */
    if (pset.encoding != PQclientEncoding(pset.db) && PQclientEncoding(pset.db) >= 0) {
        /* track effects of SET CLIENT_ENCODING */
        pset.encoding = PQclientEncoding(pset.db);
        pset.popt.topt.encoding = pset.encoding;
        if (!SetVariable(pset.vars, "ENCODING", pg_encoding_to_char(pset.encoding))) {
            psql_error("set variable %s failed.\n", "ENCODING");
        }
    }

    PrintNotifications();

    return OK;
}

/*
 * ExecQueryUsingCursor: run a SELECT-like query using a cursor
 *
 * This feature allows result sets larger than RAM to be dealt with.
 *
 * Returns true if the query executed successfully, false otherwise.
 *
 * If pset.timing is on, total query time (exclusive of result-printing) is
 * stored into *elapsed_msec.
 */
static bool ExecQueryUsingCursor(const char* query, double* elapsed_msec)
{
    bool OK = true;
    PGresult* results = NULL;
    PQExpBufferData buf;
    printQueryOpt my_popt = pset.popt;
    FILE* queryFout_copy = pset.queryFout;
    bool queryFoutPipe_copy = pset.queryFoutPipe;
    bool started_txn = false;
    bool did_pager = false;
    int ntuples;
    char fetch_cmd[64];
    instr_time before, after;
    int flush_error;
    errno_t rc;

    *elapsed_msec = 0;

    /* initialize print options for partial table output */
    my_popt.topt.start_table = true;
    my_popt.topt.stop_table = false;
    my_popt.topt.prior_records = 0;

    if (pset.timing)
        INSTR_TIME_SET_CURRENT(before);

    /* if we're not in a transaction, start one */
    if (PQtransactionStatus(pset.db) == PQTRANS_IDLE) {
        results = PQexec(pset.db, "START TRANSACTION");
        OK = AcceptResult(results) && (PQresultStatus(results) == PGRES_COMMAND_OK);
        PQclear(results);
        if (!OK) {
            return false;
        }
        started_txn = true;
    }

    /* Send DECLARE CURSOR */
    initPQExpBuffer(&buf);
    appendPQExpBuffer(&buf, "CURSOR _psql_cursor NO SCROLL FOR\n%s", query);

    results = PQexec(pset.db, buf.data);
    OK = AcceptResult(results) && (PQresultStatus(results) == PGRES_COMMAND_OK);
    PQclear(results);
    termPQExpBuffer(&buf);
    if (!OK) {
        goto cleanup;
    }
    if (pset.timing) {
        INSTR_TIME_SET_CURRENT(after);
        INSTR_TIME_SUBTRACT(after, before);
        *elapsed_msec += INSTR_TIME_GET_MILLISEC(after);
    }

    rc = sprintf_s(fetch_cmd, sizeof(fetch_cmd), "FETCH FORWARD %d FROM _psql_cursor", pset.fetch_count);
    check_sprintf_s(rc);

    /* prepare to write output to \g argument, if any */
    if (NULL != pset.gfname) {
        /* keep this code in sync with PrintQueryTuples */
        pset.queryFout = stdout; /* so it doesn't get closed */

        /* open file/pipe */
        if (!setQFout(pset.gfname)) {
            pset.queryFout = queryFout_copy;
            pset.queryFoutPipe = queryFoutPipe_copy;
            OK = false;
            goto cleanup;
        }
    }

    /* clear any pre-existing error indication on the output stream */
    clearerr(pset.queryFout);

    for (;;) {
        if (pset.timing)
            INSTR_TIME_SET_CURRENT(before);

        /* get FETCH_COUNT tuples at a time */
        results = PQexec(pset.db, fetch_cmd);

        if (pset.timing) {
            INSTR_TIME_SET_CURRENT(after);
            INSTR_TIME_SUBTRACT(after, before);
            *elapsed_msec += INSTR_TIME_GET_MILLISEC(after);
        }

        if (PQresultStatus(results) != PGRES_TUPLES_OK) {
            /* shut down pager before printing error message */
            if (did_pager) {
                ClosePager(pset.queryFout);
                pset.queryFout = queryFout_copy;
                pset.queryFoutPipe = queryFoutPipe_copy;
                did_pager = false;
            }

            OK = AcceptResult(results);
            psql_assert(!OK);
            PQclear(results);
            break;
        }

        ntuples = PQntuples(results);

        if (ntuples < pset.fetch_count) {
            /* this is the last result set, so allow footer decoration */
            my_popt.topt.stop_table = true;
        } else if (pset.queryFout == stdout && !did_pager) {
            /*
             * If query requires multiple result sets, hack to ensure that
             * only one pager instance is used for the whole mess
             */
            pset.queryFout = PageOutput(100000, my_popt.topt.pager);
            did_pager = true;
        }

        printQuery(results, &my_popt, pset.queryFout, pset.logfile);

        PQclear(results);

        /* after the first result set, disallow header decoration */
        my_popt.topt.start_table = false;
        my_popt.topt.prior_records += ntuples;

        /*
         * Make sure to flush the output stream, so intermediate results are
         * visible to the client immediately.  We check the results because if
         * the pager dies/exits/etc, there's no sense throwing more data at
         * it.
         */
        flush_error = fflush(pset.queryFout);

        /*
         * Check if we are at the end, if a cancel was pressed, or if there
         * were any errors either trying to flush out the results, or more
         * generally on the output stream at all.  If we hit any errors
         * writing things to the stream, we presume $PAGER has disappeared and
         * stop bothering to pull down more data.
         */
        if (ntuples < pset.fetch_count || cancel_pressed || flush_error || ferror(pset.queryFout))
            break;
    }

    /* close \g argument file/pipe, restore old setting */
    if (pset.gfname != NULL) {
        /* keep this code in sync with PrintQueryTuples */
        setQFout(NULL);

        pset.queryFout = queryFout_copy;
        pset.queryFoutPipe = queryFoutPipe_copy;

        free(pset.gfname);
        pset.gfname = NULL;
    } else if (did_pager) {
        ClosePager(pset.queryFout);
        pset.queryFout = queryFout_copy;
        pset.queryFoutPipe = queryFoutPipe_copy;
    }

cleanup:
    if (pset.timing)
        INSTR_TIME_SET_CURRENT(before);

    /*
     * We try to close the cursor on either success or failure, but on failure
     * ignore the result (it's probably just a bleat about being in an aborted
     * transaction)
     */
    results = PQexec(pset.db, "CLOSE _psql_cursor");
    if (OK) {
        OK = AcceptResult(results) && (PQresultStatus(results) == PGRES_COMMAND_OK);
    }
    PQclear(results);

    if (started_txn) {
        results = PQexec(pset.db, OK ? "COMMIT" : "ROLLBACK");
        OK = OK && AcceptResult(results) && (PQresultStatus(results) == PGRES_COMMAND_OK);
        PQclear(results);
    }

    if (pset.timing) {
        INSTR_TIME_SET_CURRENT(after);
        INSTR_TIME_SUBTRACT(after, before);
        *elapsed_msec += INSTR_TIME_GET_MILLISEC(after);
    }

    return OK;
}

/*
 * Advance the given char pointer over white space and SQL comments.
 */
static const char* skip_white_space(const char* query)
{
    int cnestlevel = 0; /* slash-star comment nest level */

    while (*query) {
        int mblen = PQmblen(query, pset.encoding);

        /*
         * Note: we assume the encoding is a superset of ASCII, so that for
         * example "query[0] == '/'" is meaningful.  However, we do NOT assume
         * that the second and subsequent bytes of a multibyte character
         * couldn't look like ASCII characters; so it is critical to advance
         * by mblen, not 1, whenever we haven't exactly identified the
         * character we are skipping over.
         */
        if (isspace((unsigned char)*query))
            query += mblen;
        else if (query[0] == '/' && query[1] == '*') {
            cnestlevel++;
            query += 2;
        } else if (cnestlevel > 0 && query[0] == '*' && query[1] == '/') {
            cnestlevel--;
            query += 2;
        } else if (cnestlevel == 0 && query[0] == '-' && query[1] == '-') {
            query += 2;

            /*
             * We have to skip to end of line since any slash-star inside the
             * -- comment does NOT start a slash-star comment.
             */
            while (*query) {
                if (*query == '\n') {
                    query++;
                    break;
                }
                query += PQmblen(query, pset.encoding);
            }
        } else if (cnestlevel > 0) {
            query += mblen;
        } else {
            break; /* found first token */
        }
    }

    return query;
}

/*
 * judge begin is belong to anonymous block or transaction,if it belong to
 * anonymous block,return false,otherwise return true. 
 *
 */
static bool is_begin_transaction(const char* query) {
    if (pg_strncasecmp(query, "begin", 5) == 0) {
        query = skip_white_space(query + 5);
        if (query[0] == ';')
            return true;
        else
            return false;
    }
    return false;
}


/*
 * Check whether a command is one of those for which we should NOT start
 * a new transaction block (ie, send a preceding BEGIN).
 *
 * These include the transaction control statements themselves, plus
 * certain statements that the backend disallows inside transaction blocks.
 */
static bool command_no_begin(const char* query)
{
    int wordlen;

    /*
     * First we must advance over any whitespace and comments.
     */
    query = skip_white_space(query);

    /*
     * Check word length (since "beginx" is not "begin").
     */
    wordlen = 0;
    while (isalpha((unsigned char)query[wordlen]))
        wordlen += PQmblen(&query[wordlen], pset.encoding);

    /*
     * Transaction control commands.  These should include every keyword that
     * gives rise to a TransactionStmt in the backend grammar, except for the
     * savepoint-related commands.
     *
     * (We assume that START must be START TRANSACTION, since there is
     * presently no other "START foo" command.)
     */

    if (is_begin_transaction(query))
        return true;

    if (wordlen == 5 && pg_strncasecmp(query, "abort", 5) == 0)
        return true;
    if (wordlen == 5 && pg_strncasecmp(query, "start", 5) == 0)
        return true;
    if (wordlen == 6 && pg_strncasecmp(query, "commit", 6) == 0)
        return true;
    if (wordlen == 3 && pg_strncasecmp(query, "end", 3) == 0)
        return true;
    if (wordlen == 8 && pg_strncasecmp(query, "rollback", 8) == 0)
        return true;
    if (wordlen == 7 && pg_strncasecmp(query, "prepare", 7) == 0) {
        /* PREPARE TRANSACTION is a TC command, PREPARE foo is not */
        query += wordlen;

        query = skip_white_space(query);

        wordlen = 0;
        while (isalpha((unsigned char)query[wordlen]))
            wordlen += PQmblen(&query[wordlen], pset.encoding);

        if (wordlen == 11 && pg_strncasecmp(query, "transaction", 11) == 0)
            return true;
        return false;
    }

    /*
     * Commands not allowed within transactions.  The statements checked for
     * here should be exactly those that call PreventTransactionChain() in the
     * backend.
     */
    if (wordlen == 6 && pg_strncasecmp(query, "vacuum", 6) == 0)
        return true;
    if (wordlen == 7 && pg_strncasecmp(query, "cluster", 7) == 0) {
        /* CLUSTER with any arguments is allowed in transactions */
        query += wordlen;

        query = skip_white_space(query);

        if (isalpha((unsigned char)query[0]))
            return false; /* has additional words */
        return true;      /* it's CLUSTER without arguments */
    }

    if (wordlen == 6 && pg_strncasecmp(query, "create", 6) == 0) {
        query += wordlen;

        query = skip_white_space(query);

        wordlen = 0;
        while (isalpha((unsigned char)query[wordlen]))
            wordlen += PQmblen(&query[wordlen], pset.encoding);

        if (wordlen == 8 && pg_strncasecmp(query, "database", 8) == 0)
            return true;
        if (wordlen == 10 && pg_strncasecmp(query, "tablespace", 10) == 0)
            return true;

        /* CREATE [UNIQUE] INDEX CONCURRENTLY isn't allowed in xacts */
        if (wordlen == 6 && pg_strncasecmp(query, "unique", 6) == 0) {
            query += wordlen;

            query = skip_white_space(query);

            wordlen = 0;
            while (isalpha((unsigned char)query[wordlen]))
                wordlen += PQmblen(&query[wordlen], pset.encoding);
        }

        if (wordlen == 5 && pg_strncasecmp(query, "index", 5) == 0) {
            query += wordlen;

            query = skip_white_space(query);

            wordlen = 0;
            while (isalpha((unsigned char)query[wordlen]))
                wordlen += PQmblen(&query[wordlen], pset.encoding);

            if (wordlen == 12 && pg_strncasecmp(query, "concurrently", 12) == 0)
                return true;
        }

        return false;
    }

    /*
     * Note: these tests will match DROP SYSTEM and REINDEX TABLESPACE, which
     * aren't really valid commands so we don't care much. The other four
     * possible matches are correct.
     */
    if ((wordlen == 4 && pg_strncasecmp(query, "drop", 4) == 0) ||
        (wordlen == 7 && pg_strncasecmp(query, "reindex", 7) == 0)) {
        query += wordlen;

        query = skip_white_space(query);

        wordlen = 0;
        while (isalpha((unsigned char)query[wordlen]))
            wordlen += PQmblen(&query[wordlen], pset.encoding);

        if (wordlen == 8 && pg_strncasecmp(query, "database", 8) == 0)
            return true;
        if (wordlen == 6 && pg_strncasecmp(query, "system", 6) == 0)
            return true;
        if (wordlen == 10 && pg_strncasecmp(query, "tablespace", 10) == 0)
            return true;
        return false;
    }

    /* DISCARD ALL isn't allowed in xacts, but other variants are allowed. */
    if (wordlen == 7 && pg_strncasecmp(query, "discard", 7) == 0) {
        query += wordlen;

        query = skip_white_space(query);

        wordlen = 0;
        while (isalpha((unsigned char)query[wordlen]))
            wordlen += PQmblen(&query[wordlen], pset.encoding);

        if (wordlen == 3 && pg_strncasecmp(query, "all", 3) == 0)
            return true;
        return false;
    }

    return false;
}

/*
 * Check whether the specified command is a SELECT (or VALUES).
 */
static bool is_select_command(const char* query)
{
    int wordlen;

    /*
     * First advance over any whitespace, comments and left parentheses.
     */
    for (;;) {
        query = skip_white_space(query);
        if (query[0] == '(') {
            query++;
        } else {
            break;
        }
    }

    /*
     * Check word length (since "selectx" is not "select").
     */
    wordlen = 0;
    while (isalpha((unsigned char)query[wordlen]))
        wordlen += PQmblen(&query[wordlen], pset.encoding);

    if (wordlen == 6 && pg_strncasecmp(query, "select", 6) == 0)
        return true;

    if (wordlen == 6 && pg_strncasecmp(query, "values", 6) == 0)
        return true;

    return false;
}

/*
 * Check whether the specified command is a EXPLAIN.
 */
static bool is_explain_command(const char* query)
{
    int wordlen;
    bool result = false;

    /*
     * First advance over any whitespace, comments and left parentheses.
     */
    for (;;) {
        query = skip_white_space(query);
        if (query[0] == '(') {
            query++;
        } else {
            break;
        }
    }

    wordlen = 0;
    while (isalpha((unsigned char)query[wordlen]))
        wordlen += PQmblen(&query[wordlen], pset.encoding);

    if (wordlen == 7 && pg_strncasecmp(query, "explain", 7) == 0)
        result = true;

    return result;
}

/*
 * Test if the current user is a database superuser.
 *
 * Note: this will correctly detect superuserness only with a protocol-3.0
 * or newer backend; otherwise it will always say "false".
 */
bool is_superuser(void)
{
    const char* val = NULL;

    if (NULL == pset.db)
        return false;

    val = PQparameterStatus(pset.db, "is_sysadmin");

    if ((val != NULL) && strcmp(val, "on") == 0)
        return true;

    return false;
}

/*
 * Test if the current session uses standard string literals.
 *
 * Note: With a pre-protocol-3.0 connection this will always say "false",
 * which should be the right answer.
 */
bool standard_strings(void)
{
    const char* val = NULL;

    if (NULL == pset.db)
        return false;

    val = PQparameterStatus(pset.db, "standard_conforming_strings");

    if ((val != NULL) && strcmp(val, "on") == 0)
        return true;

    return false;
}

/*
 * Return the session user of the current connection.
 *
 * Note: this will correctly detect the session user only with a
 * protocol-3.0 or newer backend; otherwise it will return the
 * connection user.
 */
const char* session_username(void)
{
    const char* val = NULL;

    if (NULL == pset.db)
        return NULL;

    val = PQparameterStatus(pset.db, "session_authorization");
    if (NULL != val)
        return val;
    else
        return PQuser(pset.db);
}

/* expand_tilde
 *
 * substitute '~' with HOME or '~username' with username's home dir
 *
 */
void expand_tilde(char** filename)
{
    if ((filename == NULL) || ((*filename) == NULL))
        return;

        /*
         * WIN32 doesn't use tilde expansion for file names. Also, it uses tilde
         * for short versions of long file names, though the tilde is usually
         * toward the end, not at the beginning.
         */
#ifndef WIN32

    /* try tilde expansion */
    if (**filename == '~') {
        char* fn = NULL;
        char oldp;
        char *p = NULL;
        struct passwd* pw = NULL;
        char home[MAXPGPATH];

        fn = *filename;
        *home = '\0';

        p = fn + 1;
        while (*p != '/' && *p != '\0') {
            p++;
        }

        oldp = *p;
        *p = '\0';

        if (*(fn + 1) == '\0') {
            (void)get_home_path(home, sizeof(home)); /* ~ or ~/ only */
        } else if ((pw = getpwnam(fn + 1)) != NULL) {
            strlcpy(home, pw->pw_dir, sizeof(home)); /* ~user */
        }

        *p = oldp;
        if (strlen(home) != 0) {
            char* newfn = NULL;
            errno_t rc;
            size_t len = strlen(home) + strlen(p) + 1;
            newfn = (char*)pg_malloc(len);
            rc = sprintf_s(newfn, len, "%s%s", home, p);
            check_sprintf_s(rc);

            free(fn);
            fn = NULL;
            *filename = newfn;
        }
    }
#endif

    return;
}

/*
 * Execute one query in a forked child process.
 */
static bool do_one_parallel(char* query, int fd)
{
    bool success = true;
    pid_t pid;

    if ((pid = fork()) < 0) {
        psql_error("Can't set up parallel execution for stmt: %s", query);
        success = false;
    } else if (pid == 0) {
        int j;
        char* decode_pwd = NULL;
        GS_UINT32 pwd_len = 0;
        errno_t rc = EOK;
        char* old_conninfo_values = NULL;

        /* We decode the passwd for parallel connection in child process . */
        if (pset.connInfo.values[3] != NULL) {
            decode_pwd = SEC_decodeBase64(pset.connInfo.values[3], &pwd_len);
            if (decode_pwd == NULL) {
                fprintf(stderr, "%s: decode the parallel connect value failed.", pset.progname);
                _exit(EXIT_BADCONN);
            }
            old_conninfo_values = pset.connInfo.values[3];
            pset.connInfo.values[3] = decode_pwd;
        }

        /* Get the connection for child process with parent's conninfo. */
        pset.db = PQconnectdbParams(pset.connInfo.keywords, pset.connInfo.values, true);
        if (PQstatus(pset.db) == CONNECTION_BAD) {
            fprintf(stderr, "%s: %s", pset.progname, (char*)PQerrorMessage(pset.db));
            (void)write(fd, "0", 1);
            PQfinish(pset.db);
            _exit(EXIT_BADCONN);
        }

        /* Clear sensitive memory of decode_pwd. */
        if (decode_pwd != NULL) {
            rc = memset_s(decode_pwd, strlen(decode_pwd), 0, strlen(decode_pwd));
            securec_check_c(rc, "\0", "\0");
            if (decode_pwd != NULL) {
                OPENSSL_free(decode_pwd);
                decode_pwd = NULL;
            }

            // Revert the old value for next retry connection.
            pset.connInfo.values[3] = old_conninfo_values;
        }

        /* set the child process verbosity value the same as parent's. */
        (void)PQsetErrorVerbosity(pset.db, pset.verbosity);

        /* Set the child process title for distinct with parent process. */
        if (0 == pset.max_retry_times) {
            set_proc_title();
        }

        for (j = 0; j < pset.num_guc_stmt; j++) {
            /* Send all SET/RESET statements to child process with no print. */
            /* Here we ignore the set statements errors, as something maybe change
             * after user did so.
             */
            (void)SendQuery(pset.guc_stmt[j], false, false);
        }

        /* Send the query after all SET/RESET statements have been send successful. */
        if (success) {
            success = SendQuery(query);

            /* Query fail, if need retry, invoke QueryRetryController(). */
            if (!success && pset.retry_on) {
                success = QueryRetryController(query);
            }
        }

        /* Send the child process's execute status to parent process. */
        (void)write(fd, success ? "1" : "0", 1);
        PQfinish(pset.db);
        EmptyRetryErrcodesList(pset.errcodes_list);
        /* With version > 2.22 (Euler 2.8), glibc always resets the offset of file
         * descriptors belongs to their parent process, when subprocess is being
         * "exit". This difference makes gsql hang while processing parallel commands.
         * The hang issue is caused by rereading the contents processed before.
         * "_exit" aborts the current process without cleaning the resources which will
         * be cleaned by operating systems. So, it's safe here.
         * But if some new operations need to be executed when subprocess of gsql is
         * exiting, another solution should be found(such as close the file descriptors
         * opened by parents.
         */
        _exit(success ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    return success;
}

/*
 * Execute parallel querys in many child processes and check their status.
 */
bool do_parallel_execution(int count, char** stmts)
{
    int n, fd[2];
    int i, num_parallel;
    bool success = true;
    struct pollfd ufds;

    /* Set pset.parallel here as a tag using in child process. */
    pset.parallel = true;

    /* Create the pipe for the communication between parent and child process. */
    if (pipe(fd) < 0) {
        psql_error("Can't set up communication for parallel execution\n");
        return false;
    }

    if (IsInteractiveMode()) {
        /* Detail error messages will be printed in CreateMutexForParallel. */
        if (0 != CreateMutexForParallel()) {
            close(fd[0]);
            close(fd[1]);
            return false;
        }
    }

    /* Set search_path for parallel execute temp table. */
    (void)set_searchpath_for_tmptbl(pset.db);

    /* Execute each single quey through do_one_parallel. */
    for (i = 0; i < count && (pset.parallel_num == 0 || i < pset.parallel_num); i++) {
        success = do_one_parallel(stmts[i], fd[1]);
        if (!success) {
            break;
        }
    }

    num_parallel = i;
    ufds.fd = fd[0];
    ufds.events = POLLIN | POLLPRI;
    ufds.revents = 0;
    do {
        int status, retval;
        char x;
        pid_t pid;

        /* Wait all the child processes closed and check their exit status. */
        pid = waitpid(-1, &status, 0);
        if (pid == -1) {
            psql_error("Wait child processes failed and status is: %d\n", status);
            success = false;
            break;
        }

        /* Check the executing status of child processes . */
        retval = poll(&ufds, 1, 0);
        if (retval > 0) {
            n = read(fd[0], &x, 1);
            success = success && ((x == '1') && (n == 1));
        } else {
            psql_error("Child processes exit and exit status is: %d\n", status);
            success = false;
        }
        num_parallel--;

        /* When count > parallel_num, execute left statements one by one once a child process have finished. */
        if (i < count && !(!success && pset.on_error_stop)) {
            success = do_one_parallel(stmts[i++], fd[1]);
            if (success) {
                num_parallel++;
            }
        }
    } while (num_parallel > 0);

    if (IsInteractiveMode()) {
        /* Detail error message will be printed in DestroyMutexForParallel */
        (void)DestroyMutexForParallel();
    }

    pset.parallel = false;
    close(fd[0]);
    close(fd[1]);

    return success;
}

/*
 * Record the set guc statements for child process in parallel execute.
 */
static void RecordGucStmt(PGresult* results, const char* query)
{
    char* cmdstatus = PQcmdStatus(results);
    errno_t rc = 0;

    if ((cmdstatus == NULL) || (strncmp(cmdstatus, "SET", 3) != 0 && strncmp(cmdstatus, "RESET", 5) != 0))
        return;

    /*
     * If SET/RESET statements is more than MAX_STMTS -1, realloc more memory.
     * As normal user can't set role/session, there are no sensitive information leak
     * risk for the use of realloc.
     */
    if (pset.num_guc_stmt % MAX_STMTS == 0) {
        if(NULL != pset.guc_stmt) {
            char** temp = (char**)pg_calloc(1, sizeof(char*) * (pset.num_guc_stmt + MAX_STMTS));
            rc = memcpy_s(temp, sizeof(char*) * pset.num_guc_stmt, pset.guc_stmt, sizeof(char*) * pset.num_guc_stmt);
            securec_check_c(rc, "\0", "\0");

            free(pset.guc_stmt);
            pset.guc_stmt = temp;
        }
        else {
            pset.guc_stmt = (char**)pg_calloc(1, sizeof(char*) * (pset.num_guc_stmt + MAX_STMTS));
        }
    }

    pset.guc_stmt[pset.num_guc_stmt] = (char*)pg_malloc(sizeof(char) * (strlen(query) + 1));

    /* Saved the SET/RESET statements for parallel execute. */
    rc = strncpy_s(pset.guc_stmt[pset.num_guc_stmt], strlen(query) + 1, query, strlen(query));
    securec_check_c(rc, "\0", "\0");
    pset.num_guc_stmt++;
}

/*
 * Set the child process proc name for distinction with father process.
 */
static void set_proc_title()
{
    int i = 0;
    int rc = 0;
    int len = 0;

    /* Save the argv[0] to be child process title. The len here including the terminal '\0'. */
    len = strlen(argv_para) + 1;
    argv_para += len;

    /* Clean argv[1->i] to show only "*gsql" without other para like -d postgres e.g. */
    for (i = 0; i < argv_num - 1; i++) {
        len = strlen(argv_para) + 1;
        rc = memset_s(argv_para, len, 0, len);
        securec_check_c(rc, "\0", "\0");
        argv_para += len;
    }
}

/* File lock function for parallel write/read file. */
static int file_lock(int fd, unsigned int operation)
{
    struct flock lck;
    int cmd;
    errno_t rc;

    rc = memset_s(&lck, sizeof(lck), 0, sizeof(lck));
    check_memset_s(rc);
    lck.l_whence = SEEK_SET;
    lck.l_start = 0;
    lck.l_len = 0;
    lck.l_pid = getpid();

    if (operation & LOCK_UN)
        lck.l_type = F_UNLCK;
    else if (operation & LOCK_EX)
        lck.l_type = F_WRLCK;
    else
        lck.l_type = F_RDLCK;

    if (operation & LOCK_NB)
        cmd = F_SETLK;
    else
        cmd = F_SETLKW;

    return fcntl(fd, cmd, &lck);
}

/* Set search_path for parallel execute in temp table. */
static void set_searchpath_for_tmptbl(PGconn* conn)
{
    static const char *stmt1 = "select                  \
        case                                            \
        when instr(s.setting, 'pg_temp_') = 1 then      \
            s.setting                                   \
        else                                            \
            n.nspname||','||s.setting                   \
        end                                             \
        from pg_namespace n, pg_settings s              \
        where n.oid = pg_my_temp_schema()               \
            and s.name='search_path';";
    char stmt2[128] = {0};
    char* value1 = NULL;
    PGresult* res1 = NULL;
    bool success = true;
    errno_t rc = EOK;

    ExecStatusType resStatus;

    /* Get the temp schema for parallel execute. */
    res1 = PQexec(conn, stmt1);
    resStatus = PQresultStatus(res1);
    if (resStatus != PGRES_TUPLES_OK) {
        psql_error("get temp schema failed. \n");
        PQclear(res1);
        PQfinish(conn);
        exit(1);
    }

    if (PQntuples(res1)) {
        /* Get the temp schema name. */
        value1 = PQgetvalue(res1, 0, 0);

        /* Constructe set search_path statement using temp schema name. */
        rc = sprintf_s(stmt2, sizeof(stmt2), "set search_path to %s;", value1);
        check_sprintf_s(rc);

        /* Set the search_path for parallel execute in temp table. */
        success = SendQuery(stmt2);
        if (!success) {
            psql_error("set temp schema failed. \n");
        }
    }

    if (NULL != res1)
        PQclear(res1);
    return;
}

static int CreateMutexForParallel()
{
    pset.parallelMutex = (struct parallelMutex_t*)mmap(
        NULL, sizeof(*pset.parallelMutex), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (NULL == pset.parallelMutex) {
        psql_error("Failed to create mutex for parallel execution.\n");
        return -1;
    }
    if (pset.parallelMutex == MAP_FAILED) {
        psql_error("Failed to create mutex for parallel execution.\n");
        return -1;
    }

    check_memset_s(memset_s(pset.parallelMutex, sizeof(*pset.parallelMutex), 0, sizeof(*pset.parallelMutex)));

    if (0 != pthread_mutexattr_init(&pset.parallelMutex->mutAttr)) {
        psql_error("Failed to create mutex attribute for parallel execution.\n");
        munmap(pset.parallelMutex, sizeof(*pset.parallelMutex));
        return -1;
    }

    if (0 != pthread_mutexattr_setpshared(&pset.parallelMutex->mutAttr, PTHREAD_PROCESS_SHARED)) {
        psql_error("Failed to set mutex attribute to share mode for parallel execution.\n");
        pthread_mutexattr_destroy(&pset.parallelMutex->mutAttr);
        munmap(pset.parallelMutex, sizeof(*pset.parallelMutex));
        return -1;
    }

    if (0 != pthread_mutex_init(&pset.parallelMutex->mut, &pset.parallelMutex->mutAttr)) {
        psql_error("Failed to create mutex for parallel execution.\n");
        pthread_mutexattr_destroy(&pset.parallelMutex->mutAttr);
        munmap(pset.parallelMutex, sizeof(*pset.parallelMutex));
        return -1;
    }

    return 0;
}

static int LockMutexForParallel()
{
    return pthread_mutex_lock(&pset.parallelMutex->mut);
}

static int UnlockMutexForParallel()
{
    return pthread_mutex_unlock(&pset.parallelMutex->mut);
}

static int DestroyMutexForParallel()
{
    int ret = 0;

    if (0 != pthread_mutexattr_destroy(&pset.parallelMutex->mutAttr))
        ret = -1;

    if (0 != pthread_mutex_destroy(&pset.parallelMutex->mut))
        ret = -1;

    if (0 != munmap(pset.parallelMutex, sizeof(*pset.parallelMutex)))
        ret = -1;

    pset.parallelMutex = NULL;

    return ret;
}
