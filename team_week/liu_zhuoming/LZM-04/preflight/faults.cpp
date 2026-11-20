#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdarg>
#include "libpq/Client_Sophos.h"
#include <stdexcept>

namespace {
int temporary_fd=-1;
bool after_commit=false, renamed=false, removed=false, injected=false;
bool state_fd(int fd) {
    const char* path=getenv("SSE_V1_STATE_DIR");struct stat a,b;
    return path && stat(path,&a)==0 && fstat(fd,&b)==0 && a.st_dev==b.st_dev && a.st_ino==b.st_ino;
}
bool mode(const char* name) {const char* f=getenv("STATE_FAULT");return f && !strcmp(f,name);}
bool stage(const char* suffix) {
    return (!after_commit && mode((std::string("marker-")+suffix).c_str())) ||
           (after_commit && mode((std::string("counter-")+suffix).c_str()));
}
bool inject() {
    if(injected)return false;injected=true;
    const char msg[]="FAULT_INJECTED scoped-state-io\n";
    auto real=reinterpret_cast<ssize_t(*)(int,const void*,size_t)>(dlsym(RTLD_NEXT,"write"));
    if(real)real(2,msg,sizeof(msg)-1);errno=EIO;return true;
}
}
extern "C" int openat(int dir,const char* name,int flags,...) {
    mode_t perm=0;if(flags&O_CREAT){va_list args;va_start(args,flags);perm=va_arg(args,int);va_end(args);}
    auto real=reinterpret_cast<int(*)(int,const char*,int,...)>(dlsym(RTLD_NEXT,"openat"));
    int fd=real(dir,name,flags,perm);
    if(fd>=0 && state_fd(dir) && !strncmp(name,".row-state-",11))temporary_fd=fd;
    return fd;
}
extern "C" ssize_t write(int fd,const void* data,size_t n) {
    if(fd==temporary_fd && stage("write") && inject())return -1;
    auto real=reinterpret_cast<ssize_t(*)(int,const void*,size_t)>(dlsym(RTLD_NEXT,"write"));return real(fd,data,n);
}
extern "C" int fsync(int fd) {
    if(((fd==temporary_fd && stage("fsync")) ||
        (state_fd(fd) && renamed && stage("dir-fsync")) ||
        (state_fd(fd) && removed && mode("clear-dir-fsync"))) && inject())return -1;
    auto real=reinterpret_cast<int(*)(int)>(dlsym(RTLD_NEXT,"fsync"));return real(fd);
}
extern "C" int close(int fd) {
    bool fail=fd==temporary_fd && stage("close");
    if(fd==temporary_fd)temporary_fd=-1;
    auto real=reinterpret_cast<int(*)(int)>(dlsym(RTLD_NEXT,"close"));int rc=real(fd);
    if(fail && inject())return -1;return rc;
}
extern "C" int unlinkat(int dir,const char* name,int flags) {
    bool marker=state_fd(dir) && !strcmp(name,"operation.v1");
    if(marker && mode("clear-unlink") && inject())return -1;
    auto real=reinterpret_cast<int(*)(int,const char*,int)>(dlsym(RTLD_NEXT,"unlinkat"));int rc=real(dir,name,flags);
    if(marker && rc==0)removed=true;return rc;
}
namespace sse {namespace sophos {
new_SearchRequest SophosClient::search_request(const std::string& keyword) {
    using Fn=new_SearchRequest(*)(SophosClient*,const std::string&);
    static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"_ZN3sse6sophos12SophosClient14search_requestERKSs"));
    if(!real)throw std::runtime_error("cannot resolve real Fides search");
    auto result=real(this,keyword);
    if(mode("empty-token"))result.token.clear();
    if(mode("empty-derivation"))result.derivation_key.clear();
    return result;
}
}}
extern "C" int renameat(int a,const char* b,int c,const char* d) {
    using Fn=int(*)(int,const char*,int,const char*);
    static auto real=reinterpret_cast<Fn>(dlsym(RTLD_NEXT,"renameat"));
    const char* fault=getenv("STATE_FAULT");const char* state=getenv("SSE_V1_STATE_DIR");
    struct stat x,y;
    if (fault&&state&&stat(state,&x)==0&&fstat(c,&y)==0&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino) {
        if ((!strcmp(fault,"marker")&&!strcmp(d,"operation.v1")) ||
            (!strcmp(fault,"counter")&&!strcmp(d,"fides_client.mat")) ||
            (!strcmp(fault,"mapping")&&!strcmp(d,"sse_table.mat"))) {errno=EIO;return -1;}
    }
    if (!real) {errno=ENOSYS;return -1;} int rc=real(a,b,c,d);
    if(rc==0 && state_fd(c) && ((!after_commit && !strcmp(d,"operation.v1")) || (after_commit && !strcmp(d,"fides_client.mat"))))renamed=true;
    return rc;
}
extern "C" void sse_v1_request_event(const char* phase) {
    const std::string text=std::string("STATE_EVENT ")+phase+"\n";
    (void)!write(2,text.data(),text.size());
    if(!strcmp(phase,"search_commit")){after_commit=true;renamed=false;}
    const char* fault=getenv("STATE_FAULT");
    if (fault&&!strcmp(fault,"kill")&&!strcmp(phase,"counter_advanced")) kill(getpid(),SIGKILL);
    if (fault&&!strcmp(fault,"readonly")&&!strcmp(phase,"search_commit")) {
        const char* state=getenv("SSE_V1_STATE_DIR");
        if (!state || chmod(state,0500)!=0) _exit(125);
    }
    if (fault&&!strcmp(fault,"pause")&&!strcmp(phase,"begin_durable")) {
        const char* dir=getenv("STATE_SIGNAL_DIR"); if (!dir) _exit(125);
        const std::string root(dir);int fd=open((root+"/ready").c_str(),O_CREAT|O_WRONLY|O_EXCL,0600);
        if(fd<0)_exit(125);close(fd);
        for(int n=0;n<600;++n) {if(access((root+"/release").c_str(),F_OK)==0)return;usleep(100000);}
        _exit(124);
    }
}
