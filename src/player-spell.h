/**
 * \file player-spell.h
 * \brief Spell and prayer casting/praying
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

extern byte adj_mag_fail[STAT_RANGE];
extern s16b adj_mag_stat[STAT_RANGE];

void player_spells_init(struct player *p);
void player_spells_free(struct player *p);

const struct class_spell *spell_by_index(const struct player *p, int index);
int spell_book_count_spells(bool (*tester)(const struct player *p, int spell_index));
bool spell_okay_list(const struct player *p, bool (*spell_test)(const struct player *p, int spell_index), const int spells[],
					 int n_spells);
bool spell_okay_to_cast(const struct player *p, int spell_index);
bool spell_okay_to_browse(const struct player *p, int spell_index);
s16b spell_chance(int spell_index);
bool spell_cast(int spell_index, int dir, struct command *cmd);
struct class_spell *combine_books(const struct player *p, int *count, int *spells, int *maxidx, struct class_spell **spellps, int *books, struct class_book ***book);

extern void spell_fold_book(struct player *p, int book);
extern void spell_unfold_all(struct player *p);
extern int spell_count_visible(const struct player *p);
extern void get_spell_info(int index, char *buf, size_t len);
extern bool cast_spell(int tval, int index, int dir);
extern bool spell_needs_aim(int spell_index);
extern expression_base_value_f spell_value_base_by_name(const char *name);
extern int spell_collect_from_book(struct player *p, int **spells, int *books, struct class_book ***book);
size_t append_random_value_string(char *buffer, size_t size, const random_value *rv);
