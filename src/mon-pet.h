/**
 * \file mon-pet.h
 * \brief Flags, structures and variables for monster pets
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

struct monster;

extern struct file_parser interact_parser;

bool mon_anger(struct monster *mon);
bool mon_hates_you(const struct monster *mon);
bool mon_hates_mon(const struct monster *attacker, const struct monster *victim);
