# Shop rewards in protocol 35

The Life Medal and heart piece share their original purchase receipts and inventory
transactions. Recipients keep their own rupees. Incoming purchases wait while a
player is inside Beedle's shop (F002r); leave to receive them, then return so native
shop actors rebuild their offers from the completed purchase records. The Multiplayer
tab explains this pending state. A full Adventure Pouch holds the Life Medal until a
usable slot is free. The heart piece can arrive with a full pouch.

| Native purchase | Story receipt | CHEST identity | Item |
| --- | --- | --- | --- |
| Heart piece | 813 | 4973 | 94 |
| Life Medal | 814 | 4974 | 114 |

Only these exact pairs are accepted. Raw story813/814 completion flags cannot bypass
the personal reward transaction. A completed receipt in either the saved or live flag
table proves delivery: the missing copy is repaired without replacing a stored medal,
adding another heart piece, healing again or charging currency. Completed purchases
are discovered from saves, including when the item is absent from the active pouch.

The heart transaction validates the existing native remainder and total counters,
capacity change and healing result before committing its copy-on-write story receipt.
The medal uses checked native pouch insertion, then records completion with the native
story setter. Unexpected native inventory results quarantine the receipt rather than
risk a repeated grant. Existing event, map, trial and boss guards still apply.

The reviewed SSHD 1.0.0 shop table is at main+0x163c43c with 84-byte entries. Entry26
is item114 and chains to entry23/item94. Native purchase methods at main+0xaffe40 and
main+0xb00074 set flags814 and813 respectively. Their sold-out getters at main+0xaffe08
and main+0xb00044 read those flags. Definitions are word44/bit6 and word44/bit5.
The active model node at actor+0xcc8 stores its entry index at offset0; the
factory item objects in actor+0xcf8 store their entry at offset8.
These facts were inspected in the user's prepared executable; no executable bytes,
private game files or keys are distributed with the mod.

The public [ssrando shop-table patcher](https://github.com/ssrando/ssrando/blob/main/gamepatches.py)
helped identify the table structure, which was checked against the prepared HD game.
Its additional sold-out flags937–941 are randomizer patches and are not vanilla receipts.
Shop pouch upgrades use multi-bit counter707; extra wallets use counter708. Their counters now use the ordered transactions described below. Bug Medal uses the unique transaction described below. Crafting remains personal.

Controlled native results, original stock actor snapshots, source/artifact hashes and
cleanup records are in `build/verification/shop31-batch/`. These are isolated fixtures
and lightweight peers, not a full natural shopping route or separate-PC internet test.


Protocol32 also shares the three pouch upgrades (ordered receipts5362–5364/item113)
and three extra wallets (5365–5367/item199). Their original purchase counters707/708
prove completed prefixes. New purchases require earlier ordinals, and missing
predecessors get request-team-state guidance. Saved/live purchase and capacity repair
preserves personal rupees, wallet tiers and all pouch words. A full pouch does not
block capacity upgrades or extra wallets; extra wallets need no pouch unlock. Pouch
capacity remains at eight total slots and extra wallets at three. Temporary inventory
modes hold delivery and repair. Completed shop counts combine with the original
Goddess Chest receipt to recover four earned expansions. Saved-only stock repairs
wait until leaving Beedle's shop, then re-entry rebuilds offers from the native counts.

The extra-wallet object at main+0x164ba28 targets logical508 and uses the native
counter adder at main+0xb892f4, with cap function main+0xb8b860 returning3. Native
item199 delivery at main+0x4def4c selects that same object. Counter fields are validated
before use. Pouch purchases use the existing checked490 capacity path. No extra-wallet
item is inserted into the pouch. Remaining unreviewed purchases and crafting stay personal.

While a stock repair is held, the bridge retains the completed purchase proof it
observed. The game's frame writeback can replace the saved flag shadow with its
incomplete live copy. Keeping that pending proof allows the eventual repair after
leaving the shop without a second heart, medal or capacity grant. This pending
state clears with the current game's bridge reset; it is not a separate save ledger.


Protocol33 adds the unique Bug Medal: CHEST5368/item70. Its receipt is the original
native item70 acquired flag (word12, bit0), retained when the medal is stored. It is
not a story815 flag. Native ItemFlagManager strips mask0x4000 before flag lookup.
New delivery inserts one quantity1 medal into a free unlocked pouch slot, then sets
ownership with the checked native setter. Saved/live copies repair without another
insertion. A carried legacy medal or a completed typed medal container proves ownership.
Further Bug Medal containers complete their personal receipts without duplicating the
unique medal. Life Medal's existing separate copies retain their own behavior.

Bug Net shop entry25 chains to Bug Medal27, then sold-out127. Incoming Net71 and
Big Bug Net140 wait while F002r stock is active and the base Net is missing, for
both raw tool flags and container rewards. The unique medal and missing ownership
repairs also wait until shop exit. Return afterward to rebuild native stock. Currency,
health, pouch contents and capacity remain personal except for the actual new reward.
Stored medal ownership and saved-only completion proof survive resync and safe repair.

The base stock getter at main+0xafdd58 queries item|0x4000. The checked item-manager
vtable is main+0x1649a30; setFlag is main+0xb807ac, committed getter main+0xb808a8,
and setField main+0xb808a0. Public
[ItemFlagManager definitions](https://github.com/zeldaret/ss/blob/main/include/d/flag/itemflag_manager.h)
and [implementation](https://github.com/zeldaret/ss/blob/main/src/d/flag/itemflag_manager.inc)
helped interpret the mask; the HD addresses, fields and stock actors were checked
against the prepared game. Only scalar facts are recorded in verification evidence.


First-acquired shields (protocol 34) use original acquired-item flags as personal
receipts. Every teammate receives one copy of each newly shared type. This includes
crafted shield tiers; repeat purchases, base shields and material costs remain personal.
A shield already acquired and later stored, broken or damaged is not replaced or healed
by resync. A distinct shield container keeps its own opened receipt and can grant once.

| Shield | Item / acquired flag | First CHEST receipt | Native starting durability |
| --- | --- | --- | --- |
| Wooden | 116 | 5369 | 16 |
| Banded | 117 | 5370 | 24 |
| Braced | 118 | 5371 | 32 |
| Iron | 119 | 5372 | 24 |
| Reinforced | 120 | 5373 | 36 |
| Fortified | 121 | 5374 | 48 |
| Sacred | 122 | 5375 | 12 |
| Divine | 123 | 5376 | 16 |
| Goddess | 124 | 5377 | 20 |
| Hylian | 125 | 5378 | 48 |

Native pouch insertion at main+0xde4a90 reads these ten bytes from main+0x135a16f
when given its default quantity. Both descriptor classes and the table are checked
before mutation. Shields 116–124 have pouch class 0; Hylian has ownership class 3
while still using the shield pouch insertion path. Acquired fields occupy word5
bits10–15 and word6 bits0–3. Native acquired-flag repair preserves the entire pouch.

New shield rewards and missing-flag repairs wait inside the Bazaar (F004r) to avoid
changing active crafting/stock data. Saved-only proof is retained until exit. A valid
carried legacy shield can prove acquisition without refilling durability; quantities
above native maximum are excluded. Completed shield containers repair bookkeeping
without replacing a stored item. The native acquired flags remain the durable ledger.

The vanilla Owlan flow gives item116 at node163 and later clears temporary scene20.
That scene flag is not repurposed as a positive gift receipt; first shield acquisition
is discovered from native item116 ownership instead. This preserves dialogue state.
The public [pouch declarations](https://github.com/zeldaret/ss/blob/main/include/d/d_pouch.h)
helped identify native inventory operations; all HD offsets, descriptors and durability
values were verified against the prepared game. Natural crafting and shield combat routes
remain to be tested; automated break checks simulate removal while retaining acquisition.


Protocol 35 shares the first acquisition of each spare-ammo pouch tier, including
crafted variants. Each recipient gets one new bag at its native starting amount.
An already acquired empty, stored or lost bag is not refilled or replaced on resync.
Repeat purchases and recipes remain personal; material costs and main weapon ammo
are never copied or charged remotely. Distinct container rewards keep their own
opened receipts and can each provide another bag once.

| Bag | Item / acquired flag | First CHEST receipt | Native starting amount |
| --- | --- | --- | --- |
| Small Seed Satchel | 128 | 5379 | 10 |
| Medium Seed Satchel | 129 | 5380 | 20 |
| Large Seed Satchel | 130 | 5381 | 30 |
| Small Quiver | 131 | 5382 | 5 |
| Medium Quiver | 132 | 5383 | 10 |
| Large Quiver | 133 | 5384 | 15 |
| Small Bomb Bag | 134 | 5385 | 5 |
| Medium Bomb Bag | 135 | 5386 | 10 |
| Large Bomb Bag | 136 | 5387 | 15 |

The HD descriptors for these nine items are pouch class0. Native default amounts
are read from main+0x139fed8, and acquired flags occupy item word6 bits6–14. The
shared first-pouch adapter validates these definitions and the native insertion
result. Legacy quantity zero is valid for ammo bags. Quantities above native maximum
are excluded from carried proof. A completed container or valid carried bag can
repair acquisition without adding ammunition or replacing inventory. Adjacent shield
and Bomb Bag ownership flags are preserved.

Ammo grants and missing acquired-flag repair wait inside Bazaar F004r. Saved-only
proof survives native writeback until exit. The retained mask is 32 bits, covering
ten shield types and nine ammo variants. Main ammunition uses separate logical
fields: bombs499, seeds493 and arrows498. Native firing of a newly shared Small
Quiver is tested with base arrows empty, and replay preserves the actual spent amount.
The native menu is also checked with an empty bag in slot eight. Full natural recipes
and every reserve-ammo combat route remain to be tested.
