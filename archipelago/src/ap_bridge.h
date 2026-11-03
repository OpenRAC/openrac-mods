/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the OpenRAC contributors
 *
 * The C face of the Archipelago client (ap_client.cpp) for the game side (archipelago.c).
 *
 * The client runs on a thread of its own: it connects to the server, keeps the items the server
 * sent this slot and the lines to show, and sends what the game side asks. Every call here only
 * takes or leaves something under a lock, so the game's thread never waits on the network.
 */
#ifndef AP_BRIDGE_H
#define AP_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where to connect, and as whom. The strings are copied. */
typedef struct ap_config {
    const char* server;   /* "host:port", "ws://host:port" or "wss://host:port" */
    const char* slot;     /* the player's slot name */
    const char* password; /* the room's password, "" for none */
    const char* uuid;     /* this machine's id for the server, made once by the game side */
    const char* data_dir; /* the mod's own folder, for which room's game each save slot holds; null: none */
} ap_config;

/* What the client is doing (ap_client_state). */
enum {
    AP_STATE_OFF = 0,    /* not started, or stopped */
    AP_STATE_CONNECTING, /* looking for the server, or waiting to try again */
    AP_STATE_CONNECTED,  /* in the room as the slot */
    AP_STATE_REFUSED     /* the server said no (slot, password, game): it does not try again */
};

/* Starts the client's thread: it connects, and connects again when the link drops. */
void ap_client_start(const ap_config* config);

/* Stops the thread and lets the connection go. */
void ap_client_stop(void);

/* AP_STATE_*. */
int ap_client_state(void);

/* A location found: sent now when connected, else when the client is next. Sending one the
 * server has already is harmless. */
void ap_client_check(int64_t location);

/* The slot's goal reached (Drek beaten): the server is told, now or when next connected. */
void ap_client_goal(void);

/* The locations the loaded save has had, at a level start, and its memory card slot (-1: not
 * known): sent, unless the save is another seed's (its slot holds another room's game, or, not
 * known, it has checks the server never saw from the slot and lacks some it did). */
void ap_client_save_loaded(const int64_t* had, size_t n, int card_slot);

/* The game in progress was just saved into memory card slot `card_slot`: it holds this room's game. */
void ap_client_save_written(int card_slot);

/* Every item the server has sent this slot so far, in its order: at most `max` into `out`.
 * Returns how many there are (more than max when out was too small). *generation (when not null)
 * changes whenever the list does, so the caller can tell when it needs to look again. */
size_t ap_client_items(int64_t* out, size_t max, uint32_t* generation);

/* Whether the server's list of items has been had since the client connected (until then the
 * list may be short of what the slot really has). */
int ap_client_synced(void);

/* The next line to show the player (an item sent or received, the connection), oldest first.
 * Returns 1 and the line in out (cut to size), or 0 when there is none. */
int ap_client_message(char* out, size_t size);

/* What this slot's location holds, when the server has told (a scout of every location at the
 * connection): the item's id and whether it is this slot's own. Returns 1 when known. */
int ap_client_location_item(int64_t location, int64_t* item, int* own);

#ifdef __cplusplus
}
#endif

#endif /* AP_BRIDGE_H */
