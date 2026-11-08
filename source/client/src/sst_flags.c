// See sst_flags.h.
#include "sst_flags.h"

#include <string.h>

void sst_flags_init(SstFlags *f) { memset(f, 0, sizeof *f); }

bool sst_flags_register(SstFlags *f, uint8_t kind, uint8_t *live, size_t size, const uint8_t *sync_mask) {
    if (kind >= SST_FLAG_SPACES || size > SST_FLAG_SPACE_MAX || !live) return false;
    SstFlagSpace *s = &f->space[kind];
    s->live = live;
    s->size = size;
    s->sync_mask = sync_mask;
    s->used = true;
    memcpy(s->snapshot, live, size);
    return true;
}

void sst_flags_rebase(SstFlags *f) { f->primed = false; }

static uint8_t mask_at(const SstFlagSpace *s, size_t i) { return s->sync_mask ? s->sync_mask[i] : 0xff; }

void sst_flags_tick(SstFlags *f, SstFlagSendFn send, void *user) {
    for (int k = 0; k < SST_FLAG_SPACES; k++) {
        SstFlagSpace *s = &f->space[k];
        if (!s->used) continue;
        if (!f->primed) { memcpy(s->snapshot, s->live, s->size); continue; }
        for (size_t i = 0; i < s->size; i++) {
            uint8_t now = s->live[i];
            if (now == s->snapshot[i]) continue;
            uint8_t rose = (uint8_t)(now & ~s->snapshot[i] & mask_at(s, i));
            for (int b = 0; b < 8; b++)
                if (rose & (1u << b)) { send((uint8_t)k, (uint16_t)(i * 8 + b), user); f->sent++; }
            s->snapshot[i] = now;  // cleared bits are accepted locally but never sent
        }
    }
    f->primed = true;
}

bool sst_flags_apply(SstFlags *f, uint8_t kind, uint16_t bit) {
    if (kind >= SST_FLAG_SPACES) return false;
    SstFlagSpace *s = &f->space[kind];
    size_t i = bit / 8;
    uint8_t m = (uint8_t)(1u << (bit % 8));
    if (!s->used || i >= s->size || !(mask_at(s, i) & m)) return false;  // unknown or not shared: ignore
    if (s->live[i] & m) return false;
    s->live[i] |= m;
    s->snapshot[i] |= m;  // ours now, and not an echo to send back
    f->applied++;
    return true;
}

void sst_flags_announce_all(SstFlags *f, SstFlagSendFn send, void *user) {
    for (int k = 0; k < SST_FLAG_SPACES; k++) {
        const SstFlagSpace *s = &f->space[k];
        if (!s->used) continue;
        for (size_t i = 0; i < s->size; i++) {
            uint8_t v = (uint8_t)(s->live[i] & mask_at(s, i));
            for (int b = 0; b < 8; b++)
                if (v & (1u << b)) send((uint8_t)k, (uint16_t)(i * 8 + b), user);
        }
    }
}
