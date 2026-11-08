// See sst_backup.h.
#include "sst_backup.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#define sst_mkdir(path, mode) _mkdir(path)
#else
#define sst_mkdir(path, mode) mkdir(path, mode)
#endif

static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[1 << 16];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    return ok;
}

static int mkdirs(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        bool separator = *p == '/';
#ifdef _WIN32
        separator = separator || *p == '\\';
        if (p == tmp + 2 && tmp[1] == ':') continue; // existing drive root
#endif
        if (separator) {
            char saved = *p; *p = 0;
            if (sst_mkdir(tmp, 0755) && errno != EEXIST) return -1;
            *p = saved;
        }
    }
    return sst_mkdir(tmp, 0755) && errno != EEXIST ? -1 : 0;
}

static void remove_tree(const char *dir) {  // backups are flat: files only
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    char p[2048];
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) { snprintf(p, sizeof p, "%s/%s", dir, e->d_name); unlink(p); }
    closedir(d);
    rmdir(dir);
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void prune(const char *root, int keep) {
    DIR *d = opendir(root);
    if (!d) return;
    char *names[512];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 512)
        if (strlen(e->d_name) == 15 && e->d_name[8] == '-') names[n++] = strdup(e->d_name);  // YYYYmmdd-HHMMSS
    closedir(d);
    qsort(names, n, sizeof *names, cmp_str);
    char p[2048];
    for (int i = 0; i < n; i++) {
        if (i < n - keep) { snprintf(p, sizeof p, "%s/%s", root, names[i]); remove_tree(p); }
        free(names[i]);
    }
}

bool sst_backup_saves(const char *save_dir, const char *backup_root, int keep, char *out, size_t out_size,
                      char *why, size_t why_size) {
    DIR *d = opendir(save_dir);
    if (!d) { snprintf(why, why_size, "open %s: %s", save_dir, strerror(errno)); return false; }
    char stamp[32], dest[1024];
    time_t t = time(NULL);
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
    snprintf(dest, sizeof dest, "%s/%s", backup_root, stamp);
    while (access(dest, F_OK) == 0) {  // two backups in one second: the second waits for a fresh stamp
        sleep(1);
        t = time(NULL);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
        snprintf(dest, sizeof dest, "%s/%s", backup_root, stamp);
    }
    if (mkdirs(dest)) { closedir(d); snprintf(why, why_size, "create %s: %s", dest, strerror(errno)); return false; }
    struct dirent *e;
    int copied = 0;
    bool ok = true;
    while ((e = readdir(d))) {
        char src[2048], dst[2048];
        snprintf(src, sizeof src, "%s/%s", save_dir, e->d_name);
        struct stat st;
        if (stat(src, &st) || !S_ISREG(st.st_mode)) continue;
        snprintf(dst, sizeof dst, "%s/%s", dest, e->d_name);
        if (!copy_file(src, dst)) { snprintf(why, why_size, "copy %s failed", src); ok = false; break; }
        copied++;
    }
    closedir(d);
    if (!ok) { remove_tree(dest); return false; }
    if (out) snprintf(out, out_size, "%s", dest);
    if (keep > 0) prune(backup_root, keep);
    (void)copied;
    return true;
}
