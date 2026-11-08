// Permanent chest receipts are the game's own opened bits, never a separate disk ledger.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SST_TBOX_CHESTS (26 * 32)
#define SST_SCENE_CHESTS (26 * 128)
#define SST_CONTAINER_CHESTS (SST_TBOX_CHESTS + SST_SCENE_CHESTS)
#define SST_STORY_CHESTS 1202
#define SST_SHOP_CHESTS (SST_CONTAINER_CHESTS + SST_STORY_CHESTS)
#define SST_BUG_MEDAL_CHEST (SST_SHOP_CHESTS + 6)
#define SST_SHIELD_CHESTS (SST_BUG_MEDAL_CHEST + 1)
#define SST_AMMO_CHESTS (SST_SHIELD_CHESTS + 10)
#define SST_CHESTS (SST_AMMO_CHESTS + 9)
typedef struct { uint16_t index, item, from; } SstChestReward;
typedef void (*SstChestFn)(const SstChestReward *, void *);
// Supported counter rewards; dungeon ownership rewards use the separate checked mapping.
bool sst_chest_item(uint16_t item, unsigned *counter, int *amount);
// Share the consumed container receipt, never its rupee/material counter gain.
// Keys, heart pieces, crystals, tools and other progression keep their existing paths.
bool sst_chest_collector_only_resource(uint16_t item);
bool sst_chest_key_table(unsigned table);
// Five vanilla Eldin digging-spot scene receipts; these share the global key-piece counter.
bool sst_chest_key_piece(unsigned index);
// Complete vanilla catalogue of five freestanding heart pieces, for recovery after restart.
bool sst_chest_heart_pickup(unsigned index);
// Fifteen vanilla single Gratitude Crystals; NPC packs have different acquisition receipts.
bool sst_chest_crystal_pickup(unsigned index);
bool sst_chest_dungeon(uint16_t item, unsigned table, unsigned *flag);
// Reviewed tools; Bow/Slingshot also fill their native base ammunition on first ownership.
bool sst_chest_tool(uint16_t item);
// Sword item IDs are not tier order: Practice10, Goddess11, Long12, White9, Master13, True14.
unsigned sst_chest_sword_tier(uint16_t item);
uint16_t sst_chest_sword_item(unsigned tier);
unsigned sst_chest_sword_best(unsigned owned_mask);
unsigned sst_chest_sword_target(unsigned live_mask, unsigned saved_mask, uint16_t incoming);
// Original ownership-only shop/quest acquisitions; base dungeon tools still need receipts.
bool sst_chest_tool_gift(uint16_t item);
// Upgrade dependencies, in native acquisition order; zero ends the list (at most three).
uint16_t sst_chest_tool_prerequisite(uint16_t item, unsigned slot);
// Passive traversal gear has no item-wheel unlock or ammunition side effect.
bool sst_chest_gear(uint16_t item);
// Permanent wallet tiers, with higher ownership satisfying lower rewards.
bool sst_chest_wallet(uint16_t item);
bool sst_chest_wallet_satisfied(uint16_t item, unsigned owned_mask);
unsigned sst_chest_wallet_limit(unsigned owned_mask, unsigned extra_wallets);
// Batreaux's six permanent scene receipts; wrong item pairs are rejected.
uint16_t sst_chest_batreaux_item(unsigned index);
unsigned sst_chest_reward_quantity(unsigned index, uint16_t item);
// Final Batreaux wallet uses human-transformation story receipt 360.
uint16_t sst_chest_wallet_gift_item(unsigned index);
// Reviewed empty bottle and Sealed Temple Revitalizing Potion bottle; native pouch insertions.
bool sst_chest_bottle(uint16_t item);
#define SST_POUCH_SLOTS 8
#define SST_POUCH_EXPANSIONS 4
bool sst_chest_pouch_expansion(uint16_t item);
// Preserve personal upgrades, restore completed receipt proof, and add a new reward once.
// Values above SST_POUCH_EXPANSIONS are invalid; return SST_POUCH_EXPANSIONS + 1.
unsigned sst_chest_pouch_expansion_target(unsigned live, unsigned saved, unsigned completed, bool incoming);
// Reviewed medals, spare ammo pouches and bottles. Zero means unsupported.
unsigned sst_chest_pouch_quantity(uint16_t item);
// Native shield durability, and first-acquisition identities for the ten shield types.
unsigned sst_chest_shield_durability(uint16_t item);
uint16_t sst_chest_shield_item(unsigned index);
// First acquired spare-ammo variants retain ownership after the bag is emptied or stored.
unsigned sst_chest_ammo_quantity(uint16_t item);
uint16_t sst_chest_ammo_item(unsigned index);
uint16_t sst_chest_first_pouch_item(unsigned index);
bool sst_chest_first_pouch_word(uint16_t item, uint32_t word);
typedef enum { SST_POUCH_WAIT, SST_POUCH_APPLIED, SST_POUCH_CONFLICT } SstPouchResult;
// A native refusal is retryable only when the entire pouch is unchanged.
SstPouchResult sst_chest_pouch_result(unsigned slot, uint16_t item, unsigned result,
    const uint32_t before[SST_POUCH_SLOTS], const uint32_t after[SST_POUCH_SLOTS]);
// Returns the first free unlocked slot, or SST_POUCH_SLOTS when delivery must wait.
unsigned sst_chest_pouch_slot(bool owned, unsigned expansions, const uint16_t items[SST_POUCH_SLOTS]);
// Reviewed, unambiguous vanilla containers recover from saved receipts without an area visit.
uint16_t sst_chest_saved_reward(unsigned index);
// Reviewed NPC pouch gifts use permanent story receipts and native pouch insertion.
uint16_t sst_chest_pouch_gift_item(unsigned index);
// Beedle's reviewed single-purchase stock; no currency is copied or charged remotely.
uint16_t sst_chest_shop_item(unsigned index);
// Bug Medal is unique; its native acquired-item flag is also its shop receipt.
uint16_t sst_chest_unique_pouch_item(unsigned index);
// Three ordered purchases each for shop pouch upgrades and extra wallets.
bool sst_chest_shop_counter(unsigned index, unsigned *story_flag, unsigned *ordinal);
// Reviewed NPC crystal packs use permanent story receipts in a separate identity range.
// Silent Realm completion receipts pair their story flag with the native trial reward.
unsigned sst_chest_trial_flag(unsigned index);
uint16_t sst_chest_trial_item(unsigned index);
bool sst_chest_trial_reserved(unsigned flag);
unsigned sst_chest_quest_flag(unsigned index);
bool sst_chest_quest_reserved(unsigned story_flag);
#define SST_QUEST_AUX 6
unsigned sst_chest_quest_aux(unsigned story_flag, unsigned slot);
// These are two endings of one personal reward, not two crystal packs.
unsigned sst_chest_quest_alternative(unsigned story_flag);
// Letter rewards wait for one personal hand-over choice, then use that ending's receipt.
bool sst_chest_quest_letter(unsigned story_flag);
unsigned sst_chest_quest_target(unsigned story_flag, bool toilet, bool karane);
// Keep existing TBox identities; permanent scene receipts occupy the following range.
unsigned sst_chest_index(bool scene, unsigned table, unsigned flag);
bool sst_chest_location(unsigned index, bool *scene, unsigned *table, unsigned *flag);
bool sst_chest_valid(const SstChestReward *reward);
typedef struct {
    uint16_t item[SST_CHESTS];
    bool conflict[SST_CHESTS];
    uint16_t pickup[SST_SCENE_CHESTS]; // native metadata retained after placed actors disappear
} SstChests;
// Only placed, fixed-ID, normal pickups with permanent scene receipts qualify as sources.
bool sst_chest_pickup(uint32_t params, uint16_t placed, uint16_t item, unsigned table, SstChestReward *out);
void sst_chest_remember_pickup(SstChests *state, const SstChestReward *reward);
bool sst_chest_learn(SstChests *state, const SstChestReward *reward);
void sst_chest_announce(const SstChests *state, SstChestFn send, void *user);
