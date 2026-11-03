/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.1.54: the Xbox's trainers (.etm and .xbtf, Evolution-X's and XBMC4Xbox's) for the games.
 * They are the player's: OGBox ships none. Copied into the data folder's trainers (subfolders too),
 * /data/ogbox/trainers or a USB drive's OGBox/trainers, they are read once into an index (the
 * cache's trainers.idx: a file is read again only when its size or date changes) and each game's
 * Mods screen lists those for its title ID. One a game is chosen, with its options, in
 * /data/ogbox/mods.txt ("<game> trainer <file under its folder>", "<game> trainer-options 0101"),
 * and hakuX runs it before the game (native/core/trainer.h: trainer=, trainer_options=).
 */
#ifndef OGBOX_CORE_TRAINERS_H
#define OGBOX_CORE_TRAINERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OGBOX_TRAINER_TEXT 85
#define OGBOX_TRAINER_MAX_OPTIONS 100

/* The folders trainers are looked for in, first first (a chosen trainer's file is found in the
 * first that has it). Set by the front end before the first scan. */
void ogbox_trainers_set_roots(const char *const *roots, int n);

/* The folders read (on the caller's thread; the index at index_path kept up to date): how many
 * trainers are known. One scan at a time; while one runs, another returns at once with -1. */
int ogbox_trainers_scan(const char *index_path);
/* 0 never read, 1 reading, 2 read; *generation counts the scans finished. */
int ogbox_trainers_state(unsigned *generation, unsigned *found);

typedef struct OgboxTrainer {
    char rel[384];                  /* the file under its folder ("XBTF trainers/halo 2 p13.xbtf") */
    char path[640];                 /* the file */
    char name[OGBOX_TRAINER_TEXT];
    char author[OGBOX_TRAINER_TEXT];
    uint32_t title_ids[3];
    uint32_t xbe_time;              /* the XBE date it checks for, 0 if it doesn't say */
    unsigned noptions;
    bool xbtf;
} OgboxTrainer;

/* The trainers for a title (the same trainer in several files once), by name: how many. */
int ogbox_trainers_for_title(uint32_t title_id, OgboxTrainer *out, int max);
/* How many there are for a title. */
int ogbox_trainers_count(uint32_t title_id);

/* A trainer's options' names (read from its file) and its scroller text: how many, -1 unread. */
int ogbox_trainers_options(const char *rel, char (*names)[OGBOX_TRAINER_TEXT], int max, char *scroller, size_t n);
/* A file under the folders: its path; false if no folder has it. */
bool ogbox_trainers_resolve(const char *rel, char *path, size_t n);

/* The game's trainer: its file under its folder and its options ('1' on), false if none chosen. */
bool ogbox_trainers_chosen(const char *key, char *rel, size_t reln, char *bits, size_t bitsn);
/* Chosen ("" none: its options go too), and its options. */
bool ogbox_trainers_choose(const char *key, const char *rel);
bool ogbox_trainers_set_options(const char *key, const char *bits);

#ifdef __cplusplus
}
#endif

#endif
