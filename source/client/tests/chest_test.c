#include "../src/sst_chest.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int sent;
static void send_reward(const SstChestReward *r, void *user) {
    (void)user; assert(sst_chest_valid(r)); sent++;
}
int main(void) {
    SstChests s = {0}; unsigned c; int n;
    assert(sst_chest_item(1, &c, &n) && c == 6 && n == 1);
    assert(sst_chest_item(3, &c, &n) && c == 0 && n == 5);
    assert(sst_chest_item(33, &c, &n) && c == 0 && n == 300);
    assert(sst_chest_item(169, &c, &n) && c == 30 && n == 1);
    assert(sst_chest_item(176, &c, &n) && c == 37 && n == 1);
    assert(sst_chest_item(94, &c, &n) && c == 8 && n == 1);
    assert(sst_chest_item(28, &c, &n) && c == 5 && n == 1);
    assert(sst_chest_item(48, &c, &n) && c == 38 && n == 1);
    // Exact collector-only resource registry, independent of the broader counter/progression map.
    const uint16_t collector_resources[]={2,3,4,32,33,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176};
    unsigned collector_count=0;
    for (unsigned id = 0; id < 512; id++) {
        bool supported = id == 1 || id == 2 || id == 3 || id == 4 || id == 28 || id == 32 || id == 33 || id == 35 || id == 48 || id == 94 || (id >= 161 && id <= 176);
        assert(sst_chest_item(id, NULL, NULL) == supported);
        bool collector_only=false;
        for(unsigned i=0;i<sizeof collector_resources/sizeof *collector_resources;i++)
            if(id==collector_resources[i])collector_only=true;
        assert(sst_chest_collector_only_resource((uint16_t)id)==collector_only);
        if(collector_only){assert(supported);collector_count++;}
    }
    assert(collector_count==21);
    assert(!sst_chest_collector_only_resource(512) && !sst_chest_collector_only_resource(UINT16_MAX));
    for (unsigned table = 0; table < 26; table++) for (unsigned bit = 0; bit < 128; bit++) {
        unsigned index = sst_chest_index(true, table, bit), t, b; bool scene;
        assert(sst_chest_location(index, &scene, &t, &b) && scene && t == table && b == bit);
        SstChestReward scene_reward = {.index = index, .item = 94};
        assert(sst_chest_valid(&scene_reward) == (!sst_chest_batreaux_item(index) || sst_chest_batreaux_item(index) == 94));
        scene_reward.item = 1; assert(sst_chest_valid(&scene_reward) == sst_chest_key_table(table));
    }
    assert(sst_chest_index(true, 26, 0) == SST_CHESTS);
    assert(sst_chest_index(true, 0, 128) == SST_CHESTS); // temporary/zone flags have no durable receipt
    assert(sst_chest_index(false, 0, 32) == SST_CHESTS);
    assert(!sst_chest_location(SST_CHESTS, NULL, NULL, NULL));
    // Every key-piece transaction must name one of the five native digging-spot receipts.
    for (unsigned index = 0; index <= SST_CHESTS; index++) {
        bool piece = index == 1352 || index == 1353 || index == 1354 || index == 1356 || index == 1408;
        assert(sst_chest_key_piece(index) == piece);
        SstChestReward r = {.index = index, .item = 28};
        assert(sst_chest_valid(&r) == piece);
        if (piece) { assert(sst_chest_learn(&s, &r)); assert(!sst_chest_learn(&s, &r)); }
    }
    s = (SstChests){0};
    unsigned crystals = 0;
    for (unsigned index = 0; index <= SST_CHESTS; index++) {
        bool crystal = index == 934 || index == 935 || index == 936 || index == 938 ||
            (index >= 940 && index <= 947) || index == 3620 || index == 3636 || index == 3639;
        assert(sst_chest_crystal_pickup(index) == crystal);
        SstChestReward r = {.index = index, .item = 48};
        assert(sst_chest_valid(&r) == crystal);
        if (crystal) {
            crystals++;
            assert(sst_chest_learn(&s, &r)); assert(!sst_chest_learn(&s, &r));
        }
        r.item = 35; assert(sst_chest_valid(&r) == (sst_chest_quest_flag(index) != 0)); // only reviewed NPC story receipts
    }
    assert(crystals == 15);
    s = (SstChests){0};
    // Reviewed vanilla source dungeons. Item 51 is not the native map item.
    const struct { unsigned item, tables, flag; } dungeon[] = {
        {50, (1u<<11)|(1u<<12)|(1u<<14)|(1u<<15)|(1u<<17)|(1u<<18)|(1u<<20), 2},
        {25, 1u<<12, 12}, {26, 1u<<15, 12}, {27, 1u<<18, 12},
        {29, 1u<<11, 12}, {30, 1u<<14, 12}, {31, 1u<<17, 12}
    };
    for (unsigned i = 0; i < sizeof dungeon / sizeof *dungeon; i++)
        for (unsigned t = 0; t < 26; t++) {
            unsigned flag = 99; bool valid = (dungeon[i].tables & (1u << t)) != 0;
            assert(sst_chest_dungeon(dungeon[i].item, t, &flag) == valid);
            if (valid) assert(flag == dungeon[i].flag);
            for (unsigned scene = 0; scene < 2; scene++) {
                SstChestReward d = {.index = sst_chest_index(scene, t, 0), .item = dungeon[i].item};
                assert(sst_chest_valid(&d) == valid);
                d.item = 51; assert(!sst_chest_valid(&d));
                d.item = 28; assert(!sst_chest_valid(&d));
            }
        }
    assert(!sst_chest_dungeon(50, 26, NULL));
    for (unsigned id = 0; id < 512; id++)
        assert(sst_chest_tool(id) == (id == 19 || id == 20 || id == 49 || id == 52 || id == 53 || id == 71 ||
            id == 75 || id == 76 || id == 77 || id == 90 || id == 91 || id == 92 || id == 105 || id == 137 || id == 140));
    for (unsigned id = 0; id < 512; id++)
        assert(sst_chest_tool_gift(id) == (id == 20 || id == 52 || id == 71 || id == 75 || id == 76 ||
            id == 77 || id == 90 || id == 91 || id == 105 || id == 140));
    for (unsigned id = 0; id < 512; id++)
        assert(sst_chest_gear(id) == (id == 56 || id == 68 || id == 99 || id == 138));
    for (unsigned index = 0; index <= SST_CHESTS; index++) {
        for (unsigned id = 0; id < 19; id++) {
            SstChestReward r = {.index = index, .item = (uint16_t[]){19,20,49,52,53,71,75,76,77,90,91,92,105,137,140,56,68,99,138}[id]};
            assert(sst_chest_valid(&r) == (index < SST_CONTAINER_CHESTS && !sst_chest_batreaux_item(index) ||
                (sst_chest_trial_flag(index) && r.item == sst_chest_trial_item(index))));
        }
    }
    for (unsigned i = 0; i <= SST_CHESTS; i++) {
        unsigned flag = sst_chest_trial_flag(i);
        bool supported = i >= 4366 && i <= 4369;
        assert((flag != 0) == supported);
        if (supported) {
            uint16_t expected = i == 4366 ? 68 : i == 4367 ? 138 : i == 4368 ? 20 : 180;
            assert(flag == i - 4160 && sst_chest_trial_item(i) == expected);
            for (unsigned item = 0; item < 512; item++) {
                SstChestReward reward = {.index=i,.item=item};
                assert(sst_chest_valid(&reward) == (item == expected));
            }
        } else assert(!sst_chest_trial_item(i));
    }
    assert(!sst_chest_trial_item(65535));
    assert(!sst_chest_gear(180)); // Stone is admitted only by its exact trial receipt
    for (unsigned i=0;i<SST_CONTAINER_CHESTS;i++) {
        SstChestReward stone={.index=i,.item=180};assert(!sst_chest_valid(&stone));
    }
    // Every upgrade supplies all preceding ownership levels; the base item has no dependencies.
    const uint16_t families[][4] = {{53,75,76,77},{19,90,91,0},{52,105,0,0},{71,140,0,0}};
    for (unsigned family = 0; family < 4; family++) for (unsigned tier = 0; tier < 4; tier++) {
        uint16_t item = families[family][tier];
        if (!item) break;
        for (unsigned slot = 0; slot < 5; slot++)
            assert(sst_chest_tool_prerequisite(item, slot) == (slot < tier ? families[family][slot] : 0));
    }
    assert(!sst_chest_tool_prerequisite(65535, 0));
    // Bottles need container receipts and real unlocked pouch space, never ownership flags.
    assert(sst_chest_quest_aux(384, 0) == 382 && !sst_chest_quest_aux(384, 1));
    for (unsigned item = 0; item < 512; item++) assert(sst_chest_bottle(item) == (item == 126 || item == 153));
    for (unsigned i = 0; i <= SST_CHESTS; i++) {
        SstChestReward r = {.index = i, .item = 153};
        assert(sst_chest_valid(&r) == (i < SST_CONTAINER_CHESTS && !sst_chest_batreaux_item(i) || i == 4195));
        assert(sst_chest_pouch_gift_item(i) == (i == 4195 ? 153 : i == 4974 ? 114 : 0));
        SstChestReward filled = {.index=i,.item=126};
        assert(sst_chest_valid(&filled) == (i < SST_CONTAINER_CHESTS && !sst_chest_batreaux_item(i)));
        if (i == 4195) {
            for (unsigned item = 0; item < 512; item++) {
                r.item = item; assert(sst_chest_valid(&r) == (item == 153));
            }
        }
    }
    // Shop purchases have exact vanilla receipt/item pairs, including the heart counter.
    for (unsigned index=0;index<=SST_CHESTS;index++) {
        unsigned expected=index==4973?94:index==4974?114:index==SST_BUG_MEDAL_CHEST?70:index>=SST_SHOP_CHESTS && index<SST_BUG_MEDAL_CHEST?(index<SST_SHOP_CHESTS+3?113:199):0;
        assert(sst_chest_shop_item(index)==expected);
        if (!expected) continue;
        assert(index>=SST_SHOP_CHESTS || sst_chest_quest_reserved(index-SST_CONTAINER_CHESTS));
        for (unsigned item=0;item<512;item++) {
            SstChestReward shop={.index=index,.item=item};
            assert(sst_chest_valid(&shop)==(item==expected));
        }
    }
    for (unsigned index=0;index<=SST_CHESTS;index++) {
        unsigned flag=0,ordinal=0;bool valid=index>=SST_SHOP_CHESTS && index<SST_BUG_MEDAL_CHEST;
        assert(sst_chest_shop_counter(index,&flag,&ordinal)==valid);
        if(valid) {assert(flag==(index<SST_SHOP_CHESTS+3?707:708));assert(ordinal==(index-SST_SHOP_CHESTS)%3+1);}
    }
    for (unsigned index=0;index<=SST_CHESTS;index++)
        assert(sst_chest_unique_pouch_item(index)==(index==SST_BUG_MEDAL_CHEST?70:0));
    // Capacity grants preserve personal upgrades and repair only completed receipt proof.
    const unsigned expansion_cases[][5] = {
        {0,0,0,0,0},{0,0,0,1,1},{1,1,1,0,1},{1,1,1,1,2},
        {3,0,1,0,3},{0,3,1,0,3},{0,3,1,1,4},{0,0,3,0,3},{0,0,3,1,4},
        {4,4,4,1,4},{4,0,0,1,4},{2,2,10,0,4},{7,4,1,0,5},{4,7,1,1,5},
        {0,0,UINT32_MAX,1,4}
    };
    for (unsigned j = 0; j < sizeof expansion_cases / sizeof *expansion_cases; j++) {
        const unsigned *x = expansion_cases[j];
        assert(sst_chest_pouch_expansion_target(x[0],x[1],x[2],x[3]) == x[4]);
    }
    for (unsigned live=0;live<=4;live++) for (unsigned saved=0;saved<=4;saved++)
        for (unsigned completed=0;completed<=8;completed++) for (unsigned incoming=0;incoming<=1;incoming++) {
            unsigned target=sst_chest_pouch_expansion_target(live,saved,completed,incoming);
            assert(target<=4 && target>=live && target>=saved);
            assert(sst_chest_pouch_expansion_target(target,target,completed,false)==target);
        }
    for (unsigned item=0;item<512;item++) assert(sst_chest_pouch_expansion(item)==(item==113));
    for (unsigned index=0;index<=SST_CHESTS;index++) {
        SstChestReward expansion={.index=index,.item=113};
        assert(sst_chest_valid(&expansion)==((index<SST_CONTAINER_CHESTS && !sst_chest_batreaux_item(index)) || (index>=SST_SHOP_CHESTS && index<SST_SHOP_CHESTS+3)));
    }
    for (unsigned i=0;i<=SST_CHESTS;i++) {
        assert(sst_chest_shield_item(i)==(i>=SST_SHIELD_CHESTS&&i<SST_AMMO_CHESTS?116+i-SST_SHIELD_CHESTS:0));
        for(unsigned item=116;item<=125;item++) {
            SstChestReward r={.index=i,.item=item};
            assert(sst_chest_valid(&r)==((i<SST_CONTAINER_CHESTS&&!sst_chest_batreaux_item(i))||sst_chest_shield_item(i)==item));
        }
    }
    const unsigned ammo_quantities[] = {10,20,30,5,10,15,5,10,15};
    for(unsigned item=0;item<512;item++) {
        unsigned ammo = item>=128&&item<=136?ammo_quantities[item-128]:0;
        assert(sst_chest_ammo_quantity(item)==ammo);
        for(unsigned amount=0;amount<=49;amount++) {
            unsigned shield=sst_chest_shield_durability(item);
            bool valid=(shield&&amount&&amount<=shield)||(ammo&&amount<=ammo);
            assert(sst_chest_first_pouch_word(item,(amount<<16)|item)==valid);
            assert(!sst_chest_first_pouch_word(item,(amount<<16)|(item+1)));
        }
    }
    for(unsigned index=0;index<=SST_CHESTS;index++) {
        unsigned expected=index>=SST_AMMO_CHESTS&&index<SST_CHESTS?128+index-SST_AMMO_CHESTS:0;
        assert(sst_chest_ammo_item(index)==expected);
        assert(sst_chest_first_pouch_item(index)==(expected?expected:sst_chest_shield_item(index)));
        for(unsigned item=128;item<=136;item++) {
            SstChestReward r={.index=index,.item=item};
            assert(sst_chest_valid(&r)==((index<SST_CONTAINER_CHESTS&&!sst_chest_batreaux_item(index))||expected==item));
        }
    }
    const uint16_t sword_ids[]={10,11,12,9,13,14};
    for(unsigned item=0;item<512;item++) {
        unsigned tier=0;for(unsigned i=0;i<6;i++)if(sword_ids[i]==item)tier=i+1;
        assert(sst_chest_sword_tier(item)==tier);
        for(unsigned index=0;tier&&index<=SST_CHESTS;index++) {
            SstChestReward r={.index=index,.item=item};assert(sst_chest_valid(&r)==(index<SST_CONTAINER_CHESTS&&!sst_chest_batreaux_item(index)));
        }
    }
    for(unsigned tier=0;tier<8;tier++)assert(sst_chest_sword_item(tier)==(tier&&tier<=6?sword_ids[tier-1]:0));
    for(unsigned live=0;live<64;live++)for(unsigned saved=0;saved<64;saved++)for(unsigned tier=1;tier<=6;tier++){
        unsigned want=tier;for(unsigned i=0;i<6;i++)if((live|saved)&(1u<<i))if(want<i+1)want=i+1;
        assert(sst_chest_sword_target(live,saved,sword_ids[tier-1])==want);
    }
    assert(!sst_chest_sword_best(64)&&!sst_chest_sword_target(64,0,10)&&!sst_chest_sword_target(0,64,10)&&!sst_chest_sword_target(0,0,15));
    const uint16_t pouch_items[] = {70,100,101,102,103,104,114,116,117,118,119,120,121,122,123,124,125,126,128,129,130,131,132,133,134,135,136,153};
    const unsigned quantities[] = {1,1,1,1,1,1,1,16,24,32,24,36,48,12,16,20,48,1,10,20,30,5,10,15,5,10,15,1};
    for (unsigned item = 0; item < 512; item++) {
        unsigned expected = 0;
        for (unsigned j = 0; j < sizeof pouch_items / sizeof *pouch_items; j++)
            if (item == pouch_items[j]) expected = quantities[j];
        assert(sst_chest_pouch_quantity(item) == expected);
    }
    for (unsigned j = 0; j < sizeof pouch_items / sizeof *pouch_items; j++) {
        uint16_t item = pouch_items[j];
        for (unsigned index = 0; index <= SST_CHESTS; index++) {
            SstChestReward reward = {.index=index,.item=item};
            assert(sst_chest_valid(&reward) == ((index < SST_CONTAINER_CHESTS && !sst_chest_batreaux_item(index)) || (index == 4195 && item == 153) || (index == 4974 && item == 114) || (index == SST_BUG_MEDAL_CHEST && item == 70) || (sst_chest_first_pouch_item(index) == item)));
        }
        for (unsigned slot = 0; slot < SST_POUCH_SLOTS; slot++) {
            uint32_t before[8] = {0x00030037,0x00010099,0x00320074,0x00010046,0x00050080,0x00010064,0x00010099,0x00010099}, after[8];
            before[slot] = 0; memcpy(after,before,sizeof before);
            assert(sst_chest_pouch_result(slot,item,0,before,after) == SST_POUCH_WAIT);
            assert(sst_chest_pouch_result(slot,item,1,before,after) == SST_POUCH_CONFLICT);
            after[slot] = quantities[j] << 16 | item;
            assert(sst_chest_pouch_result(slot,item,1,before,after) == SST_POUCH_APPLIED);
            assert(sst_chest_pouch_result(slot,item,0,before,after) == SST_POUCH_CONFLICT);
            assert(sst_chest_pouch_result(slot,item,2,before,after) == SST_POUCH_CONFLICT);
            after[(slot+1)%8] ^= 1u << 16;
            assert(sst_chest_pouch_result(slot,item,1,before,after) == SST_POUCH_CONFLICT);
        }
    }
    uint32_t empty_pouch[8] = {0};
    assert(sst_chest_pouch_result(8,128,0,empty_pouch,empty_pouch) == SST_POUCH_CONFLICT);
    assert(sst_chest_pouch_result(0,154,0,empty_pouch,empty_pouch) == SST_POUCH_CONFLICT);
    assert(sst_chest_pouch_result(0,128,0,NULL,empty_pouch) == SST_POUCH_CONFLICT);
    for (unsigned mask = 0; mask < 256; mask++) for (unsigned extra = 0; extra <= 4; extra++) {
        uint16_t items[SST_POUCH_SLOTS], before[SST_POUCH_SLOTS];
        for (unsigned i = 0; i < SST_POUCH_SLOTS; i++) items[i] = mask & (1u << i) ? 55 : 0;
        memcpy(before, items, sizeof items);
        unsigned expected = SST_POUCH_SLOTS;
        for (unsigned i = 4 + extra; i > 0; i--) if (!(mask & (1u << (i - 1)))) expected = i - 1;
        assert(sst_chest_pouch_slot(true, extra, items) == expected);
        assert(sst_chest_pouch_slot(false, extra, items) == SST_POUCH_SLOTS);
        assert(sst_chest_pouch_slot(true, 5, items) == SST_POUCH_SLOTS);
        assert(!memcmp(items, before, sizeof items));
    }
    assert(sst_chest_pouch_slot(true, 0, NULL) == SST_POUCH_SLOTS);
    const uint16_t batreaux[] = {108,94,109,33,110,33};
    for (unsigned i = 0; i <= SST_CHESTS; i++) {
        unsigned expected = i >= 902 && i <= 907 ? batreaux[i-902] : 0;
        assert(sst_chest_batreaux_item(i) == expected);
        assert(sst_chest_wallet_gift_item(i) == (i == 4520 ? 111 : 0));
        assert(sst_chest_reward_quantity(i, 33) == (i == 907 ? 2 : 1));
        assert(sst_chest_reward_quantity(i, 3) == 1);
        if (expected || i == 4520) for (unsigned item = 0; item < 512; item++) {
            SstChestReward reward = {.index=i,.item=item};
            assert(sst_chest_valid(&reward) == (item == (expected ? expected : 111)));
        }
    }
    const unsigned limits[] = {300,500,1000,5000,9000};
    for (unsigned mask = 0; mask < 16; mask++) for (unsigned extras = 0; extras < 4; extras++) {
        unsigned tier=0;for (unsigned i=0;i<4;i++) if(mask&(1u<<i))tier=i+1;
        assert(sst_chest_wallet_limit(mask,extras)==limits[tier]+300*extras);
        for (unsigned item=107;item<=112;item++)
            assert(sst_chest_wallet_satisfied(item,mask)==(item>=108&&item<=111&&(mask>>(item-108))!=0));
    }
    assert(!sst_chest_wallet_limit(16,0) && !sst_chest_wallet_limit(1,4));
    assert(!sst_chest_wallet_satisfied(111,16));
    for (unsigned item=0;item<512;item++) assert(sst_chest_wallet(item)==(item>=108&&item<=111));
    unsigned quests = 0;
    for (unsigned i = 0; i <= SST_CHESTS; i++) {
        unsigned flag = sst_chest_quest_flag(i);
        bool supported = i == 4486 || i == 4533 || i == 4543 || i == 4544 || i == 4547 || i == 4554 || i == 4558 || i == 4632 || i == 4637 || i == 4642 || i == 4713 || i == 4721 || i == 4735 || i == 4796 || i == 4799;
        assert((flag != 0) == supported);
        SstChestReward q = {.index=i, .item=35}; assert(sst_chest_valid(&q) == supported);
        if (supported) {
            assert(flag == i - 4160); quests++;
            assert(!sst_chest_location(i, NULL, NULL, NULL) && !sst_chest_saved_reward(i));
            assert(sst_chest_learn(&s, &q)); assert(!sst_chest_learn(&s, &q));
        }
    }
    assert(quests == 15 && sst_chest_quest_aux(575, 0) == 361 && sst_chest_quest_aux(387, 0) == 386);
    sent = 0; sst_chest_announce(&s, send_reward, NULL); assert(sent == 15);
    assert(sst_chest_quest_aux(482, 0) == 481 && !sst_chest_quest_aux(477, 0));
    assert(sst_chest_quest_aux(636, 0) == 635 && !sst_chest_quest_aux(639, 0));
    for (unsigned flag = 0; flag < 1202; flag++) {
        unsigned other = sst_chest_quest_alternative(flag);
        assert(other == (flag == 636 ? 639 : flag == 639 ? 636 : flag == 553 ? 561 : flag == 561 ? 553 : 0));
        if (other) assert(sst_chest_quest_alternative(other) == flag);
    }
    // A sleep cleanup (558) is not a reward receipt. Both hand-over choices wait;
    // one choice translates either teammate ending into the local receipt.
    assert(!sst_chest_quest_flag(4160 + 558));
    const unsigned followups[] = {558,559,557,560,663,554};
    for (unsigned slot = 0; slot < SST_QUEST_AUX + 2; slot++) {
        assert(sst_chest_quest_aux(561, slot) == (slot < 6 ? followups[slot] : 0));
        assert(!sst_chest_quest_aux(553, slot));
    }
    for (unsigned flag = 0; flag < 1202; flag++) {
        bool letter = flag == 553 || flag == 561;
        assert(sst_chest_quest_letter(flag) == letter);
        assert(sst_chest_quest_target(flag, false, false) == (letter ? 0 : flag));
        assert(sst_chest_quest_target(flag, true, true) == (letter ? 0 : flag));
        assert(sst_chest_quest_target(flag, true, false) == (letter ? 553 : flag));
        assert(sst_chest_quest_target(flag, false, true) == (letter ? 561 : flag));
    }
    assert(!sst_chest_quest_alternative(65535));
    assert(!sst_chest_quest_alternative(383) && !sst_chest_quest_alternative(384)); // Orielle and Parrow each give a pack
    s = (SstChests){0}; sent = 0;
    unsigned catalogued = 0;
    for (unsigned i = 0; i <= SST_CHESTS; i++) {
        uint16_t item = sst_chest_saved_reward(i);
        if (item) { SstChestReward r = {.index = i, .item = item}; assert(sst_chest_valid(&r)); catalogued++; }
    }
    assert(catalogued == 125);
    assert(sst_chest_saved_reward(2) == 10);
    assert(sst_chest_saved_reward(672) == 113);
    assert(sst_chest_saved_reward(228) == 75 && sst_chest_saved_reward(1550) == 105);
    assert(sst_chest_saved_reward(165) == 92 && sst_chest_saved_reward(449) == 92);
    assert(sst_chest_saved_reward(484) == 99);
    assert(sst_chest_saved_reward(320) == 126 && sst_chest_saved_reward(486) == 153);
    assert(sst_chest_saved_reward(353) == 53 && sst_chest_saved_reward(584) == 19);
    assert(sst_chest_saved_reward(162) == 20);
    assert(sst_chest_saved_reward(164) == 52 && sst_chest_saved_reward(352) == 50);
    assert(sst_chest_saved_reward(355) == 1 && sst_chest_saved_reward(864) == 94);
    assert(sst_chest_saved_reward(2070) == 171 && sst_chest_saved_reward(3157) == 94);
    assert(!sst_chest_saved_reward(29) && !sst_chest_saved_reward(483)); // conflicting vanilla variants
    // Freestanding heart metadata outlives its actor; merely seeing an item cannot grant it.
    for (unsigned i = 0; i <= SST_CHESTS; i++)
        assert(sst_chest_heart_pickup(i) == (i == 993 || i == 1028 || i == 1446 || i == 1576 || i == 2343));
    SstChestReward pickup;
    assert(sst_chest_pickup(0xff0c865e, 255, 94, 1, &pickup));
    assert(pickup.index == sst_chest_index(true, 1, 33) && pickup.item == 94);
    sst_chest_remember_pickup(&s, &pickup);
    assert(!s.item[pickup.index] && s.pickup[1 * 128 + 33] == 94);
    assert(sst_chest_learn(&s, &pickup));
    assert(!sst_chest_learn(&s, &pickup));
    SstChestReward wrong = pickup; wrong.item = 3;
    assert(!sst_chest_learn(&s, &wrong) && s.conflict[pickup.index]);
    s = (SstChests){0};
    assert(sst_chest_learn(&s, &wrong)); // a remote claim that predates native discovery
    sst_chest_remember_pickup(&s, &pickup);
    assert(s.conflict[pickup.index]);
    for (unsigned kind = 0; kind < 16; kind++) {
        uint32_t params = (0xff0c865e & ~0xf00000u) | kind << 20;
        assert(sst_chest_pickup(params, 255, 94, 1, NULL) == (kind == 0));
    }
    assert(!sst_chest_pickup(0xff0c865e, 0xffff, 94, 1, NULL)); // spawned presentations/drops
    assert(!sst_chest_pickup(0xff0c865e, 255, 93, 1, NULL)); // randomized/substituted ID
    assert(!sst_chest_pickup(0xff0c865e, 255, 94, 26, NULL));
    for (unsigned flag = 0; flag < 256; flag++) {
        uint32_t params = (0xff0c865e & ~(255u << 10)) | flag << 10;
        assert(sst_chest_pickup(params, 255, 94, 1, NULL) == (flag < 128));
    }
    sst_chest_remember_pickup(&s, NULL);
    s = (SstChests){0};
    SstChestReward r = {.index = 0, .item = 3};
    assert(sst_chest_learn(&s, &r));
    assert(!sst_chest_learn(&s, &r)); // concurrent opening / duplicate announcement
    r.index = SST_CONTAINER_CHESTS - 1; r.item = 176;
    assert(sst_chest_learn(&s, &r));
    sst_chest_announce(&s, send_reward, NULL); assert(sent == 2);
    r.item = 175;
    assert(!sst_chest_learn(&s, &r)); // mismatched chest data fails closed
    assert(s.conflict[r.index]);
    sent = 0; sst_chest_announce(&s, send_reward, NULL); assert(sent == 1);
    r.index = SST_CHESTS; assert(!sst_chest_learn(&s, &r));
    r.index = 12; r.item = 154; assert(!sst_chest_learn(&s, &r)); // unsupported pouch items stay personal
    r.item = 93; assert(!sst_chest_learn(&s, &r)); // boss heart containers remain personal
    r.item = 94; assert(sst_chest_learn(&s, &r)); assert(!sst_chest_learn(&s, &r));
    r.item = 1; assert(!sst_chest_learn(&s, &r)); // never admit a key for Skyloft
    for (unsigned table = 0; table < 26; table++) {
        bool valid = table == 7 || table == 11 || table == 12 || table == 15 ||
                     table == 17 || table == 18 || table == 20;
        for (unsigned bit = 0; bit < 32; bit++) {
            r.index = table * 32 + bit;
            assert(sst_chest_valid(&r) == valid);
        }
    }
    r.index = 11 * 32 + 2;
    assert(sst_chest_learn(&s, &r)); assert(!sst_chest_learn(&s, &r));
    r.index = 12 * 32 + 2; assert(sst_chest_learn(&s, &r)); // another dungeon is a distinct receipt
    puts("chests: TBox/scene receipt identities, permanent bounds, counter/heart rewards, dungeon keys and duplicates PASS");
}
