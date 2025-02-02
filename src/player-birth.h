/**
 * \file player-birth.h
 * \brief Character creation
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
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

#ifndef PLAYER_BIRTH_H
#define PLAYER_BIRTH_H

#include "cmd-core.h"

extern void setup_menus(void);
extern void player_init(struct player *p);
extern void player_generate(struct player *p, struct player_race *r, struct player_race *e,
                            struct player_class *c, struct player_race *per, bool old_history);
extern char *get_history(struct history_chart *h);
extern void add_start_items(struct player *p, const struct start_item *si, bool skip, bool pay, int origin);
extern void wield_all(struct player *p);

extern void get_height_weight(struct player *p);
extern void roll_hp(void);
extern int hitdie_class(const struct player_class *c);

extern bool player_make_simple(const char *nrace, const char *next, const char *nclass,
	const char *nplayer);

extern void select_artifact_max(void);

void do_cmd_birth_init(struct command *cmd);
void do_cmd_birth_reset(struct command *cmd);
void do_cmd_choose_race(struct command *cmd);
void do_cmd_choose_ext(struct command *cmd);
void do_cmd_choose_personality(struct command *cmd);
void do_cmd_choose_class(struct command *cmd);
void do_cmd_buy_stat(struct command *cmd);
void do_cmd_sell_stat(struct command *cmd);
void do_cmd_reset_stats(struct command *cmd);
void do_cmd_refresh_stats(struct command *cmd);
void do_cmd_roll_stats(struct command *cmd);
void do_cmd_prev_stats(struct command *cmd);
void do_cmd_choose_name(struct command *cmd);
void do_cmd_choose_history(struct command *cmd);
void do_cmd_accept_character(struct command *cmd);

void player_embody(struct player *p);

char *find_roman_suffix_start(const char *buf);

#endif /* !PLAYER_BIRTH_H */
