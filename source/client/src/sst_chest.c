#include "sst_chest.h"
#include <stddef.h>
#include "sst_reward_catalogue.inc"

bool sst_chest_collector_only_resource(uint16_t item) {
    return item==2 || item==3 || item==4 || item==32 || item==33 || (item>=161 && item<=176);
}

bool sst_chest_item(uint16_t item, unsigned *counter, int *amount) {
    unsigned c = 0; int n = 1;
    switch (item) {
    case 1: c = 6; break;
    case 2: n = 1; break;
    case 3: n = 5; break;
    case 4: n = 20; break;
    case 28: c = 5; break;
    case 32: n = 100; break;
    case 33: n = 300; break;
    case 35: c = 38; n = 5; break;
    case 48: c = 38; break; // HD Gratitude Crystal counter (logical item flag 502)
    case 94: c = 8; break; // native remainder counter; health effects run in the game bridge
    default:
        if (item >= 161 && item <= 176) c = 22 + item - 161;
        else return false;
    }
    if (counter) *counter = c;
    if (amount) *amount = n;
    return true;
}
bool sst_chest_key_table(unsigned table) {
    // Vanilla small keys: Lanayru Caves, Skyview, Ancient Cistern, Fire Sanctuary,
    // Lanayru Mining Facility, Sandship and Sky Keep. Earth Temple uses key pieces.
    return table == 7 || table == 11 || table == 12 || table == 15 ||
           table == 17 || table == 18 || table == 20;
}
bool sst_chest_dungeon(uint16_t item, unsigned table, unsigned *flag) {
    unsigned source = 26, logical = 12;
    switch (item) {
    case 25: source = 12; break; // Ancient Cistern
    case 26: source = 15; break; // Fire Sanctuary
    case 27: source = 18; break; // Sandship
    case 29: source = 11; break; // Skyview
    case 30: source = 14; break; // Earth Temple
    case 31: source = 17; break; // Lanayru Mining Facility
    case 50:
        logical = 2; // native map item; item 51 is a different, unused ownership item
        if (table == 11 || table == 12 || table == 14 || table == 15 ||
            table == 17 || table == 18 || table == 20) source = table;
        break;
    default: return false;
    }
    if (source >= 26 || source != table) return false;
    if (flag) *flag = logical;
    return true;
}
bool sst_chest_valid(const SstChestReward *r) {
    if (!r) return false;
    if (sst_chest_batreaux_item(r->index) && r->item != sst_chest_batreaux_item(r->index)) return false;
    if (r->index >= SST_CONTAINER_CHESTS)
        return (r->item == 35 && sst_chest_quest_flag(r->index)) ||
            (sst_chest_shop_item(r->index) && r->item == sst_chest_shop_item(r->index)) ||
            (sst_chest_first_pouch_item(r->index) && r->item == sst_chest_first_pouch_item(r->index)) ||
            (sst_chest_pouch_gift_item(r->index) && r->item == sst_chest_pouch_gift_item(r->index)) ||
            (sst_chest_wallet_gift_item(r->index) && r->item == sst_chest_wallet_gift_item(r->index)) ||
            (sst_chest_trial_flag(r->index) && r->item == sst_chest_trial_item(r->index));
    unsigned table;
    return r && sst_chest_location(r->index, NULL, &table, NULL) &&
        ((r->item != 35 && sst_chest_item(r->item, NULL, NULL) && (r->item != 1 || sst_chest_key_table(table)) &&
          (r->item != 28 || sst_chest_key_piece(r->index)) &&
          (r->item != 48 || sst_chest_crystal_pickup(r->index))) ||
         sst_chest_dungeon(r->item, table, NULL) || sst_chest_sword_tier(r->item) || sst_chest_tool(r->item) || sst_chest_gear(r->item) || sst_chest_wallet(r->item) || sst_chest_pouch_expansion(r->item) || sst_chest_pouch_quantity(r->item));
}
uint16_t sst_chest_sword_item(unsigned tier) {
    static const uint16_t items[] = {10,11,12,9,13,14};
    return tier && tier <= 6 ? items[tier - 1] : 0;
}
unsigned sst_chest_sword_tier(uint16_t item) {
    for (unsigned tier=1;tier<=6;tier++) if (sst_chest_sword_item(tier)==item) return tier;
    return 0;
}
unsigned sst_chest_sword_best(unsigned owned_mask) {
    if (owned_mask >= 64) return 0;
    for (unsigned tier=6;tier; tier--) if (owned_mask & (1u << (tier-1))) return tier;
    return 0;
}
unsigned sst_chest_sword_target(unsigned live_mask, unsigned saved_mask, uint16_t incoming) {
    unsigned tier=sst_chest_sword_tier(incoming);
    if (!tier || live_mask>=64 || saved_mask>=64) return 0;
    unsigned owned=sst_chest_sword_best(live_mask|saved_mask);
    return owned>tier?owned:tier;
}
bool sst_chest_tool(uint16_t item) {
    return item == 19 || item == 20 || item == 49 || item == 52 || item == 53 ||
        item == 71 || item == 75 || item == 76 || item == 77 || item == 90 || item == 91 ||
        item == 92 || item == 105 || item == 137 || item == 140;
}
bool sst_chest_tool_gift(uint16_t item) {
    return item == 20 || item == 52 || item == 71 || item == 75 || item == 76 ||
        item == 77 || item == 90 || item == 91 || item == 105 || item == 140;
}
uint16_t sst_chest_tool_prerequisite(uint16_t item, unsigned slot) {
    switch (item) {
    case 75: return slot == 0 ? 53 : 0;
    case 76: return slot == 0 ? 53 : slot == 1 ? 75 : 0;
    case 77: return slot == 0 ? 53 : slot == 1 ? 75 : slot == 2 ? 76 : 0;
    case 90: return slot == 0 ? 19 : 0;
    case 91: return slot == 0 ? 19 : slot == 1 ? 90 : 0;
    case 105: return slot == 0 ? 52 : 0;
    case 140: return slot == 0 ? 71 : 0;
    default: return 0;
    }
}
bool sst_chest_gear(uint16_t item) {
    return item == 56 || item == 68 || item == 99 || item == 138;
}
bool sst_chest_wallet(uint16_t item) { return item >= 108 && item <= 111; }
bool sst_chest_wallet_satisfied(uint16_t item, unsigned owned_mask) {
    return sst_chest_wallet(item) && owned_mask < 16 && (owned_mask >> (item - 108)) != 0;
}
unsigned sst_chest_wallet_limit(unsigned owned_mask, unsigned extra_wallets) {
    static const unsigned limits[] = {300, 500, 1000, 5000, 9000};
    if (owned_mask >= 16 || extra_wallets > 3) return 0;
    unsigned tier = 0;
    for (unsigned i = 0; i < 4; i++) if (owned_mask & (1u << i)) tier = i + 1;
    return limits[tier] + extra_wallets * 300;
}
uint16_t sst_chest_batreaux_item(unsigned index) {
    static const uint16_t rewards[] = {108, 94, 109, 33, 110, 33};
    return index >= 902 && index <= 907 ? rewards[index - 902] : 0;
}
unsigned sst_chest_reward_quantity(unsigned index, uint16_t item) {
    return index == 907 && item == 33 ? 2 : 1; // seventy crystals: two Gold Rupees
}
uint16_t sst_chest_wallet_gift_item(unsigned index) {
    return index == SST_CONTAINER_CHESTS + 360 ? 111 : 0;
}
bool sst_chest_bottle(uint16_t item) { return item == 126 || item == 153; }
bool sst_chest_pouch_expansion(uint16_t item) { return item == 113; }
unsigned sst_chest_pouch_expansion_target(unsigned live, unsigned saved, unsigned completed, bool incoming) {
    if (live > SST_POUCH_EXPANSIONS || saved > SST_POUCH_EXPANSIONS) return SST_POUCH_EXPANSIONS + 1;
    unsigned target = live > saved ? live : saved;
    if (incoming && target < SST_POUCH_EXPANSIONS) target++;
    unsigned proven = completed < SST_POUCH_EXPANSIONS ? completed + (incoming ? 1 : 0) : SST_POUCH_EXPANSIONS;
    return target > proven ? target : proven;
}
unsigned sst_chest_shield_durability(uint16_t item) {
    static const unsigned durability[] = {16,24,32,24,36,48,12,16,20,48};
    return item >= 116 && item <= 125 ? durability[item - 116] : 0;
}
uint16_t sst_chest_shield_item(unsigned index) {
    return index >= SST_SHIELD_CHESTS && index < SST_AMMO_CHESTS ? 116 + index - SST_SHIELD_CHESTS : 0;
}
unsigned sst_chest_ammo_quantity(uint16_t item) {
    static const unsigned ammo[] = {10,20,30,5,10,15,5,10,15};
    return item >= 128 && item <= 136 ? ammo[item - 128] : 0;
}
uint16_t sst_chest_ammo_item(unsigned index) {
    return index >= SST_AMMO_CHESTS && index < SST_CHESTS ? 128 + index - SST_AMMO_CHESTS : 0;
}
uint16_t sst_chest_first_pouch_item(unsigned index) {
    uint16_t shield = sst_chest_shield_item(index);
    return shield ? shield : sst_chest_ammo_item(index);
}
bool sst_chest_first_pouch_word(uint16_t item, uint32_t word) {
    unsigned shield = sst_chest_shield_durability(item), ammo = sst_chest_ammo_quantity(item);
    unsigned amount = word >> 16;
    return (word & 0xffff) == item && ((shield && amount && amount <= shield) || (ammo && amount <= ammo));
}
unsigned sst_chest_pouch_quantity(uint16_t item) {
    unsigned shield = sst_chest_shield_durability(item), ammo = sst_chest_ammo_quantity(item);
    if (shield || ammo) return shield ? shield : ammo;
    return sst_chest_bottle(item) || item == 70 || (item >= 100 && item <= 104) || item == 114 ? 1 : 0;
}
SstPouchResult sst_chest_pouch_result(unsigned slot, uint16_t item, unsigned result,
    const uint32_t before[SST_POUCH_SLOTS], const uint32_t after[SST_POUCH_SLOTS]) {
    unsigned quantity = sst_chest_pouch_quantity(item);
    if (slot >= SST_POUCH_SLOTS || !quantity || !before || !after || (before[slot] & 0xffff))
        return SST_POUCH_CONFLICT;
    bool unchanged = true, expected = true;
    for (unsigned i = 0; i < SST_POUCH_SLOTS; i++) {
        if (after[i] != before[i]) unchanged = false;
        if (after[i] != (i == slot ? (quantity << 16) | item : before[i])) expected = false;
    }
    if (result == 0 && unchanged) return SST_POUCH_WAIT;
    return result == 1 && expected ? SST_POUCH_APPLIED : SST_POUCH_CONFLICT;
}
unsigned sst_chest_pouch_slot(bool owned, unsigned expansions, const uint16_t items[SST_POUCH_SLOTS]) {
    if (!owned || expansions > 4 || !items) return SST_POUCH_SLOTS;
    for (unsigned i = 0; i < 4 + expansions; i++) if (!items[i]) return i;
    return SST_POUCH_SLOTS;
}
uint16_t sst_chest_saved_reward(unsigned index) {
    return index < SST_CONTAINER_CHESTS ? saved_rewards[index] : 0;
}
uint16_t sst_chest_pouch_gift_item(unsigned index) {
    // Potion Lady: 106-DrugStore flow 44 gives 153, then flow 41 sets story 35.
    return index == SST_CONTAINER_CHESTS + 35 ? 153 : index == SST_CONTAINER_CHESTS + 814 ? 114 : 0;
}
uint16_t sst_chest_shop_item(unsigned index) {
    // Vanilla ShpSmpl purchase methods set these after spawning their reward.
    if (index >= SST_SHOP_CHESTS && index < SST_BUG_MEDAL_CHEST) return index < SST_SHOP_CHESTS + 3 ? 113 : 199;
    if (index == SST_BUG_MEDAL_CHEST) return 70;
    return index == SST_CONTAINER_CHESTS + 813 ? 94 : index == SST_CONTAINER_CHESTS + 814 ? 114 : 0;
}
uint16_t sst_chest_unique_pouch_item(unsigned index) { return index == SST_BUG_MEDAL_CHEST ? 70 : 0; }
bool sst_chest_shop_counter(unsigned index, unsigned *flag, unsigned *ordinal) {
    if (index < SST_SHOP_CHESTS || index >= SST_BUG_MEDAL_CHEST) return false;
    unsigned slot = index - SST_SHOP_CHESTS;
    if (flag) *flag = slot < 3 ? 707 : 708;
    if (ordinal) *ordinal = slot % 3 + 1;
    return true;
}
unsigned sst_chest_trial_flag(unsigned index) {
    if (index < SST_CONTAINER_CHESTS || index >= SST_CHESTS) return 0;
    unsigned flag = index - SST_CONTAINER_CHESTS;
    return flag >= 206 && flag <= 209 ? flag : 0;
}
uint16_t sst_chest_trial_item(unsigned index) {
    switch (sst_chest_trial_flag(index)) {
    case 206: return 68; // Farore: Water Dragon's Scale
    case 207: return 138; // Din: Fireshield Earrings
    case 208: return 20; // Nayru: Clawshots
    case 209: return 180; // Hylia: Stone of Trials; story 210 is not part of the reward
    default: return 0;
    }
}
bool sst_chest_trial_reserved(unsigned flag) { return flag >= 206 && flag <= 210; }
unsigned sst_chest_quest_flag(unsigned index) {
    if (index < SST_CONTAINER_CHESTS || index >= SST_CHESTS) return 0;
    unsigned flag = index - SST_CONTAINER_CHESTS;
    switch (flag) {
    case 326: case 373: case 383: case 384: case 387: case 394: case 398:
    case 472: case 477: case 482: case 553: case 561: case 575: case 636: case 639: return flag;
    default: return 0;
    }
}
unsigned sst_chest_quest_aux(unsigned flag, unsigned slot) {
    // Pipit's reward sets 561, then these six flags. Sleep/day-change cleanup
    // also sets 558, so 558 alone is not proof of a crystal grant.
    static const unsigned pipit[SST_QUEST_AUX] = {558, 559, 557, 560, 663, 554};
    if (slot >= SST_QUEST_AUX) return 0;
    if (flag == 561) return pipit[slot];
    if (slot) return 0;
    return flag == 575 ? 361 : flag == 387 ? 386 : flag == 482 ? 481 : flag == 636 ? 635 : flag == 384 ? 382 : 0;
}
unsigned sst_chest_quest_alternative(unsigned flag) {
    return flag == 636 ? 639 : flag == 639 ? 636 : flag == 553 ? 561 : flag == 561 ? 553 : 0;
}
bool sst_chest_quest_letter(unsigned flag) { return flag == 553 || flag == 561; }
unsigned sst_chest_quest_target(unsigned flag, bool toilet, bool karane) {
    if (!sst_chest_quest_letter(flag)) return flag;
    if (toilet == karane) return 0; // no choice yet, or inconsistent personal branches
    return toilet ? 553 : 561;
}
bool sst_chest_quest_reserved(unsigned flag) {
    switch (flag) {
    // Include unsupported packs: their completion cannot hide an unimplemented personal reward.
    case 326: case 361: case 373: case 383: case 384: case 386: case 387: case 394: case 398:
    case 472: case 473: case 477: case 478: case 481: case 482: case 553: case 558: case 575:
    case 635: case 636: case 639: return true;
    // Pipit's crystal route also finishes these flags (115-Town2 flow 641 onward).
    // Sharing them alone can finish the alternative quest before its personal grant.
    case 554: case 557: case 559: case 560: case 561: case 663: return true;
    // Letter ownership/hand-over and alternative branch prerequisites stay personal.
    case 547: case 548: case 549: case 550: case 551: case 552:
    case 555: case 556: case 664: case 771: return true;
    // Parrow's loan flag proves personal bottle delivery; full-pouch refusal stays personal.
    case 382: case 395: return true;
    case 360: return true; // Batreaux final wallet and human transformation are one transaction
    case 35: return true; // Potion Lady's bottle requires its personal pouch transaction
    case 813: case 814: return true; // Beedle's purchase and its reward travel together
    case 707: case 708: return true; // ordered purchase counters are never generic flags
    default: return false;
    }
}
bool sst_chest_key_piece(unsigned index) {
    // F200 permanent scene table 4: native Soil subtype 4, collection flags 8,9,10,12,64.
    return index == SST_TBOX_CHESTS + 4 * 128 + 8 ||
           index == SST_TBOX_CHESTS + 4 * 128 + 9 ||
           index == SST_TBOX_CHESTS + 4 * 128 + 10 ||
           index == SST_TBOX_CHESTS + 4 * 128 + 12 ||
           index == SST_TBOX_CHESTS + 4 * 128 + 64;
}
bool sst_chest_heart_pickup(unsigned index) {
    // Native STIF save table + Item scene flag: Faron, Eldin, summit and Skyview.
    return index == 993 || index == 1028 || index == 1446 || index == 1576 || index == 2343;
}
bool sst_chest_crystal_pickup(unsigned index) {
    // Save table 0: Skyloft/Academy/houses/sparring hall; table 21: the Sky/Lumpy Pumpkin.
    // Interior/outdoor representations of the same crystal deliberately share one receipt.
    return index == 934 || index == 935 || index == 936 || index == 938 ||
           (index >= 940 && index <= 947) || index == 3620 || index == 3636 || index == 3639;
}
unsigned sst_chest_index(bool scene, unsigned table, unsigned flag) {
    unsigned count = scene ? 128 : 32;
    return table < 26 && flag < count ? (scene ? SST_TBOX_CHESTS : 0) + table * count + flag : SST_CHESTS;
}
bool sst_chest_location(unsigned index, bool *scene, unsigned *table, unsigned *flag) {
    if (index >= SST_CONTAINER_CHESTS) return false;
    bool sc = index >= SST_TBOX_CHESTS;
    unsigned offset = sc ? index - SST_TBOX_CHESTS : index, count = sc ? 128 : 32;
    if (scene) *scene = sc;
    if (table) *table = offset / count;
    if (flag) *flag = offset % count;
    return true;
}
bool sst_chest_learn(SstChests *s, const SstChestReward *r) {
    if (!sst_chest_valid(r) || s->conflict[r->index]) return false;
    if (r->index >= SST_TBOX_CHESTS && r->index < SST_CONTAINER_CHESTS && s->pickup[r->index - SST_TBOX_CHESTS] &&
        s->pickup[r->index - SST_TBOX_CHESTS] != r->item) {
        s->conflict[r->index] = true; return false;
    }
    uint16_t *item = &s->item[r->index];
    if (*item && *item != r->item) { s->conflict[r->index] = true; return false; }
    if (*item) return false;
    *item = r->item; return true;
}
bool sst_chest_pickup(uint32_t params, uint16_t placed, uint16_t item, unsigned table, SstChestReward *out) {
    unsigned flag = (params >> 10) & 255;
    if (placed == 0xffff || !item || item >= 511 || (params & 511) != item ||
        ((params >> 20) & 15) != 0 || table >= 26 || flag >= 128) return false;
    if (out) *out = (SstChestReward){.index = sst_chest_index(true, table, flag), .item = item};
    return true;
}
void sst_chest_remember_pickup(SstChests *s, const SstChestReward *r) {
    bool scene;
    if (!r || !sst_chest_location(r->index, &scene, NULL, NULL) || !scene || !r->item || r->item >= 511) return;
    uint16_t *item = &s->pickup[r->index - SST_TBOX_CHESTS];
    if ((*item && *item != r->item) || (s->item[r->index] && s->item[r->index] != r->item))
        s->conflict[r->index] = true;
    if (!*item) *item = r->item;
}
void sst_chest_announce(const SstChests *s, SstChestFn send, void *user) {
    for (unsigned i = 0; i < SST_CHESTS; i++)
        if (s->item[i] && !s->conflict[i]) {
            SstChestReward r = {.index = i, .item = s->item[i]}; send(&r, user);
        }
}
