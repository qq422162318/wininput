#include "WinInputCommon.h"
#include <cstdio>
int main(){
    if(!OpenClipboard(0)){fprintf(stderr,"open fail\n");return 1;}
    // 1) make a real source file
    wchar_t dir[MAX_PATH]={}; GetTempPathW(MAX_PATH,dir);
    std::wstring src=std::wstring(dir)+L"wi_e2e.png";
    HANDLE h=CreateFileW(src.c_str(),GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    if(h==INVALID_HANDLE_VALUE){fprintf(stderr,"src create fail\n");CloseClipboard();return 1;}
    DWORD w=0; WriteFile(h,"\x89PNG\r\n\x1a\nEND",12,&w,0); CloseHandle(h);
    // 2) put a real CF_HDROP on the local clipboard, like Explorer does
    std::string drop=clip_dropfiles_buffer({src});
    EmptyClipboard();
    HGLOBAL g=GlobalAlloc(GMEM_MOVEABLE,drop.size());
    memcpy(GlobalLock(g),drop.data(),drop.size()); GlobalUnlock(g);
    if(!SetClipboardData(CF_HDROP,g)){fprintf(stderr,"set hdrop fail\n");CloseClipboard();return 1;}
    CloseClipboard();
    // 3) serialize from the real clipboard (sender side)
    bool skipped=false;
    std::string payload=clip_serialize(skipped);
    if(payload.empty()){fprintf(stderr,"serialize empty\n");return 1;}
    fprintf(stderr,"serialized %zu bytes skipped=%d\n",payload.size(),skipped?1:0);
    // 4) deserialize onto the real clipboard (receiver side)
    int n=clip_deserialize(payload);
    fprintf(stderr,"deserialize wrote %d formats\n",n);
    if(n<1){fprintf(stderr,"nothing written\n");return 1;}
    // 5) read CF_HDROP back off the clipboard and confirm the file exists
    if(!OpenClipboard(0)){fprintf(stderr,"reopen fail\n");return 1;}
    HANDLE hd=GetClipboardData(CF_HDROP);
    if(!hd){fprintf(stderr,"no CF_HDROP on clipboard\n");CloseClipboard();return 1;}
    SIZE_T sz=GlobalSize(hd); void* p=GlobalLock(hd);
    std::string reb((const char*)p,sz); GlobalUnlock(hd); CloseClipboard();
    auto names=clip_dropfiles_names(reb);
    fprintf(stderr,"clipboard CF_HDROP names=%zu\n",names.size());
    bool ok=names.size()==1;
    for(auto& nm:names){
        bool exist=GetFileAttributesW(nm.c_str())!=INVALID_FILE_ATTRIBUTES;
        std::string content;
        bool read=exist&&clip_read_file(nm,content);
        fprintf(stderr,"  '%ls' exist=%d bytes=%zu\n",nm.c_str(),exist?1:0,read?content.size():0);
        ok=ok&&exist&&read&&content.size()==12;
    }
    DeleteFileW(src.c_str());
    fprintf(stderr,"E2E %s\n",ok?"OK":"FAIL");
    return ok?0:1;
}
