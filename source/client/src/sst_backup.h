// Save backups: before the mod writes any network progress into a save, the save folder is copied to
// <backup_root>/<YYYYmmdd-HHMMSS>/ and only the newest `keep` backups are kept.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Copies the regular files of save_dir (one level, which is how SkyRecomp stores slots). Writes the
// backup folder's path into out (may be NULL). false + why on failure; the save is never modified.
bool sst_backup_saves(const char *save_dir, const char *backup_root, int keep, char *out, size_t out_size,
                      char *why, size_t why_size);

#ifdef __cplusplus
}
#endif
