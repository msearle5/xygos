/**
 * \file mon-pet.h
 * \brief Pets - friendly and neutral monsters
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

#include "datafile.h"
#include "init.h"
#include "mon-pet.h"
#include "monster.h"
#include "parser.h"
#include "z-bitflag.h"
#include "z-util.h"

static bitflag **mon_vs_mon;

/**
 * ------------------------------------------------------------------------
 * Initialize monster interactions
 * ------------------------------------------------------------------------ */
static bool parse_interact_vs_mon(int *races, struct monster_base **base, const char *in)
{
	bool all = false;
	if (*in == '!') {
		all = true;
		in++;
	}
	if (*in) {
		struct monster_base *b = NULL;
		for (b = rb_info; b; b = b->next) {
			if (streq(b->name, in)) {
				*base = b;
				break;
			}
		}
		if (!*base) {
			*races = 0;
			for (int i=0;i<z_info->r_max; i++) {
				if (strstr(r_info[i].name, in)) {
					(*races)++;
				}
			}
		}
	}
	return all;
}

static enum parser_error parse_interact_vs(struct parser *p) {
	/* An attacker or defender may be specified as:
	 * "!" 				- Any monster
	 * "<Monster Base>"	- All monsters of that base
	 * "<String>"		- All monsters containing that string
	 * "!<Base or Mon>" - Except that base or monster
	 * These are parsed in order, so that you could for example use:
	 * vs:ant:!
	 * vs:ant:!ant
	 * vs:black ant:red ant
	 * vs:red ant:black ant
	 * 
	 * to mean "ants attack everything except other ants, with the exception
	 * that black and red ants will attack each other."
	 *
	 * Limits are added for attacking much more dangerous monsters.
	 * SMART monsters don't go for anything more than 9/7 their level, while
	 * non-STUPID ones don't go for anything more than 9/5 their level.
	 * 
	 * UNIQUE monsters are more aggressive, because they are typically more
	 * powerful than others of their level (SMART = 11/7, non-STUPID 11/5).
	 *
	 * Other monsters will never intentionally attack a UNIQUE, QUESTOR, 
	 * SPECIAL_GEN or rarity-0 (all defined here). Ideally they would not
	 * attack other monsters that are found normally but are special to a
	 * quest, but that's something that would need to be handled elsewhere.
	 * (Even this doesn't prevent them attacking uniques, etc. altogether.
	 *  They could be confused, or catch them in an an area attack.) 
	 */

	/* Read, ensure at least 1 char in both */
	const char *att = parser_getsym(p, "attack");
	const char *vic = parser_getsym(p, "victim");
	if ((!att) || (!vic) || (!*att) || (!*vic))
		return PARSE_ERROR_MISSING_FIELD;

	/* Parse both fields, identically.
	 * They must both have 1+ races, a base, or the all-flag.
	 * (The combination of all-flag and a race(s) or base is also OK.)
	 */
	int races_att = 0;
	struct monster_base *base_att = NULL;
	bool all_att = parse_interact_vs_mon(&races_att, &base_att, att);
	if (!all_att && !races_att && !base_att)
		return PARSE_ERROR_INVALID_VALUE;

	int races_def = 0;
	struct monster_base *base_def = NULL;
	bool all_def = parse_interact_vs_mon(&races_def, &base_def, vic);
	if (!all_def && !races_def && !base_def)
		return PARSE_ERROR_INVALID_VALUE;

	/* Scan all combinations of attacker and defender.
	 * If both match (all/race/base) and it's not forbidden by level or flags, then set the mon-vs-mon flag.
	 */
	for(int a=0;a<z_info->r_max;a++) {
		struct monster_race *ar = &r_info[a];
		bool aok;
		if (all_att) {
			aok = true;
			if (base_att) {
				aok = (ar->base != base_att);
			} else if (races_att) {
				aok = !(strstr(ar->name, att));
			}
		} else {
			aok = false;
			if (base_att) {
				aok = (ar->base == base_att);
			} else {
				aok = (strstr(ar->name, att));
			}
		}
		/* Attacker matches */
		if (aok) {
			int eff_att_lev = ar->level * 9;
			if (rf_has(ar->flags, RF_UNIQUE))
				eff_att_lev = ar->level * 11;
			for(int d=0;d<z_info->r_max;d++) {
				struct monster_race *dr = &r_info[d];
				/* Check forbidden targets */
				if (rf_has(dr->flags, RF_UNIQUE))
					continue;
				if (rf_has(dr->flags, RF_QUESTOR))
					continue;
				if (rf_has(dr->flags, RF_SPECIAL_GEN))
					continue;
				if (dr->rarity == 0)
					continue;
				/* Check level */
				if (!rf_has(dr->flags, RF_STUPID)) {
					int scary_lev = dr->level * 5;
					if (rf_has(dr->flags, RF_SMART))
						scary_lev = dr->level * 7;
					if (eff_att_lev < scary_lev)
						continue;
				}
				bool dok;
				if (all_def) {
					dok = true;
					if (base_def) {
						dok = (dr->base != base_def);
					} else if (races_def) {
						dok = !(strstr(dr->name, vic));
					}
				} else {
					dok = false;
					if (base_def) {
						dok = (dr->base == base_def);
					} else {
						dok = (strstr(dr->name, vic));
					}
				}
				/* Defender also matches */
				if (dok) {
					/* Set the flag */
					flag_on(mon_vs_mon[a], FLAG_SIZE(z_info->r_max), d);
				}
			}
		}
	}
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_interact(void) {
	mon_vs_mon = mem_zalloc(sizeof(*mon_vs_mon) * z_info->r_max);
	for(int i=0;i<z_info->r_max;i++)
		mon_vs_mon[i] = mem_zalloc(FLAG_SIZE(z_info->r_max));
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "vs sym attack sym victim", parse_interact_vs);
	return p;
}

static errr run_parse_interact(struct parser *p) {
	return parse_file_quit_not_found(p, "monster_interact");
}

static errr finish_parse_interact(struct parser *p) {
	return 0;
}

static void cleanup_interact(void)
{
	for(int i=0;i<z_info->r_max;i++)
		mem_free(mon_vs_mon[i]);
	mem_free(mon_vs_mon);
	mon_vs_mon = NULL;
}

struct file_parser interact_parser = {
	"monster_interact",
	init_parse_interact,
	run_parse_interact,
	finish_parse_interact,
	cleanup_interact
};


/** If true, the monster will attack you on sight in the traditional way */
bool mon_hates_you(const struct monster *mon)
{
	if (mflag_has(mon->mflag, MFLAG_FRIENDLY))
		return false;
	if (mflag_has(mon->mflag, MFLAG_NEUTRAL))
		return false;
	return true;
}

/** If true, the monster 'attacker' will attack another 'victim' if possible.
 * These are attacks of opportunity: it won't by itself prevent the monster
 * from attacking you as well.
 */
bool mon_hates_mon(const struct monster *attacker, const struct monster *victim)
{
	return flag_has(mon_vs_mon[attacker->race - r_info], FLAG_SIZE(z_info->r_max), victim->race - r_info);
}
