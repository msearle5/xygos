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


struct attack {
	char *msg;								/**< "You "smack" the foo */
	random_value damage;					/**< for 6d6 of */
	int element;							/**< fire damage */
};

struct ability {
	char *name;
	char *gain;
	char *lose;
	char *brief;
	char *desc;
	char *desc_future;
	char *class;
	uint32_t flags;
	int16_t minlevel;
	int16_t maxlevel;
	int16_t  cost;
	int ac;
	int tohit;
	int todam;
	struct class_magic magic;
	int effect_randomly;
	struct effect *effect;
	int nattacks;
	struct attack *attacks;
	bool forbid[PF_MAX];
	bool require[PF_MAX];
	bool cancel[PF_MAX];
	int a_adj[STAT_MAX];
	int16_t mom_speed[MOM_SPEED_MAX];			/**< Momentum - to - speed */
	bitflag oflags[OF_SIZE];				/**< Racial (object) flags */
	bitflag oflags_off[OF_SIZE];			/**< Racial (object) flags (turn off) */
	bitflag pflags[PF_SIZE];				/**< Racial (player) flags */
	struct element_info el_info[ELEM_MAX];	/**< Resists */
	int16_t  modifiers[OBJ_MOD_MAX];
};

/* Ability flags */
#define AF_BIRTH		0x00000001		/* can take at birth only */
#define AF_NASTY		0x00000002		/* has at least some negatives to some characters */
#define AF_TALENT		0x00000004		/* can be bought as a talent */
#define AF_MUTATION		0x00000008		/* can be gained as a mutation */
#define AF_FLYING		0x00000010		/* flags and speed take effect only when in flight. Books are ground/flight/both. */
#define AF_SUPER		0x00000020		/* can be gained through level up as a Super */

/* The ability array */
extern struct ability *ability[];

extern struct file_parser ability_parser;

void changed_abilities(void);
bool ability_levelup(struct player *p, int from, int to);
int setup_talents(void);
int cmd_abilities(struct player *p, bool birth, int selected, bool *flip);
int ability_to_stat(int stat);
void init_talent(int initial_tp, int orig_tp);
bool get_mutation(unsigned long flags, bool allow_loss);
bool mutate(void);
bool gain_ability(unsigned a, bool birth);
