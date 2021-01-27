/**
 * \file player-ability.h
 * \brief Ability-related variables and functions
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

struct ability {
	char *name;
	char *gain;
	char *lose;
	char *brief;
	char *desc;
	char *desc_future;
	u32b flags;
	s16b minlevel;
	s16b maxlevel;
	s16b cost;
	bool forbid[PF_MAX];
	bool require[PF_MAX];
};

/* Ability flags */
#define AF_BIRTH		0x00000001		/* can take at birth only */
#define AF_NASTY		0x00000002		/* has at least some negatives to some characters */
#define AF_TALENT		0x00000004		/* can be bought as a talent */
#define AF_MUTATION		0x00000008		/* can be gained as a mutation */

/* The ability array */
extern struct ability *ability[];

extern struct file_parser ability_parser;

bool ability_levelup(struct player *p, int from, int to);
int setup_talents(void);
int cmd_abilities(struct player *p, bool birth, int selected, bool *flip);
