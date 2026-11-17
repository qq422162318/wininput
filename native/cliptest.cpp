#include "WinInputCommon.h"
#include <cstdio>
int main(){
    wchar_t dir[MAX_PATH]={}; GetTempPathW(MAX_PATH,dir);
    std::wstring src=std::wstring(dir)+L"wi_src.png";
    HANDLE h=CreateFileW(src.c_str(),GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    if(h==INVALID_HANDLE_VALUE){fprintf(stderr,"create src fail\n");return 1;}
    const char* px="\x89PNG\r\n\x1a\n"; DWORD w=0; WriteFile(h,px,8,&w,0); CloseHandle(h);
    std::string drop=clip_dropfiles_buffer({src});
    std::string transfer;
    if(!clip_transfer_files(drop,transfer)){fprintf(stderr,"transfer fail\n");DeleteFileW(src.c_str());return 1;}
    std::string rebuilt;
    if(!clip_rebuild_files(transfer,rebuilt)){fprintf(stderr,"rebuild fail\n");DeleteFileW(src.c_str());return 1;}
    auto names=clip_dropfiles_names(rebuilt);
    fprintf(stderr,"rebuilt names=%zu\n",names.size());
    bool ok=names.size()==1;
    for(auto& n:names){
        DWORD attr=GetFileAttributesW(n.c_str());
        bool exist=attr!=INVALID_FILE_ATTRIBUTES;
        fprintf(stderr,"  '%ls' exist=%d\n",n.c_str(),exist?1:0);
        std::string content;
        if(exist&&clip_read_file(n,content)&&content.size()==8) ok=ok&&true; else ok=false;
    }
    DeleteFileW(src.c_str());
    fprintf(stderr,"roundtrip %s\n",ok?"OK":"FAIL");
    return ok?0:1;
}
