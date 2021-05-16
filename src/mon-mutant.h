/**
 * \file mon-mutant.h
 * \brief Monster mutations
 *
 * Copyright (c) 2021 Mike Searle
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

struct monster_race *mutate_monster(struct monster_race **mon, bool birth, int level);
struct monster_race *get_mutant_race_by_name(const char *name);
struct monster_race *select_mutation(struct monster_race *race, bool birth, int level);
