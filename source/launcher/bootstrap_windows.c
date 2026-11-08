#include <windows.h>
#include <wchar.h>
#include <stdio.h>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR args,int show) {
    (void)instance;(void)previous;(void)show;
    wchar_t root[32768],python[32768],script[32768],command[32768];
    DWORD n=GetModuleFileNameW(NULL,root,32768);
    if(!n || n>=32768)return 1;
    wchar_t *slash=wcsrchr(root,L'\\');if(!slash)return 1;*slash=0;
    if(swprintf(python,32768,L"%ls\\launcher-runtime\\pythonw.exe",root)<0 ||
       swprintf(script,32768,L"%ls\\sst_launcher.py",root)<0 ||
       swprintf(command,32768,L"\"%ls\" -I \"%ls\" --app-dir \"%ls\" %ls",python,script,root,args?args:L"")<0)return 1;
    STARTUPINFOW startup={0};startup.cb=sizeof startup;PROCESS_INFORMATION process={0};
    if(!CreateProcessW(python,command,NULL,NULL,FALSE,0,NULL,root,&startup,&process)) {
        MessageBoxW(NULL,L"The SST launcher runtime is missing or could not start. Extract the entire launcher ZIP into a writable folder, then open this shortcut again.",L"Skyward Sword Together",MB_OK|MB_ICONERROR);return 1;
    }
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return 0;
}
