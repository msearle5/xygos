/**
 * \file player-quest.c
 * \brief All quest-related code
 *
 * Copyright (c) 2013 Angband developers
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
#include "mon-util.h"
#include "monster.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-quest.h"
#include "store.h"

/**
 * Array of quests
 */
struct quest *quests;

/**
 * Parsing functions for quest.txt
 */
static enum parser_error parse_quest_name(struct parser *p) {
	const char *name = parser_getstr(p, "name");
	struct quest *h = parser_priv(p);

	struct quest *q = mem_zalloc(sizeof(*q));
	q->next = h;
	parser_setpriv(p, q);
	q->name = string_make(name);
	q->store = STORE_NONE;
	q->entry_min = -1;
	q->entry_max = -1;
	q->entry_feature = FEAT_NONE;

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_level(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->level = parser_getuint(p, "level");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_intro(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->intro = string_make(parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_desc(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->desc = string_make(parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_succeed(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->succeed = string_make(parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_failure(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->failure = string_make(parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_unlock(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->unlock = string_make(parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_entrymin(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->entry_min = parser_getuint(p, "min");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_entrymax(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->entry_max = parser_getuint(p, "max");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_entryfeature(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->entry_feature = lookup_feat(parser_getsym(p, "feature"));

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_flags(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	const char *in = parser_getstr(p, "flags");
	if (strstr(in, "active"))
		q->flags |= QF_ACTIVE;
	if (strstr(in, "essential"))
		q->flags |= QF_ESSENTIAL;
	if (strstr(in, "locked"))
		q->flags |= QF_LOCKED;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_race(struct parser *p) {
	struct quest *q = parser_priv(p);
	const char *name = parser_getstr(p, "race");
	assert(q);

	q->race = lookup_monster(name);
	if (!q->race)
		return PARSE_ERROR_INVALID_MONSTER;

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_store(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	const char *name = parser_getstr(p, "store");
	assert(name);

	/* Find the store */
	assert(stores);
	for (int i = 0; i < MAX_STORES; i++) {
		/* Get the store */
		struct store *store = &stores[i];
		assert(store->name);
		if (!strcmp(store->name, name)) {
			q->store = i;
			return PARSE_ERROR_NONE;
		}
	}

	return PARSE_ERROR_INVALID_VALUE;
}

static enum parser_error parse_quest_number(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->max_num = parser_getuint(p, "number");
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_quest(void) {
	struct parser *p = parser_new();
	parser_setpriv(p, NULL);
	parser_reg(p, "name str name", parse_quest_name);
	parser_reg(p, "level uint level", parse_quest_level);
	parser_reg(p, "race str race", parse_quest_race);
	parser_reg(p, "number uint number", parse_quest_number);
	parser_reg(p, "store str store", parse_quest_store);
	parser_reg(p, "entrymin uint min", parse_quest_entrymin);
	parser_reg(p, "entrymax uint max", parse_quest_entrymax);
	parser_reg(p, "entryfeature sym feature", parse_quest_entryfeature);
	parser_reg(p, "intro str text", parse_quest_intro);
	parser_reg(p, "desc str text", parse_quest_desc);
	parser_reg(p, "succeed str text", parse_quest_succeed);
	parser_reg(p, "failure str text", parse_quest_failure);
	parser_reg(p, "unlock str text", parse_quest_unlock);
	parser_reg(p, "flags str flags", parse_quest_flags);
	return p;
}

static errr run_parse_quest(struct parser *p) {
	return parse_file_quit_not_found(p, "quest");
}

static errr finish_parse_quest(struct parser *p) {
	struct quest *quest, *next = NULL;
	int count;

	/* Count the entries */
	z_info->quest_max = 0;
	quest = parser_priv(p);
	while (quest) {
		z_info->quest_max++;
		quest = quest->next;
	}

	/* Allocate the direct access list and copy the data to it */
	quests = mem_zalloc(z_info->quest_max * sizeof(*quest));
	count = z_info->quest_max - 1;
	for (quest = parser_priv(p); quest; quest = next, count--) {
		memcpy(&quests[count], quest, sizeof(*quest));
		quests[count].index = count;
		next = quest->next;
		if (count < z_info->quest_max - 1)
			quests[count].next = &quests[count + 1];
		else
			quests[count].next = NULL;

		mem_free(quest);
	}

	parser_destroy(p);
	return 0;
}

static void cleanup_quest(void)
{
	int idx;
	for (idx = 0; idx < z_info->quest_max; idx++)
		string_free(quests[idx].name);
	mem_free(quests);
}

struct file_parser quests_parser = {
	"quest",
	init_parse_quest,
	run_parse_quest,
	finish_parse_quest,
	cleanup_quest
};

/**
 * Check if the given level is a quest level.
 */
bool is_quest(int level)
{
	size_t i;

	/* Town is never a quest */
	if (!level) return false;

	for (i = 0; i < z_info->quest_max; i++)
		if (player->quests[i].level == level)
			return true;

	return false;
}

/**
 * Copy all the standard quests to the player quest history
 */
void player_quests_reset(struct player *p)
{
	size_t i;

	if (p->quests)
		player_quests_free(p);
	p->quests = mem_zalloc(z_info->quest_max * sizeof(struct quest));

	for (i = 0; i < z_info->quest_max; i++) {
		p->quests[i].name = string_make(quests[i].name);
		p->quests[i].succeed = string_make(quests[i].succeed);
		p->quests[i].failure = string_make(quests[i].failure);
		p->quests[i].intro = string_make(quests[i].intro);
		p->quests[i].desc = string_make(quests[i].desc);
		p->quests[i].level = quests[i].level;
		p->quests[i].race = quests[i].race;
		p->quests[i].max_num = quests[i].max_num;
		p->quests[i].flags = quests[i].flags;
		p->quests[i].store = quests[i].store;
		p->quests[i].unlock = quests[i].unlock;
		p->quests[i].entry_min = quests[i].entry_min;
		p->quests[i].entry_max= quests[i].entry_max;
		p->quests[i].entry_feature = quests[i].entry_feature;
	}
}

/**
 * Free the player quests
 */
void player_quests_free(struct player *p)
{
	size_t i;

	for (i = 0; i < z_info->quest_max; i++) {
		string_free(p->quests[i].name);
		string_free(p->quests[i].succeed);
		string_free(p->quests[i].failure);
		string_free(p->quests[i].intro);
		string_free(p->quests[i].desc);
	}
	mem_free(p->quests);
}

/**
 * Creates magical stairs after finishing a quest monster.
 */
static void build_quest_stairs(struct loc grid)
{
	struct loc new_grid = player->grid;

	/* Stagger around */
	while (!square_changeable(cave, grid) &&
		   !square_iswall(cave, grid) &&
		   !square_isdoor(cave, grid)) {
		/* Pick a location */
		scatter(cave, &new_grid, grid, 1, false);

		/* Stagger */
		grid = new_grid;
	}

	/* Push any objects */
	push_object(grid);

	/* Explain the staircase */
	msg("A staircase down is revealed...");

	/* Create stairs down */
	square_set_feat(cave, grid, FEAT_MORE);

	/* Update the visuals */
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);
}

/**
 * Return the quest entered at a given grid in the Town, or NULL if there is none.
 * The quest must be active.
 */
struct quest *get_quest_by_grid(struct loc grid)
{
	for (int i = 0; i < z_info->quest_max; i++) {
		struct quest *q = &player->quests[i];
		if (q->flags & QF_ACTIVE) {
			if ((q->x == grid.x) && (q->y == grid.y)) {
				return q;
			}
		}
	}
	abort();
	return NULL;
}

/* Add a start item to a list */
static void add_item(struct start_item *si, int tval, const char *name, int min, int max)
{
	struct start_item *prev = NULL;
	while (si->max != 0) {
		prev = si;
		si++;
	}
	si->tval = tval;
	si->sval = lookup_sval(tval, name);
	si->min = min;
	si->max = max;
	si->next = NULL;
	if (prev)
		prev->next = si;
}

/**
 * Generate a reward for completing a quest.
 * Passed true if the quest was completed successfully.
 * Make sure overflowing inventory is handled reasonably.
 */
void quest_reward(const struct quest *q, bool success)
{
	const char *n = q->name;
	int au = 0;
	struct start_item si[2];
	memset(si, 0, sizeof(si));

	if (success) {
		if (streq(n, "Rats")) {
			add_item(si, TV_FOOD, "cheese", 6, 9);
			au = 200;
		}
	}

	if (si[0].max) {
		add_start_items(player, si, false, false, ORIGIN_REWARD);
	}
	player->au += au;
}

/**
 * Check if this (now dead) monster is a quest monster, and act appropriately
 */
bool quest_check(const struct monster *m) {
	int i, total = 0;

	/* First check for a quest level */
	if (player->active_quest >= 0) {
		struct quest *q = &player->quests[player->active_quest];
		if (m->race == q->race) {
			if (q->cur_num < q->max_num) {
				/* You've killed a quest target */
				q->cur_num++;
				if (q->cur_num == q->max_num) {
					/* You've killed the last quest target */
					q->flags |= QF_SUCCEEDED;
					msgt(MSG_LEVEL, "Your task is complete!");
				}
			}
		}
	}

	/* Now dealing only with the win quests - so don't bother with non-questors */
	if (!rf_has(m->race->flags, RF_QUESTOR)) return false;

	/* Mark quests as complete */
	for (i = 0; i < z_info->quest_max; i++) {
		if (player->quests[i].flags & QF_ESSENTIAL) {
			/* Note completed quests */
			if (player->quests[i].level == m->race->level) {
				player->quests[i].level = 0;
				player->quests[i].cur_num++;
			}

			/* Count incomplete quests */
			if (player->quests[i].level) total++;
		}
	}

	/* Build magical stairs */
	build_quest_stairs(m->grid);

	/* Nothing left, game over... */
	if (total == 0) {
		player->total_winner = true;
		player->upkeep->redraw |= (PR_TITLE);
		msg("*** CONGRATULATIONS ***");
		msg("You have won the game!");
		msg("You may retire (commit suicide) when you are ready.");
	}

	return true;
}
