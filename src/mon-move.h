/**
 * \file mon-move.h
 * \brief Monster movement
 *
 * Copyright (c) 1997 Ben Harrison, David Reeve Sward, Keldon Jones.
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
#ifndef MONSTER_MOVE_H
#define MONSTER_MOVE_H


bool multiply_monster(struct chunk *c, const struct monster *mon);
void process_monsters(struct chunk *c, int minimum_energy);
void reset_monsters(void);
void restore_monsters(void);
bool mon_race_hates_grid(struct chunk *c, struct monster_race *race,
							   struct loc grid);

#endif /* !MONSTER_MOVE_H */
