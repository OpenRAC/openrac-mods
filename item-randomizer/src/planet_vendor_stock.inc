/* SPDX-License-Identifier: GPL-3.0-or-later
 * Planet vendor stock: a planet's new vendor weapon is put in the stock once its coordinates are
 * unlocked, so the vendor on the planet the hero is on already sells it.
 *
 * The game adds a level's vendor weapon to the stock (D_0015EED0) only at that level's start
 * (VendorStockLevelInit). The stock is shared by every vendor, so a weapon added here appears
 * wherever the hero is, the next time a vendor's menu is opened.
 *
 * Included by item_randomizer.c and archipelago.c, which each give: s_disc, byte_at, owned,
 * planet_flag, vendor_slot, rando_locations, rando_location_count, STOCK_END, STOCK_AMMO and
 * STOCK_ITEM.
 */

/* Drops repeats from the stock: an exact repeat, and the for-sale entry of an item whose ammo is
 * listed (the ammo means it was bought). What is left is closed up in order. */
static void planet_vendor_stock_dedupe(uint8_t* stock) {
    uint8_t out[VENDOR_STOCK_SIZE];
    int n = 0;

    for (int i = 0; i < VENDOR_STOCK_SIZE && stock[i] != STOCK_END; ++i) {
        const uint8_t entry = stock[i];
        int keep = 1;

        for (int j = 0; j < VENDOR_STOCK_SIZE && stock[j] != STOCK_END && keep; ++j) {
            if (j == i || (stock[j] & STOCK_ITEM) != (entry & STOCK_ITEM)) {
                continue;
            }
            // An exact repeat: only the first is kept.
            if (stock[j] == entry && j < i) {
                keep = 0;
            }
            // The item for sale while its ammo is listed too: the ammo is kept.
            if (!(entry & STOCK_AMMO) && (stock[j] & STOCK_AMMO)) {
                keep = 0;
            }
        }

        if (keep) {
            out[n++] = entry;
        }
    }

    memset(out + n, STOCK_END, (size_t)(VENDOR_STOCK_SIZE - n));
    memcpy(stock, out, sizeof out);
}

/* Puts each unlocked planet's vendor weapon in the stock when the stock does not list it yet
 * (for sale or as ammo). A weapon that is a shuffled vendor slot goes in for sale whatever is
 * owned (the menu shows a bought slot as ammo); an unshuffled one only while not owned. */
static void planet_vendor_stock_sync(void) {
    uint8_t* stock = byte_at(s_disc->vendor_stock);

    for (size_t i = 0; i < rando_location_count; ++i) {
        const rando_location* l = &rando_locations[i];

        if (l->kind != LOC_VENDOR || l->key >= RANDO_ITEM_COUNT || !*planet_flag(l->level)) {
            continue;
        }

        const int item = l->key;
        if (vendor_slot(item) < 0 && *owned(item)) {
            continue;
        }

        int n = 0, present = 0;
        while (n < VENDOR_STOCK_SIZE && stock[n] != STOCK_END) {
            present |= (stock[n] & STOCK_ITEM) == item;
            ++n;
        }

        // Listed already, or no room left in the stock.
        if (present || n >= VENDOR_STOCK_SIZE) {
            continue;
        }

        stock[n] = (uint8_t)item;
        if (n + 1 < VENDOR_STOCK_SIZE) {
            stock[n + 1] = STOCK_END;
        }
    }
}
