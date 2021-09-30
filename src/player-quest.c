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
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-move.h"
#include "mon-spell.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-quest.h"
#include "player-util.h"
#include "store.h"
#include "trap.h"
#include "ui-knowledge.h"
#include "ui-input.h"
#include "ui-store.h"
#include "world.h"

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
	q->quests = 0;
	q->town = q->store = -1;
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

static enum parser_error parse_quest_object(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->target_item = string_make(parser_getstr(p, "object"));
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

static enum parser_error parse_quest_min_found(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->min_found = parser_getuint(p, "min");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_max_remaining(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	q->max_remaining = parser_getuint(p, "max");
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
	if (strstr(in, "guardian"))
		q->flags |= QF_GUARDIAN;
	if (strstr(in, "town"))
		q->flags |= QF_TOWN;
	if (strstr(in, "home"))
		q->flags |= QF_HOME;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_race(struct parser *p) {
	struct quest *q = parser_priv(p);
	const char *name = parser_getstr(p, "race");
	assert(q);

	struct monster_race *race[256];
	int races = lookup_all_monsters(name, race, 256);
	if (!races)
		return PARSE_ERROR_INVALID_MONSTER;
	for(int i=0;i<races;i++) {
		q->race = mem_realloc(q->race, sizeof(q->race[0]) * (1 + q->races));
		q->race[q->races++] = race[i];
		if (!race[i])
			return PARSE_ERROR_INVALID_MONSTER;
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_quest_store(struct parser *p) {
	struct quest *q = parser_priv(p);
	assert(q);

	/* Read a town index, as a town name or the name of another quest */
	const char *location = parser_getsym(p, "town");

	if (!location)
		return PARSE_ERROR_INVALID_VALUE;

	const char *name = NULL;
	if (parser_hasval(p, "store"))
		name = parser_getsym(p, "store");

	q->loc = mem_realloc(q->loc, sizeof(struct quest_location) * (1 + q->quests));
	struct quest_location *ql = q->loc + q->quests;
	q->quests++;
	ql->town = ql->store = -1;
	if (!streq(location, "All"))
		ql->location = string_make(location);
	else
		ql->location = NULL;
	ql->storename = string_make(name);

	return PARSE_ERROR_NONE;
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
	parser_reg(p, "store sym town ?sym store", parse_quest_store);
	parser_reg(p, "object str object", parse_quest_object);
	parser_reg(p, "entrymin uint min", parse_quest_entrymin);
	parser_reg(p, "entrymax uint max", parse_quest_entrymax);
	parser_reg(p, "entryfeature sym feature", parse_quest_entryfeature);
	parser_reg(p, "intro str text", parse_quest_intro);
	parser_reg(p, "desc str text", parse_quest_desc);
	parser_reg(p, "succeed str text", parse_quest_succeed);
	parser_reg(p, "failure str text", parse_quest_failure);
	parser_reg(p, "unlock str text", parse_quest_unlock);
	parser_reg(p, "min-found uint min", parse_quest_min_found);
	parser_reg(p, "max-remaining uint max", parse_quest_max_remaining);
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


/** Complete the current quest, successfully */
static void succeed_quest(struct quest *q) {
	if (!(q->flags & QF_SUCCEEDED))
		msgt(MSG_LEVEL, "Your task is complete!");
	q->flags |= QF_SUCCEEDED;
	q->flags |= QF_UNREWARDED;
	q->flags &= ~QF_FAILED;
}

/** Complete the current quest, successfully and reward it */
static void reward_quest(struct quest *q) {
	if (!(q->flags & QF_SUCCEEDED))
		msgt(MSG_LEVEL, "Your task is complete!");
	q->flags |= QF_SUCCEEDED;
	q->flags &= ~QF_UNREWARDED;
	q->flags &= ~QF_FAILED;
}

/** Make a quest active */
static void activate_quest(struct quest *q) {
	q->flags |= QF_ACTIVE;
	q->flags &= ~QF_SUCCEEDED;
	q->flags &= ~QF_UNREWARDED;
	q->flags &= ~QF_FAILED;
}

/** Complete the current quest, unsuccessfully */
static void fail_quest(struct quest *q) {
	if (!(q->flags & QF_FAILED))
		msgt(MSG_LEVEL, "You have failed in your task!");
	q->flags |= QF_FAILED;
	q->flags &= ~QF_SUCCEEDED;
	q->flags |= QF_UNREWARDED;
}

/** Complete the current quest, successfully */
static void quest_succeed(void) {
	assert(player->active_quest >= 0);
	struct quest *q = &player->quests[player->active_quest];
	succeed_quest(q);
}

struct quest *quest_guardian_any(struct town *town)
{
	for(int i=0;i<z_info->quest_max;i++) {
		if ((player->quests[i].town == town - t_info) && (player->quests[i].store == -1)) {
			return &player->quests[i];
		}
	}
	return NULL;
}

struct quest *quest_guardian_of(struct town *town)
{
	for(int i=0;i<z_info->quest_max;i++) {
		if ((player->quests[i].town == town - t_info) && (player->quests[i].store == -1) &&
			(!(player->quests[i].flags & QF_ACTIVE)) && (!(player->quests[i].flags & QF_SUCCEEDED))) {
			return &player->quests[i];
		}
	}
	return NULL;
}

struct quest *quest_guardian(void)
{
	return quest_guardian_of(player->town);
}

/**
 * Check if the given level is an essential (or at least blocking), incomplete quest level.
 */
bool is_blocking_quest(struct player *p, int level)
{
	size_t i;

	/* Town is never a quest */
	if (!level) return false;

	/* Is this the last level of a dungeon? */
	bool end = ((world_level_exists(NULL, level)) && (!world_level_exists(NULL, level+1)));

	for (i = 0; i < z_info->quest_max; i++)
		if (((p->quests[i].town == (player->town - t_info)) || (p->quests[i].town < 0)) &&
			(((p->quests[i].level == level) && (p->quests[i].flags & QF_ESSENTIAL)) ||
			(end && (p->quests[i].flags & QF_GUARDIAN))) &&
			(!(p->quests[i].flags & QF_SUCCEEDED)))
			return true;

	return false;
}

/**
 * Check if there is a town quest in progress.
 */
bool in_town_quest(void)
{
	/* Must be in town */
	if (player->depth)
		return false;

	/* Must be an active town quest granted from this town */
	for (int i = 0; i < z_info->quest_max; i++) {
		if ((player->quests[i].flags & (QF_TOWN | QF_ACTIVE)) == (QF_TOWN | QF_ACTIVE)) {
			if (player->quests[i].town == (player->town - t_info)) {
				return true;
			}
		}
	}
	return false;
}

/**
 * Check if the given level is a quest level.
 */
bool is_quest(struct player *p, int level)
{
	size_t i;

	/* Town is never a quest */
	if (!level) return false;

	for (i = 0; i < z_info->quest_max; i++)
		if ((p->quests[i].level == level) && (!(p->quests[i].flags & QF_TOWN)))
			return true;

	return false;
}

/**
 * Check if the given level is an active quest level.
 * For quests involving monsters, that means at least one of the targeted
 * monsters is present. For other quests, they are always active if you
 * are on their level.
 */
bool is_active_quest(struct player *p, int level)
{
	size_t i;

	/* Town is never a quest */
	if (!level) return false;

	for (i = 0; i < z_info->quest_max; i++) {
		if ((p->quests[i].level == level) && (!(p->quests[i].flags & QF_TOWN))) {
			if (!p->quests[i].races)
				return true;
			for(int j = 0; j < p->quests[i].races; j++) {
				if (p->quests[i].race[j]->cur_num)
					return true;
			}
		}
	}

	return false;
}

/**
 * Advance the Hit List quest.
 * This is called before asking for a new Hit List quest.
 * Returns true if a new target was found.
 */
static bool get_next_hitlist(struct player *p)
{
	char buf[256];
	struct quest *q = get_quest_by_name("Hit List");

	/* First time? Activate, and skip early levels if you started late  */
	if (q->level == 0) {
		q->level = MAX(0, (player->lev - 10));
		q->flags |= QF_ACTIVE;
	}

	/* May have already completed all Hit List missions */
	if (q->level == z_info->max_depth-1)
		return false;

	/* May still be in progress */
	if ((q->cur_num == 0) && (q->max_num == 1))
		return false;

	/* Look for a unique of greater than the quest's current depth
	 * which is alive and not a questor, special-generation etc.
	 *
	 * Some will randomly be skipped: this is more likely to happen
	 * for levels close to the current one and less likely to happen
	 * when more missions have already been completed - which are
	 * supposed to go some way towards getting a similar number of
	 * hits between games, to not depend too much on how early you
	 * take the first mission, and to cope with the number of uniques
	 * increasing.
	 *
	 * (Should they become lower level / more likely to spawn?)
	 */
	int diff = 0;
	struct monster_race *r = NULL;
	do {
		diff++;
		q->level++;
		for (int i = 0; i < z_info->r_max; i++) {
			if ((r_info[i].level == q->level) &&
				(rf_has(r_info[i].flags, RF_UNIQUE)) &&
				(!rf_has(r_info[i].flags, RF_QUESTOR)) &&
				(!rf_has(r_info[i].flags, RF_SPECIAL_GEN)) &&
				(r_info[i].rarity > 0) &&
				(r_info[i].max_num > 0) &&
				(randint0(diff+1+p->hitlist_wins) > p->hitlist_wins)) {
					r = &r_info[i];
					break;
			}
		}
	} while ((!r) && (q->level < z_info->max_depth));

	/* If r is unset, there are no more eligible targets.
	 * Make the quest inactive.
	 **/
	if (!r) {
		q->flags &= ~(QF_ACTIVE | QF_FAILED | QF_UNREWARDED);
		q->flags |= QF_SUCCEEDED;
		if (q->intro)
			string_free(q->intro);
		q->level = z_info->max_depth-1;
		q->intro = string_make("There are no more contracts available.");
		return false;
	}

	/* Set up the messages for the new quest */
	static const char *kill[] = {
		"Kill", "Exterminate", "Annul", "Inhume", "Obliterate",
		"Blow away", "Rub out", "Erase", "Put down", "Assassinate",
		"Execute", "Remove", "Get rid of", "Gank", "Liquidate",
		"Destroy", "Delete", "Annihilate", "Squish"
	};
	if (q->intro)
		string_free(q->intro);
	strnfmt(buf, sizeof(buf), "%s %s and you will be rewarded.", kill[randint0(sizeof(kill)/sizeof(*kill))], r->name);
	q->intro = string_make(buf);

	/* One target */
	q->race = mem_realloc(q->race, sizeof(q->race[0]));
	q->race[0] = r;
	q->races = 1;
	q->max_num = 1;
	q->cur_num = 0;

	return true;
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
		p->quests[i].target_item = string_make(quests[i].target_item);
		p->quests[i].level = quests[i].level;
		p->quests[i].max_num = quests[i].max_num;
		p->quests[i].flags = quests[i].flags;
		p->quests[i].quests = quests[i].quests;
		p->quests[i].town = quests[i].town;
		p->quests[i].store = quests[i].store;
		p->quests[i].loc = mem_alloc(quests[i].quests * sizeof(struct quest_location));
		memcpy(p->quests[i].loc,  quests[i].loc,  quests[i].quests * sizeof(struct quest_location));
		p->quests[i].unlock = quests[i].unlock;
		p->quests[i].entry_min = quests[i].entry_min;
		p->quests[i].entry_max= quests[i].entry_max;
		p->quests[i].min_found = quests[i].min_found;
		p->quests[i].max_remaining = quests[i].max_remaining;
		p->quests[i].entry_feature = quests[i].entry_feature;
		p->quests[i].races = quests[i].races;
		p->quests[i].race = mem_alloc(quests[i].races * sizeof(p->quests[i].race[0]));
		memcpy(p->quests[i].race, quests[i].race, quests[i].races * sizeof(p->quests[i].race[0]));
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
		string_free(p->quests[i].target_item);
		mem_free(p->quests[i].loc);
		mem_free(p->quests[i].race);
	}
	mem_free(p->quests);
	p->quests = NULL;
}

/**
 * Creates magical stairs after finishing a quest monster.
 */
static void build_quest_stairs(struct player *p, struct loc grid)
{
	struct loc new_grid = p->grid;

	/* Stagger around */
	while (!square_changeable(cave, grid) &&
		   !square_ispassable(cave, grid) &&
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
	p->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);
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
	return NULL;
}

/**
 * Return the quest with a given name.
 */
struct quest *get_quest_by_name(const char *name)
{
	if (!name)
		return NULL;
	for (int i = 0; i < z_info->quest_max; i++) {
		struct quest *q = &player->quests[i];
		if (strstr(name, q->name))
			return q;
	}
	return NULL;
}

/** Add a start item to a list, returns the item used */
static struct start_item *add_item(struct start_item *si, int tval, const char *name, int min, int max)
{
	struct start_item *prev = NULL;
	while (si->max != 0) {
		prev = si;
		si++;
	}
	si->tval = tval;
	si->sval = name ? lookup_sval(tval, name) : SV_UNKNOWN;
	si->min = min;
	si->max = max;
	for(int i=0;i<MAX_EGOS;i++)
		si->ego[i] = NULL;
	si->artifact = NULL;
	si->next = NULL;
	if (prev)
		prev->next = si;
	return si;
}

/** Add an ego start item */
static void add_ego_item(struct start_item *si, int tval, const char *name, const char *ego, int min, int max)
{
	si = add_item(si, tval, name, min, max);
	si->ego[0] = lookup_ego_item(ego, tval, si->sval);
	if (!si->ego[0])
		msg("Warning: unable to find ego item '%s', base '%s', tv/sv %d/%d", ego, name, tval, si->sval);
}

/** Add an artifact start item */
static void add_artifact(struct start_item *si, const char *name)
{
	add_item(si, 0, NULL, 1, 1);
	si->artifact = lookup_artifact_name(name);
	if (!si->artifact)
		msg("Warning: unable to find artifact '%s'", name);
}

/** Drop an item on the floor at the specified place, mark it as a quest item */
static void quest_item_at(struct chunk *c, struct loc xy, struct object *obj)
{
	bool dummy;
	obj->origin = ORIGIN_SPECIAL;
	obj->origin_depth = player->depth;
	of_on(obj->flags, OF_QUEST_SPECIAL);
	floor_carry(c, xy, obj, &dummy);
}

/** Enter a quest level. This is called after the vault is generated.
 * At this point cave may not be set - so use the passed in chunk.
 **/
void quest_enter_level(struct chunk *c)
{
	assert(player->active_quest >= 0);
	struct quest *q = &player->quests[player->active_quest];
	const char *n = q->name;
	s32b value;

	/* Find the entry point */
	struct loc grid;
	for (grid.y = 0; grid.y < c->height; grid.y++) {
		for (grid.x = 0; grid.x < c->width; grid.x++) {
			if (square_isstairs(c, grid))
				break;
		}
		if ((grid.x < c->width) && (square_isstairs(c, grid)))
			break;
	}

	 if (streq(n, "Msing Pills")) {
		/* Traps:
		 * Place a portal at each side first.
		 */
		struct trap_kind *glyph = lookup_trap("portal");
		if (glyph) {
			int tidx = glyph->tidx;
			place_trap(c, loc(1, 9), tidx, 0);
			place_trap(c, loc(c->width-2, 9), tidx, 0);
		}

		/* Items:
		 * Place one piece of fruit next to where you start, and
		 * one of each other type in random clear positions.
		 */

		char *fruit[] = {
			 "apple", "pear", "orange", "satsuma", "banana",
			 "pineapple", "melon", "pepper", "habanero", "choke-apple",
			 "snozzcumber"
		};
		const int nfruit = sizeof(fruit)/sizeof(fruit[0]);

		shuffle_sized(fruit, nfruit, sizeof(fruit[0]));
		for(int i=0;i<nfruit;i++) {
			struct loc xy = loc(13, 14);	// left of the <
			if (i > 0) {
				/* Find a space (avoiding the unreachable center) */
				do {
					xy = loc(randint1(c->width - 1), randint1(c->height - 1));
				} while ((xy.y == 9) || (!square_isempty(c, xy)));
			}
			struct object *obj = make_object_named(c, 1, false, false, false, &value, TV_FOOD, fruit[i]);
			bool dummy;
			obj->origin = ORIGIN_SPECIAL;
			obj->origin_depth = player->depth;
			obj->number = 1;
			floor_carry(c, xy, obj, &dummy);
		}

		/*
		 * Fill every other clear position (outside the central area)
		 * with random non-useless but typically low-level pills - total ~200.
		 */

		for(int x=1;x<c->width;x++) {
			for(int y=1;y<c->height;y++) {
				struct loc xy = loc(x, y);
				if ((y != 9) || (x == 5) || (x == 22)) {
					if (square_isempty(c, xy)) {
						struct object *obj = NULL;
						do {
							if (obj)
								object_delete(cave, player->cave, &obj);
							obj = make_object_named(c, 1, false, false, false, &value, TV_PILL, NULL);
						} while ((!obj) || (value <= 5) || (obj->number > 1));	/* not a nasty or sugar */
						quest_item_at(c, xy, obj);
					}
				}
			}
		}

		/* Monsters:
		 * 4 uniques.
		 * Resurrect centrally when killed? This seems too mean, though.
		 */

		struct monster_group_info info = { 0, 0 };
		place_new_monster(c, loc(1, 1), lookup_monster("Inky"), false, false, info, ORIGIN_DROP);
		place_new_monster(c, loc(c->width-2, 1), lookup_monster("Blinky"), false, false, info, ORIGIN_DROP);
		place_new_monster(c, loc(1, c->height - 2), lookup_monster("Pinky"), false, false, info, ORIGIN_DROP);
		place_new_monster(c, loc(c->width-2, c->height - 2), lookup_monster("Clyde"), false, false, info, ORIGIN_DROP);
	} else if (streq(n, "Whiskey Cave")) {
		/* Scatter the loot distant from you */
		for(int i=0;i<q->min_found * 2;i++) {
			struct loc xy;
			int dist;
			int value;
			struct object *obj = make_object_named(c, 1, false, false, false, &value, TV_FOOD, "bottle of whiskey");
			if (!obj)
				obj = make_object_named(c, 1, false, false, false, &value, TV_FOOD, NULL);
			if (!obj)
				obj = make_object_named(c, 1, false, false, false, &value, 0, NULL);
			assert(obj);
			do {
				xy = loc(randint0(c->width), randint0(c->height));
				dist = (abs(grid.x - xy.x) + abs(grid.y - xy.y));
			} while ((!square_isempty(c, xy)) || (dist < 14));
			quest_item_at(c, xy, obj);
		}

		/* Monsters - fire theme */
		for(int i=0;i<12+randint0(3);i++) {
			struct loc xy;
			int dist;
			struct monster_race *mon;
			/* Try hard to find thematic monsters, but give up if there aren't any */
			do {
				mon = get_mon_num(q->level, q->level);
				/* Thematic means:
				 * 	Immune to fire or plasma, or capable of projecting fire or plasma.
				 */
				if (rf_has(mon->flags, RF_IM_FIRE)) break;
				if (rf_has(mon->flags, RF_IM_PLASMA)) break;
				if (rsf_has(mon->spell_flags, RSF_BR_FIRE)) break;
				if (rsf_has(mon->spell_flags, RSF_BR_PLAS)) break;
				if (rsf_has(mon->spell_flags, RSF_BA_FIRE)) break;
				if (rsf_has(mon->spell_flags, RSF_BO_FIRE)) break;
				if (rsf_has(mon->spell_flags, RSF_BO_PLAS)) break;
			} while (randint0(1000) > 0);
			do {
				xy = loc(randint0(c->width), randint0(c->height));
				dist = (abs(grid.x - xy.x) + abs(grid.y - xy.y));
			} while ((!square_isempty(c, xy)) || (dist < 5));
			struct monster_group_info info = { 0, 0 };
			place_new_monster(c, xy, mon, false, false, info, ORIGIN_DROP);
		}

		/* Traps:
		 * There should be a number of granite, rubble, pit type of traps.
		 */
		struct trap_kind *traps[5];
		traps[0] = lookup_trap("pit");
		traps[1] = lookup_trap("fire trap");
		traps[2] = traps[3] = lookup_trap("rock fall trap");
		traps[4] = lookup_trap("earthquake trap");
		for(int i=0;i<5+randint0(3);i++) {
			struct loc xy;
			int dist;
			do {
				xy = loc(randint0(c->width), randint0(c->height));
				dist = (abs(grid.x - xy.x) + abs(grid.y - xy.y));
			} while ((!square_isempty(c, xy)) || (dist < 7));
			struct trap_kind *glyph = traps[randint0(5)];
			if (glyph) {
				int tidx = glyph->tidx;
				place_trap(c, loc(1, 9), tidx, 0);
				place_trap(c, loc(c->width-2, 9), tidx, 0);
			}
		}
	}
}

static struct object *has_special_flag(struct object *obj, void *data)
{
	return (of_has(obj->flags, OF_QUEST_SPECIAL)) ? obj : NULL;
}

static struct object *obj_of_kind(struct object *obj, void *data)
{
	struct object_kind *k = (struct object_kind *)data;
	if (obj->kind == k)
		return obj;
	return NULL;
}

/**
 * Remove the QUEST_SPECIAL flag on all items - they are now ordinary items which can e.g. be sold.
 * Must at least check your home, your gear and the level.
 * This usually happens when you fail a quest - but some quests might allow you to keep the stuff when you succeed as well.
 */
static void quest_remove_flags(void)
{
	struct object *obj;
	do {
		obj = find_object(has_special_flag, NULL);
		if (obj)
			of_off(obj->flags, OF_QUEST_SPECIAL);
	} while (obj);
}

/** Delete all items with the QUEST_SPECIAL flag.
 * Must at least check your home, your gear and the level.
 * This usually happens when you succeed.
 */
static void quest_remove_specials(void)
{
	struct object *obj;
	do {
		obj = find_object(has_special_flag, NULL);
		if (obj)
			remove_object(obj);
	} while (obj);
}

/** Count all items with the QUEST_SPECIAL flag.
 */
static int quest_count_specials(void)
{
	struct object *obj;
	int count = 0;
	do {
		obj = find_object(has_special_flag, NULL);
		if (obj)
			count++;
	} while (obj);
	return count;
}

/** Count all items of a given kind
 */
static int quest_count_kind(struct object_kind *k)
{
	struct object *obj;
	int count = 0;
	do {
		obj = find_object(obj_of_kind, k);
		if (obj)
			count++;
	} while (obj);
	return count;
}

/** Returns a (static) table of the count of all items of a given kind
 */
static int *quest_locate_kind(struct object_kind *k)
{
	struct object *obj;
	static int table[MAX_LOCATION];
	struct location location;
	memset(table, 0, sizeof(table));
	do {
		obj = locate_object(obj_of_kind, k, &location);
		if (obj)
			table[location.type]++;
	} while (obj);
	return table;
}

/** Delete all items of a given kind
 */
static void quest_remove_kind(struct object_kind *k)
{
	struct object *obj;
	do {
		obj = find_object(obj_of_kind, k);
		if (obj)
			remove_object(obj);
	} while (obj);
}

/** Special cases for quests when you change levels
 * Called before level gen occurs
 */
void quest_changing_level(void)
{
	struct player *p = player;

	/* Handle returning to the town from a quest level */
	if ((p->active_quest >= 0) && (!player->depth)) {
		struct quest *quest = &player->quests[player->active_quest];

		/* Home quests are different - you can't fail them permanently, you can try them again as many times as you want */
		if (quest->flags & QF_HOME) {
			if (!(quest->flags & QF_SUCCEEDED)) {
				quest->flags |= QF_FAILED;
			} else {
				quest->flags &= ~QF_ACTIVE;
			}
		} else {
			/* Not a home quest */

			/* Fail, or reward */
			if (!(quest->flags & QF_SUCCEEDED)) {
				quest->flags |= QF_FAILED;
				quest->flags |= QF_UNREWARDED;
			} else {
				quest->flags |= QF_UNREWARDED;
			}

			/* No longer active */
			quest->flags &= ~QF_ACTIVE;
		}
		/* Not generating or in a quest any more */
		p->active_quest = -1;
	}

	/* Guardian and win-quests: find the quest and unlock it.
	 * This is similar to do_cmd_go_down(), without the messages and is to catch cases
	 * where you enter the quest without descending the stair from the town (such as a
	 * descent card, or debug options)
	 **/
	else if (player->depth) {
		struct quest *quest = quest_guardian();
		if (quest)
			quest->flags |= QF_ACTIVE;
	}
}

/** Special cases for quests when you change levels
 * Called after level gen is complete (and the old level has been discarded)
 * This is also called after reloading a level.
 */
void quest_changed_level(bool store)
{
	/* A free position for a monster, if one is generated */
	struct loc xy = loc(0, 0);
	struct monster_group_info info = { 0, 0 };
	/* Find a space */
	for(int i=0;i<10000;i++) {
		struct loc try_xy = loc(randint1(cave->width - 1), randint1(cave->height - 1));
		if (square_isempty(cave, try_xy)) {
			xy = try_xy;
			break;
		}
	}
	bool guardian = ((is_blocking_quest(player, player->depth)) && (player->active_quest < 0));
	bool fortress = (player->town == t_info);

	/* Quest specific checks */
	for(int i=0;i<z_info->quest_max;i++) {
		struct quest *q = player->quests + i;
		if (q->flags & QF_ACTIVE) {
			if ((q->flags & QF_TOWN) && (in_town_quest()) && (q->races) && (!(q->flags & (QF_SUCCEEDED|QF_FAILED|QF_UNREWARDED)))) {
				int cur_num = 0;
				for(int j=0; j<q->races; j++)
					cur_num += q->race[j]->cur_num;
				if (cur_num == 0) {
					int max_num = q->max_num;
					for(int j=1; j<q->races; j++)
						max_num -= q->race[j]->cur_num;
					while (q->race[0]->cur_num < max_num) {
						struct loc try_xy = loc(randint1(cave->width - 1), randint1(cave->height - 1));
						if (!mon_race_hates_grid(cave, q->race[0], try_xy)) {
							place_new_monster(cave, try_xy, q->race[0], false, true, info, ORIGIN_DROP);
						}
					}
				}
				cur_num = 0;
				for(int j=0; j<q->races; j++)
					cur_num += q->race[j]->cur_num;
				/* Fail the quest if the target has disappeared */
				if ((!store) && (cur_num == 0) && (!(q->flags & (QF_SUCCEEDED)))) {
					fail_quest(q);
				}
			} else if (strstr(q->name, "Pie")) {
				if (q->flags & QF_SUCCEEDED) {
					/* Pie quest: if the card ever disappears unrecoverably, then you have failed */
					struct object_kind *kind = lookup_kind(TV_CARD, lookup_sval(TV_CARD, "security"));
					int count = quest_count_kind(kind);
					if (!count) {
						fail_quest(q);
					}
				}
			} else {
				/* Create quest monsters, if possible.
				 * It must be the right level, and the right dungeon.
				 */
				if (xy.x) {
					if (streq(q->name, "Slick") || streq(q->name, "The Dark Helmet") || streq(q->name, "Primordial Grue") || streq(q->name, "Icky Sticky Dinosaur")) {
						if (guardian && ((player->town - t_info) == q->town)) {
							place_new_monster(cave, xy, lookup_monster(q->name), false, true, info, ORIGIN_DROP);
						}
					} else if (streq(q->name, "Impy")) {
						if (fortress && (player->depth == q->level)) {
							place_new_monster(cave, xy, lookup_monster(q->name), false, true, info, ORIGIN_DROP);
						}
					} else if (streq(q->name, "Holo-Triax") || streq(q->name, "Mecha-Triax")) {
						if (fortress && (player->depth == q->level)) {
							place_new_monster(cave, xy, lookup_monster(q->name), false, false, info, ORIGIN_DROP);
						}
					} else if (streq(q->name, "The Core")) {
						if (player->depth == q->level) {
							place_new_monster(cave, xy, lookup_monster(q->name), false, false, info, ORIGIN_DROP);
						}
					} else if (streq(q->name, "Miniac")) {
						if (guardian && ((player->town - t_info) == q->town)) {
							place_new_monster(cave, xy, lookup_monster("Miniac, the Crusher"), false, false, info, ORIGIN_DROP);
						}
					} else if (streq(q->name, "Triax")) {
						if (fortress && (player->depth == q->level)) {
							place_new_monster(cave, xy, lookup_monster("Triax, the Emperor"), false, false, info, ORIGIN_DROP);
						}
					}
				}
			}
		}
	}
}

/** Check for special quest behaviour when selling an object to the store.
 * Returns true if handled, false if the normal selling dialog should continue.
 */
bool quest_selling_object(struct object *obj, struct store_context *ctx)
{
	struct store *store = ctx->store;

	/* Selling the card to the BM triggers an alternate completion */
	struct object_kind *security = lookup_kind(TV_CARD, lookup_sval(TV_CARD, "security"));
	if (obj->kind == security) {
		if (store->sidx == STORE_B_MARKET) {
			store_long_text(ctx, "So you found her dead? Unfortunate, but at least we got the card back "
									"and that is what really matters. You'll get the same $8000 reward she "
									"would have had, and get to see some of the goods that most customers "
									"don't. And we may have more for you to do, if you really want to take "
									"her place.");
			fail_quest(get_quest_by_name("Soldier, Sailor, Chef, Pie"));
			player->au += 8000;
			player->bm_faction++;
			for (int j = 0; j < 10; j++)
				store_maint(store);
			remove_object(obj);
			return true;
		}
	}

	return false;
}

/** Asked for a quest at a store, but none of the usual cases match.
 * This handles the special cases (such as completing with an alternative ending at a different store, as in "Pie").
 * It returns true if it has handled it, false for the generic no-quest message.
 */
bool quest_special_endings(struct store_context *ctx)
{
	struct store *store = ctx->store;
	/* Pie quest: if you take the card to the BM */
	if (store->sidx == STORE_B_MARKET) {
		struct object_kind *kind = lookup_kind(TV_CARD, lookup_sval(TV_CARD, "security"));
		struct object *obj = find_object(obj_of_kind, kind);
		if (obj) {
			while (find_object(obj_of_kind, kind)) {};
			int *locs = quest_locate_kind(kind);
			if (locs[LOCATION_PLAYER] > 0) {
				screen_save();
				int response = store_get_long_check(ctx, "So you have a certain card with you that we were expecting our Ky "
															"to bring back. If you could tell me how you ended up with it and "
															"return it to us, you'll be paid appropriately...");
				screen_load();
				if (response) {
					// accepted
					quest_selling_object(obj, ctx);
					return true;
				}
			}
		}
	}
	return false;
}

/** Returns true if the quest can have the reward given.
 * This implies QF_SUCCESS | QF_UNREWARDED, and for many quests that is all that is needed.
 * However, there are cases where additional checks are needed - for example, that you have
 * not just obtained the McGuffin but have it with you now.
 */
bool quest_is_rewardable(const struct quest *q)
{
	/* Must be successful and unrewarded */
	if ((q->flags & (QF_SUCCEEDED | QF_UNREWARDED)) != (QF_SUCCEEDED | QF_UNREWARDED))
		return false;

	/* Special cases */

	/* You must have the card with you */
	if (strstr(q->name, "Pie")) {
		struct object_kind *security = lookup_kind(TV_CARD, lookup_sval(TV_CARD, "security"));
		int *locs = quest_locate_kind(security);
		if (locs[LOCATION_PLAYER] != 1) {
			msg("Great news that you found it, but you'll to need to fetch it back.");
			return false;
		} else {
			quest_remove_kind(security);
		}
	}

	/* Default to OK */
	return true;
}

/* Return true if successful */
static bool quest_level_reward(int lev)
{
	/* Hit List rewards are based on level. */
	struct object *obj = NULL;
	int value;
	int minvalue = (lev*lev*2)+(lev*20)+200;
	int maxvalue = ((lev+1)*(lev+1)*2)+((lev+1)*20)+250;
	int reps = 0;

	/* Iterate until an item of the right value range is found */
	do {
		reps++;
		if (obj)
			object_delete(cave, player->cave, &obj);
		value = 0;
		obj = make_object_named(cave, lev, true, false, false, &value, 0, NULL);
		if (reps > 1000)
			maxvalue++;
		if ((reps > 2000) && (minvalue > 1))
			minvalue--;
	} while ((reps < 10000) && ((!obj) || (value < minvalue) || (value > maxvalue)));

	/* Initialize it and give it to you */
	if (obj) {
		obj->origin = ORIGIN_REWARD;
		obj->known = object_new();
		object_set_base_known(player, obj);
		object_flavor_aware(player, obj);
		obj->known->pval = obj->pval;
		obj->known->effect = obj->effect;
		obj->known->notice |= OBJ_NOTICE_ASSESSED;
		inven_carry(player, obj, true, false);
		int icon;
		do {
			icon = object_find_unknown_icon(player, obj);
			if (icon >= 0)
				player_learn_icon(player, icon, false);
		} while (icon >= 0);
		update_player_object_knowledge(player);
		obj->kind->everseen = true;
		return true;
	}
	return false;
}

/**
 * Generate a reward for completing a quest.
 * Passed true if the quest was completed successfully.
 * Make sure overflowing inventory is handled reasonably.
 */
void quest_reward(struct quest *q, bool success, struct store_context *ctx)
{
	const char *n = q->name;
	int au = 0;
	struct start_item si[4];
	memset(si, 0, sizeof(si));

	if (success) {
		if (streq(n, "Rats")) {
			add_item(si, TV_FOOD, "cheese", 6, 9);
			au = 200;
		} else if (streq(n, "Msing Pills")) {
			add_item(si, TV_PILL, "augmentation", 2, 2);
		} else if (strstr(n, "Pie")) {
			add_item(si, TV_FOOD, "Hunter's pie", 12, 12);
			add_item(si, TV_MUSHROOM, "clarity", 5, 5);
			add_item(si, TV_MUSHROOM, "emergency", 7, 7);
		} else if (streq(n, "Whiskey Cave")) {
			int specials = quest_count_specials();
			au = 200 * specials;
			if (specials >= ((3 * q->min_found) / 2)) {
				char buf[256];
				add_ego_item(si, (specials == (q->min_found * 2)) ? TV_HARD_ARMOR : TV_SOFT_ARMOR, NULL, "(fireproof)", 1, 1);
				strnfmt(buf, sizeof(buf), "%s Since you went beyond what I was expecting in bringing %d bottles back, I have also found some protective gear that you might like.", q->succeed);
				char *msg = string_make(buf);
				string_free(q->succeed);
				q->succeed = msg;
			}
		} else if (streq(n, "Hunter, in Darkness")) {
			/* The boom-stick!
			 * This is a bit wimpy. Maybe it should be a randart?  adding some ammo.
			 **/
			add_ego_item(si, TV_GUN, "12mm rifle", "(automatic)", 1, 1);
			add_ego_item(si, TV_AMMO_12, "12mm bullet", "(guided)", 32, 39);
			if (player->bm_faction < 0)
				player->bm_faction = 0;
		} else if (streq(n, "Day of the Triffids")) {
			int tval = TV_AMMO_12;
			struct object *gun = slot_object(player, slot_by_name(player, "shooting"));
			if (gun)
				tval = gun->kind->tval;
			const char *name = NULL;
			const char *ego = "(rock salt)";
			int qty = 1;
			switch(tval) {
				case TV_AMMO_6:
					qty = 90;
					name = "6mm bullet";
					break;
				case TV_AMMO_9:
					qty = 70;
					name = "9mm bullet";
					break;
				default:
					tval = TV_AMMO_12;
					/* fall through */
				case TV_AMMO_12:
					qty = 55;
					name = "12mm bullet";
			}
			add_ego_item(si, tval, name, ego, qty, qty+5);
		} else if (streq(n, "Swimming with Meg")) {
			add_artifact(si, "sharkproof swimsuit");
		} else if (streq(n, "Hit List")) {
			if (!quest_level_reward(q->level)) {
				au = store_roundup((q->level*q->level*2)+(q->level*20)+200);
				msg("Someone nicked your reward! You'll have to accept cash, $%d.", au);
			}
			int old_faction = player->bm_faction;
			player->hitlist_wins++;
			player->bm_faction = MIN(player->bm_faction+1, (player->hitlist_wins+1) / 2);
			if (old_faction != player->bm_faction) {
				msg("You are now trusted enough to see more stuff at better prices.");
				for (int j = 0; j < 10; j++)
					store_maint(ctx->store);
			}
			/* Open it again */
			q->flags &= ~(QF_SUCCEEDED | QF_FAILED | QF_UNREWARDED | QF_ACTIVE);
			player->au += au;
			return;
		}
		quest_remove_specials();
	} else {
		quest_remove_flags();
	}
	if (streq(n, "Hunter, in Darkness")) {
		/* The traditional 3 "broken arrows" */
		add_ego_item(si, TV_AMMO_12, "12mm bullet", "(damaged)", 3, 3);
		player->bm_faction++;
	}

	if (si[0].max) {
		add_start_items(player, si, false, false, ORIGIN_REWARD);
	}

	q->flags &= ~(QF_UNREWARDED | QF_ACTIVE);
	if (!(q->flags & QF_SUCCEEDED))
		q->flags |= QF_FAILED;
	player->au += au;
}

/** Return the quest intro text.
 * This is commonly q->intro, but some change it depending on conditions such as faction.
 */
const char *quest_get_intro(const struct quest *q) {
	if (streq(q->name, "Hunter, in Darkness")) {
		if (player->bm_faction > 0)
			return "No, you still aren't seeing the best stuff. That's not for everyone. Go kill something big to show us you aren't all talk. There's a wumpus in the bat cave. That would do nicely.";
	} else if (streq(q->name, "Hit List")) {
		get_next_hitlist(player);
	}
	return q->intro;
}


/** Return true if the item is a target of the quest */
static bool item_is_target(const struct quest *q, const struct object *obj) {
	char oname[64];
	object_desc(oname, sizeof(oname), obj, ODESC_SPOIL | ODESC_BASE, player);
	if ((!q) || (!obj))
		return false;
	if (!q->target_item)
		return false;
	if (!of_has(obj->flags, OF_QUEST_SPECIAL))
		return false;
	return (my_stristr(oname, q->target_item) != NULL);
}

/**
 * You picked up an item.
 * Check if that completes your quest.
 */
bool quest_item_check(const struct object *obj) {
	/* If you aren't in a quest, or it doesn't have a quest item,
	 * or the item picked up is not a quest item, bail out early.
	 **/
	if (player->active_quest < 0)
		return false;
	struct quest *q = &player->quests[player->active_quest];
	if (!item_is_target(q, obj))
		return false;

	/* Find the number of targets carried */
	int gear_items = 0;
	struct object *gear_obj = player->gear;
	while (gear_obj) {
		if (item_is_target(q, gear_obj))
			gear_items += gear_obj->number;
		gear_obj = gear_obj->next;
	};

	/* And the number on the level (including monster held) */
	int level_items = 0;
	for (int y = 1; y < cave->height; y++) {
		for (int x = 1; x < cave->width; x++) {
			struct loc grid = loc(x, y);
			for (struct object *obj = square_object(cave, grid); obj; obj = obj->next) {
				if (item_is_target(q, obj))
					level_items += obj->number;
			}
		}
	}

	/* Does this meet the quest conditions?
	 * Possible conditions include
	 * 'at least X' (where X is commonly 1)
	 * 'all still remaining' (i.e. destroyed items are OK)
	 * 'all that were there at the start' (so not OK)
	 *
	 * There is also 'quest flag' vs item name XXX
	 *
	 * What if you un-complete a quest? (by destroying items, or leaving them behind?)
	 * A quest might complete immediately, or only when leaving the quest level, or only when
	 * visiting the questgiver - add flags.
	 *
	 * Might also be possible to *fail* a quest - by destroying the wrong object, or killing the wrong monster...
	 *
	 * Combinations of these ("all still remaining, but at least 2")
	 *
	 * A "minimum found" and "maximum remaining" is useful, though.
	 **/
	if (gear_items >= q->min_found) {
		if (level_items <= q->max_remaining) {
			quest_succeed();
			return true;
		}
	}

	return false;
}

/* Check if entry to a building should be blocked by a quest.
 * Returns true if entry should be blocked.
 *
 * This checks by looking for a home-quest (HOME flag set) with matching town and store.
 * 		If so, and it's inactive and not complete: Make it active, and ask whether to enter.
 * 			If so, enter a quest (same as other quests entered from the town).
 * 			If not, go back to the town.
 * 		Either way, return true as you aren't entering the building.
 * If there is none, return false and enter the building normally.
 */
bool quest_enter_building(struct store *store) {
	for(int i=0;i<z_info->quest_max;i++) {
		if ((player->quests[i].flags & QF_HOME) && (!(player->quests[i].flags & QF_SUCCEEDED)) &&
			(player->quests[i].town == player->town - t_info) && (player->quests[i].store == (int)store->sidx)) {
			player->quests[i].flags |= QF_ACTIVE;
			screen_save();
			Term_clear();

			/* Prepare hooks */
			text_out_hook = text_out_to_screen;
			text_out_indent = 1;
			Term_gotoxy(0, 0);

			/* Print it */
			text_out(player->quests[i].desc);

			/* Level */
			text_out_c(COLOUR_YELLOW, " (Level %d)", player->quests[i].level);

			text_out_c(COLOUR_L_BLUE, " [yn]");
			text_out_indent = 0;

			/* Get an answer */
			struct keypress ch = inkey();

			bool response = true;
			if ((ch.code == ESCAPE) || (strchr("Nn", ch.code)))
				response = false;

			screen_load();
			if (response) {
				/* accepted */
				player->active_quest = i;

				/* set pos */
				player->quests[i].x = player->grid.x;
				player->quests[i].y = player->grid.y;

				/* Hack -- take a turn */
				player->upkeep->energy_use = z_info->move_energy;

				/* Create a way back */
				player->upkeep->create_up_stair = true;
				player->upkeep->create_down_stair = false;

				/* Change level */
				dungeon_change_level(player, player->quests[i].level);
			}
			return true;
		}
	}
	return false;
}

/* Check for quest targets killed */
static bool check_quest(struct quest *q, const struct monster *m) {
	if (q->flags & QF_HOME) {
		/* All home quests require the level to be free of all monsters */
		if (cave->mon_cnt <= 1) {
			/* The last one */
			reward_quest(q);
		}
	} else {
		bool wanted = false;
		int i=0;
		for(i=0; i<q->races; i++) {
			if (m->race == q->race[i]) {
				wanted = true;
				break;
			}
		}
		if (wanted) {
			if (q->cur_num < q->max_num) {
				/* You've killed a quest target */
				q->cur_num++;
				if (q->cur_num == q->max_num) {
					/* You've killed the last quest target */
					succeed_quest(q);
					if (streq(q->race[i]->name, "triffid")) {
						msg("That's the last of them. Maybe you should go back and ask for a reward?");
						player->town_faction++;
					} else if (streq(q->race[i]->name, "megalodon")) {
						msg("How did anyone not see that! Maybe you should go back for your share of the winnings?");
					}
					return true;
				}
			}
		}
	}
	return false;
}

/**
 * Check if this (now dead) monster is a quest monster, and act appropriately
 */
bool quest_check(struct player *p, const struct monster *m)
{
	int i, total = 0;
	/* First check for a quest level */
	if (player->active_quest >= 0)
		if (check_quest(&player->quests[player->active_quest], m))
			return true;

	/* Check for in-town quests */
	if (in_town_quest()) {
		for(int i=0;i<z_info->quest_max;i++) {
			/* Matching this town, active and a town quest */
			if (player->quests[i].town == player->town - t_info) {
				if ((player->quests[i].flags & (QF_TOWN | QF_ACTIVE)) == (QF_TOWN | QF_ACTIVE)) {
					if (check_quest(&player->quests[i], m))
						return true;
				}
			}
		}
	}

	/* Then check for monsters found outside special levels that affect quests.
	 * These don't necessarily have the RF_QUESTOR flag (consider a 'random nonunique' quest, or 'Hit List')
	 **/
	bool questor = rf_has(m->race->flags, RF_QUESTOR);
	if (questor) {
		if (streq(m->race->name, "Ky, the Pie Spy")) {
			succeed_quest(get_quest_by_name("Soldier, Sailor, Chef, Pie"));
			return true;
		} else if (streq(m->race->name, "Slick")) {
			reward_quest(get_quest_by_name("Slick"));
			/* Reward = some items dropped, townee faction, reduce danger (and an appropriate message) */
			if (player->town_faction < 3)
				player->town_faction++;
			if (player->danger < 15)
				player->danger_reduction += (19 - player->danger) / 5;
			msg("The town's a safer place with Slick's mob out of action.");
			return true;
		} else if (streq(m->race->name, "Miniac, the Crusher")) {
			reward_quest(get_quest_by_name("Miniac"));
			/* Reward = some items dropped, and a message */
			msg("The mine's no longer such a death trap now that the rogue robot has been scrapped.");
			return true;
		} else if (streq(m->race->name, "The Dark Helmet")) {
			reward_quest(get_quest_by_name("The Dark Helmet"));
			/* Reward = some items dropped, townee faction, reduce danger, and a message */
			player->town_faction++;
			if (player->danger < 30)
				player->danger_reduction += (29 - player->danger) / 5;
			msg("Without the Dark Helmet, his goon squads aren't much of a threat.");
			return true;
		} else if (streq(m->race->name, "Primordial Grue")) {
			reward_quest(get_quest_by_name("Primordial Grue"));
			/* Reward = some items dropped, and a message */
			msg("The unnatural darkness seems to be just an absence of light now.");
			return true;
		} else if (streq(m->race->name, "Icky Sticky Dinosaur")) {
			reward_quest(get_quest_by_name("Icky Sticky Dinosaur"));
			/* Reward = some items dropped, and a message */
			msg("The gel liquefies and drains away. That thing's not coming back!");
			return true;
		} else {
			struct quest *q = get_quest_by_name(m->race->name);
			/* Currently these are all Fortress level-guardian quests, so there is no separate reward.
			 * This also implies that they are contiguous.
			 */
			if (q) {
				reward_quest(q);
				if (q != (player->quests + (z_info->quest_max - 1))) {
					q++;
					activate_quest(q);
				}
			}
		}
	} else if ((get_quest_by_name("Hit List")->races) && (m->race == get_quest_by_name("Hit List")->race[0])) {
		succeed_quest(get_quest_by_name("Hit List"));
		msg("Target eliminated.");
		get_quest_by_name("Hit List")->cur_num = 1;
		return true;
	}

	/* Now dealing only with the win quests - so don't bother with non-questors */
	if (!questor) return false;

	/* Mark quests as complete */
	for (i = 0; i < z_info->quest_max; i++) {
		if (p->quests[i].flags & QF_ESSENTIAL) {
			/* Note completed quests */
			if (p->quests[i].level == m->race->level) {
				p->quests[i].level = 0;
				p->quests[i].cur_num++;
			}

			/* Count incomplete quests */
			if (player->quests[i].level) total++;
		}
	}

	/* Build magical stairs */
	build_quest_stairs(p, m->grid);

	/* Nothing left, game over... */
	if (total == 0) {
		p->total_winner = true;
		p->upkeep->redraw |= (PR_TITLE);
		msg("*** CONGRATULATIONS ***");
		msg("You have won the game!");
		msg("You may retire (Ctrl-C, \"commit suicide\") when you are ready.");
	}

	return true;
}
