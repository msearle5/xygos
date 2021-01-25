/**
 * \file player-ability.c
 * \brief All ability-related code
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

#include "angband.h"
#include "datafile.h"
#include "init.h"
#include "player.h"
#include "player-ability.h"

/** The list of abilities (indexed by player flag) */
struct ability *ability[PF_MAX];

/*
 * An ability is a player flag - it may be entirely positive, entirely negative
 * or a mixture and includes "talents" which can be bought with talent points,
 * and "mutations" which are acquired through a mutation - and it may include
 * some abilities which are neither, or both.
 * 
 * Talents can be bought at any time, but some may be birth-only or limited to
 * being gained after a certain level.
 * 
 * They are stored as player race/class flags and tested in the same way, but
 * have an additional ability structure. These are added to ability[] when the
 * ability.txt is parsed - a player flag with ability[<flag>] == NULL is not
 * an ability.
 * 
 * Gaining an ability may be a requirement to gain another, or block it.
 * 
 * Abilities can be listed in a player knowledge screen.
 */


/**
 * Parsing functions for ability.txt
 */
static enum parser_error parse_ability_name(struct parser *p) {
	struct ability *a = mem_zalloc(sizeof(*a));
	parser_setpriv(p, a);
	a->name = string_make(parser_getstr(p, "name"));

	/* Determine which entry to accept */
	#define PF(N) if (!my_stricmp(#N, a->name)) { ability[PF_##N] = a; return PARSE_ERROR_NONE; }
	#include "list-player-flags.h"
	#undef PF

	return PARSE_ERROR_INVALID_PLAYER_FLAG;
}

static enum parser_error parse_ability_gain(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	a->gain = string_make(parser_getstr(p, "gain"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_ability_lose(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	a->lose = string_make(parser_getstr(p, "lose"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_ability_brief(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	a->brief = string_make(parser_getstr(p, "brief"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_ability_desc(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	a->desc = string_make(parser_getstr(p, "desc"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_ability_desc_future(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	a->desc_future = string_make(parser_getstr(p, "desc_future"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_ability_maxlevel(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	a->maxlevel = parser_getint(p, "max");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_ability_minlevel(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	a->minlevel = parser_getint(p, "min");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_ability_cost(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	a->cost = parser_getint(p, "cost");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_ability_flag(struct parser *p) {
	struct ability *a = parser_priv(p);
	assert(a);

	const char *text = parser_getstr(p, "flag");
	u32b flag = 0;

	if (!my_stricmp(text, "birth"))
		flag = AF_BIRTH;
	if (!my_stricmp(text, "mutation"))
		flag = AF_MUTATION;
	if (!my_stricmp(text, "talent"))
		flag = AF_TALENT;
	if (!my_stricmp(text, "nasty"))
		flag = AF_NASTY;

	if (flag)
		a->flags |= flag;
	else
		return PARSE_ERROR_INVALID_FLAG;

	return PARSE_ERROR_NONE;
}

struct parser *init_parse_ability(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "name str name", parse_ability_name);
	parser_reg(p, "gain str gain", parse_ability_gain);
	parser_reg(p, "lose str lose", parse_ability_lose);
	parser_reg(p, "brief str brief", parse_ability_brief);
	parser_reg(p, "desc str desc", parse_ability_desc);
	parser_reg(p, "desc_future str desc_future", parse_ability_desc_future);
	parser_reg(p, "cost int cost", parse_ability_cost);
	parser_reg(p, "maxlevel uint max", parse_ability_maxlevel);
	parser_reg(p, "minlevel uint min", parse_ability_minlevel);
	parser_reg(p, "flag str flag", parse_ability_flag);
	return p;
}

static errr run_parse_ability(struct parser *p) {
	return parse_file_quit_not_found(p, "ability");
}

static errr finish_parse_ability(struct parser *p) {
	parser_destroy(p);
	return 0;
}

static void cleanup_ability(void)
{
	int idx;
	for (idx = 0; idx < PF_MAX; idx++) {
		if (ability[idx]) {
			string_free(ability[idx]->name);
			mem_free(ability[idx]);
		}
	}
}

struct file_parser ability_parser = {
	"ability",
	init_parse_ability,
	run_parse_ability,
	finish_parse_ability,
	cleanup_ability
};


