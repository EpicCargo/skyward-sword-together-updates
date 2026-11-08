// Runs inside a fresh temporary directory supplied by the test runner.
#include "../src/sst_backup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <unistd.h>
#define make_dir(p) mkdir(p,0755)
#endif
int main(void) {
    assert(!make_dir("source"));
    FILE *f=fopen("source/save.bin","wb"); assert(f);
    assert(fwrite("portable save fixture",1,21,f)==21); assert(!fclose(f));
    char root[1024]="nested/backups",result[1200],why[160];
#ifdef _WIN32
    assert(GetFullPathNameA("nested/backups",sizeof root,root,NULL)>0); // absolute drive + backslashes
#endif
    assert(sst_backup_saves("source",root,1,result,sizeof result,why,sizeof why));
    char first[1200]; snprintf(first,sizeof first,"%s",result);
    assert(sst_backup_saves("source",root,1,result,sizeof result,why,sizeof why));
    assert(strcmp(first,result));
    struct stat st; assert(stat(first,&st));
    char path[1500],bytes[22]={0}; snprintf(path,sizeof path,"%s/save.bin",result);
    f=fopen(path,"rb"); assert(f); assert(fread(bytes,1,21,f)==21); fclose(f);
    assert(!strcmp(bytes,"portable save fixture"));
    assert(!sst_backup_saves("missing-source",root,1,result,sizeof result,why,sizeof why));
    assert(why[0]);
    puts("portable backups: nested paths, Windows drive paths, contents, retention and missing source PASS");
}
