// Bounded metadata-only interpretation of the pinned, already converted BRRES.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SST_MODEL_FILE_LIMIT (128u*1024u*1024u)
#define SST_MODEL_READ_BUDGET 65536u
// Caller supplies safe reads under native lifetime exclusion. File extent alone
// is NOT allocated-memory/lifetime proof. No geometry/shader bytes are requested.
typedef bool (*SstModelRead)(void *context,uint64_t offset,void *out,size_t bytes);
typedef struct {uint32_t file_size,matched_mask,corroborated_mask,bytes_read;} SstModelBindings;
bool sst_model_bindings_validate(SstModelRead read,void *context,uint64_t available,
                                 const uint64_t resources[4],SstModelBindings *out);
#ifdef __cplusplus
}
#endif
