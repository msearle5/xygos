/**
 * \file player-quest.h
 * \brief Quest-related variables and functions
 *
 * Copyright (c) 2013 Angband developers
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#ifndef QUEST_H
#define QUEST_H

#include "store.h"

/* Quest list */
extern struct quest *quests;
struct store_context;

/* Functions */
bool is_quest(struct player *p, int level);
void player_quests_reset(struct player *p);
void player_quests_free(struct player *p);
struct quest *get_quest_by_grid(struct loc grid);
bool quest_check(struct player *p, const struct monster *m);
extern struct file_parser quests_parser;
void quest_reward(struct quest *q, bool success, struct store_context *ctx);
struct quest *get_quest_by_name(const char *name);
bool quest_item_check(const struct object *obj);
void quest_enter_level(struct chunk *c);
bool in_town_quest(void);
void quest_changed_level(bool store);
bool quest_enter_building(struct store *store);
void quest_changing_level(void);
bool quest_is_rewardable(const struct quest *q);
bool quest_special_endings(struct store_context *ctx);
bool quest_selling_object(struct object *obj, struct store_context *ctx);
const char *quest_get_intro(const struct quest *q);
bool is_active_quest(struct player *p, int level);
bool is_blocking_quest(struct player *p, int level);
struct quest *quest_guardian(void);
struct quest *quest_guardian_of(struct town *town);
struct quest *quest_guardian_any(struct town *town);
bool quest_play_arena(struct player *p);
void quest_complete_fight(struct player *p, struct monster *mon);

#endif /* QUEST_H */
