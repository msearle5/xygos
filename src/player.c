/**
 * \file player.c
 * \brief Player implementation
 *
 * Copyright (c) 2011 elly+angband@leptoquark.net. See COPYING.
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

#include "effects.h"
#include "init.h"
#include "game-world.h"
#include "obj-knowledge.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-ability.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-quest.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "randname.h"
#include "ui-command.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-player.h"
#include "z-color.h"
#include "z-util.h"

/**
 * Pointer to the player struct
 */
struct player *player = NULL;

struct player_body *bodies;
struct player_race *races;
struct player_shape *shapes;
struct player_class *classes;
struct player_ability *player_abilities;

/**
 * Base experience levels, may be adjusted up or down for race, class, etc.
 */
s32b player_exp[PY_MAX_LEVEL];


static const char *stat_name_list[] = {
	#define STAT(a) #a,
	#include "list-stats.h"
	#undef STAT
	"MAX",
    NULL
};

int stat_name_to_idx(const char *name)
{
    int i;
    for (i = 0; stat_name_list[i]; i++) {
        if (!my_stricmp(name, stat_name_list[i]))
            return i;
    }

    return -1;
}

const char *stat_idx_to_name(int type)
{
    assert(type >= 0);
    assert(type < STAT_MAX);

    return stat_name_list[type];
}

bool player_stat_inc(struct player *p, int stat)
{
	int v = p->stat_cur[stat];

	if (v >= 18 + 100)
		return false;
	if (v < 18) {
		p->stat_cur[stat]++;
	} else if (v < 18 + 90) {
		int gain = (((18 + 100) - v) / 2 + 3) / 2;
		if (gain < 1)
			gain = 1;
		p->stat_cur[stat] += randint1(gain) + gain / 2;
		if (p->stat_cur[stat] > 18 + 99)
			p->stat_cur[stat] = 18 + 99;
	} else {
		p->stat_cur[stat] = 18 + 100;
	}

	if (p->stat_cur[stat] > p->stat_max[stat])
		p->stat_max[stat] = p->stat_cur[stat];
	
	p->upkeep->update |= PU_BONUS;
	return true;
}

bool player_stat_dec(struct player *p, int stat, bool permanent)
{
	int cur, max, res = false;

	cur = p->stat_cur[stat];
	max = p->stat_max[stat];

	if (cur > 18+10)
		cur -= 10;
	else if (cur > 18)
		cur = 18;
	else if (cur > 3)
		cur -= 1;

	res = (cur != p->stat_cur[stat]);

	if (permanent) {
		if (max > 18+10)
			max -= 10;
		else if (max > 18)
			max = 18;
		else if (max > 3)
			max -= 1;

		res = (max != p->stat_max[stat]);
	}

	if (res) {
		p->stat_cur[stat] = cur;
		p->stat_max[stat] = max;
		p->upkeep->update |= (PU_BONUS);
		p->upkeep->redraw |= (PR_STATS);
	}

	return res;
}

/* Experience needed to gain the given level.
 * (The -2 is because the first entry in the table, player_exp[0], is the requirement to gain level 2.)
 */ 
s32b exp_to_gain(s32b level)
{
	/* Base exp to advance, ignoring exp factors */
	double exp = player_exp[level-2];

	/* Avoid division by 0 - probably can't happen, though */
	if (level <= 1)
		return 0;

	/* Class exp factor - average of all levels' classes' exp factors */
	s32b c_exp = 0;
	for(int l=1; l<level; l++) {
		c_exp += get_class_by_idx(player->lev_class[l])->c_exp;
	}
	double t_exp = c_exp;
	t_exp /= (level-1);

	/* Level-1 and max-level race/ext factors */
	double exp_low = player->expfact_low;
	exp_low += t_exp;
	double exp_high = player->expfact_high;
	exp_high += t_exp;

	/* Base exp */
	exp_low *= exp;
	exp_high *= exp;

	/* Fractional factors */
	double prop_high = (level - 2) / (PY_MAX_LEVEL - 2);
	double prop_low = 1.0 - prop_high;

	/* Mix */
	double frac_high = exp_high * prop_high;
	double frac_low = exp_low * prop_low;
	double exp_mix = frac_high + frac_low;

	/* And return as percentage */
	return exp_mix / 100.0;
}


/* Convert experience to level
 */ 
static s32b exp_to_lev(s32b exp)
{
	s32b lev = 1;
	while ((lev < PY_MAX_LEVEL) &&
	       (exp >= exp_to_gain(lev))) {
		lev++;
	}
	return lev;
}

void auto_char_dump(void)
{
	if (OPT_birth_auto_char_dump)
	{
		char buf[1024];
		char file_name[256];
		char player_name[80];

		screen_save();

		/* Get the filesystem-safe name and append .txt */
		player_safe_name(player_name, sizeof(player_name), player->full_name, false);
		strnfmt(file_name, sizeof(file_name), "%s_%x_%d.txt", player_name, seed_flavor ^ seed_randart, player->lev);

		path_build(buf, sizeof(buf), ANGBAND_DIR_USER, file_name);
		if (!dump_save(buf))
			msg("Automatic character dump failed!");

		screen_load();
	}
}

static void adjust_level(struct player *p, bool verbose)
{
	char buf[80];
	bool message = false;

	if (p->exp < 0)
		p->exp = 0;

	if (p->max_exp < 0)
		p->max_exp = 0;

	if (p->exp > PY_MAX_EXP)
		p->exp = PY_MAX_EXP;

	if (p->max_exp > PY_MAX_EXP)
		p->max_exp = PY_MAX_EXP;

	if (p->exp > p->max_exp)
		p->max_exp = p->exp;

	int max_from = p->max_lev;

	p->upkeep->redraw |= PR_EXP;

	handle_stuff(p);

	while ((p->lev > 1) &&
	       (p->exp < (exp_to_gain(p->lev))))
		p->lev--;


	while ((p->lev < PY_MAX_LEVEL) &&
	       (p->exp >= exp_to_gain(p->lev+1))) {

		p->lev++;

		/* Save the highest level */
		if (p->lev > p->max_lev)
			p->max_lev = p->lev;

		message = true;

		for(int i=0;i<STAT_MAX;i++)
			effect_simple(EF_RESTORE_STAT, source_none(), "0", STAT_STR+i, 0, 0, 0, 0, NULL);
	}

	while ((p->max_lev < PY_MAX_LEVEL) &&
	       (p->max_exp >= (exp_to_gain(p->max_lev+1))))
		p->max_lev++;

	int newexp = exp_to_gain(p->max_lev-1);

	if (p->max_lev > max_from) {
		for(int i=max_from+1; i<=p->max_lev; i++)
			p->lev_class[i] = p->switch_class;
	}

	/* Switch class */
	if ((p->max_lev > max_from) && (p->switch_class != p->lev_class[max_from])) {
		if (verbose) {
			const char *c = get_class_by_idx(p->switch_class)->name;

			/* Log class change */
			strnfmt(buf, sizeof(buf), "Level up, changed to %s", c);
			history_add(p, buf, HIST_GAIN_LEVEL);

			msgt(MSG_LEVEL, "You are now training as a %s.", c);
		}
		p->exp = p->max_exp = newexp;

		while ((p->lev > 1) &&
			   (p->exp < (exp_to_gain(p->lev))))
			p->lev--;

		while ((p->lev < PY_MAX_LEVEL) &&
			   (p->exp >= exp_to_gain(p->lev+1)))
			p->lev++;

		if (p->lev > p->max_lev)
			p->max_lev = p->lev;
	} else {
		if (verbose && message ) {
			/* Log level updates */
			strnfmt(buf, sizeof(buf), "Reached level %d", p->lev);
			history_add(p, buf, HIST_GAIN_LEVEL);

			/* Message */
			msgt(MSG_LEVEL, "Welcome to level %d.",	p->lev);
		}
	}

	set_primary_class();

	if (p->max_lev > max_from) {
		ability_levelup(p, max_from, p->max_lev);
		player_hook(levelup, max_from, p->max_lev);
		if (player->split_p)
			personality_split_level(max_from, p->max_lev);
	}

	/* You may have new intrinsics.
	 * Notice them.
	 */
	for (struct player_class *c = classes; c; c = c->next) {
		int levels = levels_in_class(c->cidx);
		if (levels) {
			for (int i=0;i<OF_MAX;i++) {
				if (of_has(c->flags[levels], i)) {
					player_learn_flag(p, i);
				}
			}
		}
	}
	update_player_object_knowledge(p);

	p->upkeep->update |= (PU_BONUS | PU_HP | PU_SPELLS);
	p->upkeep->redraw |= (PR_LEV | PR_TITLE | PR_EXP | PR_STATS);
	handle_stuff(p);

	if ((p->max_lev == 1) || (p->max_lev > max_from))
		auto_char_dump();
}

/* Cap large experience gains.
 * The first level gained is free (as you might be very close to it already), but at each
 * following level gain the remaining exp is halved. Recovery from drain (levels gained
 * below your maximum level) is always free.
 * At level 49-50, this doesn't apply.
 * Still need to take care of gaining levels into 50 from below.
 */
s32b player_exp_scale(s32b amount)
{
	struct player *p = player;

	/* Two levels above your maximum (one level above would be the amount needed to gain the next level)
	 * is the first point to care about
	 */
	if (p->max_lev >= PY_MAX_LEVEL - 1)
		return amount;

	/* This much exp is free */
	s32b free_gain = exp_to_gain(p->max_lev + 2) - p->exp;
	if (amount <= free_gain)
		return amount;

	s32b sum = free_gain;
	s32b remainder = amount - sum;
	s32b level = 1;
	while ((exp_to_lev(p->exp + sum) != exp_to_lev(p->exp + sum + remainder)) && (level < PY_MAX_LEVEL)) { 
		/* Divide the rest by 2, until no longer gaining levels */
		level = exp_to_lev(p->exp + sum);
		s32b thislevel = exp_to_gain(level);
		s32b nextlevel = exp_to_gain(level+1);
		s32b gain = nextlevel - thislevel;
		if (gain > remainder) {
			gain = remainder;
		}
		remainder -= gain;
		remainder /= 2;
		sum += gain;
	}; 

	return sum + remainder;
}

void player_exp_gain(struct player *p, s32b amount)
{
	p->exp += amount;
	if (p->exp < p->max_exp)
		p->max_exp += amount / 10;
	adjust_level(p, true);
}

void player_exp_gain_scaled(struct player *p, s32b amount)
{
	amount = player_exp_scale(amount);
	if (p->exp + amount < p->max_exp) {
		p->exp += amount;
		p->max_exp += player_exp_scale(amount / 10);
	} else {
		p->exp += player_exp_scale(amount);
	}
	adjust_level(p, true);
}

void player_exp_lose(struct player *p, s32b amount, bool permanent)
{
	if (p->exp < amount)
		amount = p->exp;
	p->exp -= amount;
	if (permanent)
		p->max_exp -= amount;
	adjust_level(p, true);
}

/**
 * Obtain object flags for the player
 */
void player_flags(struct player *p, bitflag f[OF_SIZE])
{
	/* Add racial flags */
	memcpy(f, p->race->flags[p->lev], sizeof(p->race->flags[0]));
	of_union(f, p->extension->flags[p->lev]);
	of_union(f, p->personality->flags[p->lev]);

	/* Add object-flags from class */
	for (struct player_class *c = classes; c; c = c->next) {
		int levels = levels_in_class(c->cidx);
		if (levels)
			of_union(f, c->flags[levels]);
	}

	/* Some classes become immune to fear at a certain plevel */
	if (player_has(p, PF_BRAVERY_30) && p->lev >= 30) {
		of_on(f, OF_PROT_FEAR);
	}
}


/**
 * Combine any flags due to timed effects on the player into those in f.
 */
void player_flags_timed(struct player *p, bitflag f[OF_SIZE])
{
	if (p->timed[TMD_BOLD] || p->timed[TMD_HERO] || p->timed[TMD_SHERO]) {
		of_on(f, OF_PROT_FEAR);
	}
	if (p->timed[TMD_TELEPATHY]) {
		of_on(f, OF_TELEPATHY);
	}
	if (p->timed[TMD_SINVIS]) {
		of_on(f, OF_SEE_INVIS);
	}
	if (p->timed[TMD_FREE_ACT]) {
		of_on(f, OF_FREE_ACT);
	}
	if (p->timed[TMD_AFRAID] || p->timed[TMD_TERROR]) {
		of_on(f, OF_AFRAID);
	}
	if (p->timed[TMD_OPP_CONF]) {
		of_on(f, OF_PROT_CONF);
	}
}


byte player_hp_attr(struct player *p)
{
	byte attr;
	
	if (p->chp >= p->mhp)
		attr = COLOUR_L_GREEN;
	else if (p->chp > (p->mhp * p->opts.hitpoint_warn) / 10)
		attr = COLOUR_YELLOW;
	else
		attr = COLOUR_RED;
	
	return attr;
}

/**
 * Construct a random player name appropriate for the setting.
 *
 * \param buf is the buffer to contain the name.  Must have space for at
 * least buflen characters.
 * \param buflen is the maximum number of character that can be written to
 * buf.
 * \return the number of characters, excluding the terminating null, written
 * to the buffer
 */
size_t player_random_name(char *buf, size_t buflen)
{
	size_t result = randname_make(RANDNAME_TOLKIEN, 4, 8, buf, buflen,
		name_sections);

	my_strcap(buf);
	return result;
}

/**
 * Return a version of the player's name safe for use in filesystems.
 *
 * XXX This does not belong here.
 */
void player_safe_name(char *safe, size_t safelen, const char *name, bool strip_suffix)
{
	size_t i;
	size_t limit = 0;

	if (name) {
		char *suffix = find_roman_suffix_start(name);

		if (suffix) {
			limit = suffix - name - 1; /* -1 for preceding space */
		} else {
			limit = strlen(name);
		}
	}

	/* Limit to maximum size of safename buffer */
	limit = MIN(limit, safelen);

	for (i = 0; i < limit; i++) {
		char c = name[i];

		/* Convert all non-alphanumeric symbols */
		if (!isalpha((unsigned char)c) && !isdigit((unsigned char)c))
			c = '_';

		/* Build "base_name" */
		safe[i] = c;
	}

	/* Terminate */
	safe[i] = '\0';

	/* Require a "base" name */
	if (!safe[0])
		my_strcpy(safe, "PLAYER", safelen);
}


/**
 * Release resources allocated for fields in the player structure.
 */
void player_cleanup_members(struct player *p)
{
	/* Free the history */
	history_clear(p);

	/* Free the things that are always initialised */
	if (p->obj_k) {
		object_free(p->obj_k);
	}
	mem_free(p->timed);
	if (p->upkeep) {
		mem_free(p->upkeep->quiver);
		mem_free(p->upkeep->inven);
		mem_free(p->upkeep);
		p->upkeep = NULL;
	}

	/* Free the things that are only sometimes initialised */
	if (p->quests) {
		player_quests_free(p);
	}
	if (p->spell_flags) {
		player_spells_free(p);
	}
	if (p->gear) {
		object_pile_free(p->gear);
		object_pile_free(p->gear_k);
	}
	if (p->body.slots) {
		for (int i = 0; i < p->body.count; i++)
			string_free(p->body.slots[i].name);
		mem_free(p->body.slots);
	}
	string_free(p->body.name);
	string_free(p->history);
	if (p->cave) {
		cave_free(p->cave);
		p->cave = NULL;
	}
}


/**
 * Initialise player struct
 */
static void init_player(void) {
	/* Create the player array, initialised with 0 */
	player = mem_zalloc(sizeof *player);

	/* Allocate player sub-structs */
	player->upkeep = mem_zalloc(sizeof(struct player_upkeep));
	player->upkeep->inven = mem_zalloc((z_info->pack_size + 1) * sizeof(struct object *));
	player->upkeep->quiver = mem_zalloc(z_info->quiver_size * sizeof(struct object *));
	player->timed = mem_zalloc(TMD_MAX * sizeof(s16b));
	player->obj_k = object_new();
	player->obj_k->brands = mem_zalloc(z_info->brand_max * sizeof(bool));
	player->obj_k->slays = mem_zalloc(z_info->slay_max * sizeof(bool));
	player->obj_k->faults = mem_zalloc(z_info->fault_max *
									   sizeof(struct fault_data));

	options_init_defaults(&player->opts);
}

/**
 * Free player struct
 */
static void cleanup_player(void) {
	if (!player) return;

	player_cleanup_members(player);

	/* Free the basic player struct */
	mem_free(player);
	player = NULL;
}

struct init_module player_module = {
	.name = "player",
	.init = init_player,
	.cleanup = cleanup_player
};
