/**
 * \file obj-chest.c
 * \brief Encapsulation of chest-related functions
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2012 Peter Denison
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
#include "cave.h"
#include "effects.h"
#include "init.h"
#include "mon-lore.h"
#include "obj-chest.h"
#include "obj-desc.h"
#include "obj-ignore.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"

/**
 * Chest traps are specified in the file chest_trap.txt.
 *
 * Chests are described by their 32-bit pval as follows:
 * - pval of 0 is an empty chest
 * - pval of 1 is a locked chest with no traps
 * - pval > 1  is a trapped chest, with each bit of the pval aside from the
 *             lowest and highest (potentially) representing a different trap
 * - pval < 1  is a disarmed/unlocked chest; the disarming process is simply
 *             to negate the pval
 *
 * The chest pval also determines the difficulty of disarming the chest.
 * Currently the maximum difficulty is 60 (32 + 16 + 8 + 4); if more traps are
 * added to chest_trap.txt, the disarming calculation will need adjusting.
 */

struct chest_trap *chest_traps;

/**
 * ------------------------------------------------------------------------
 * Parsing functions for chest_trap.txt and chest.txt
 * ------------------------------------------------------------------------ */
static enum parser_error parse_chest_trap_name(struct parser *p)
{
    const char *name = parser_getstr(p, "name");
    struct chest_trap *h = parser_priv(p);
    struct chest_trap *t = mem_zalloc(sizeof *t);

	/* Order the traps correctly and set the pval */
	if (h) {
		h->next = t;
		t->pval = h->pval * 2;
	} else {
		chest_traps = t;
		t->pval = 1;
	}
    t->name = string_make(name);
    parser_setpriv(p, t);
    return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_trap_code(struct parser *p)
{
    const char *code = parser_getstr(p, "code");
    struct chest_trap *t = parser_priv(p);

    if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
    t->code = string_make(code);
    return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_trap_level(struct parser *p)
{
    struct chest_trap *t = parser_priv(p);

    if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
    t->level = parser_getint(p, "level");
    return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_trap_effect(struct parser *p) {
    struct chest_trap *t = parser_priv(p);
	struct effect *effect;
	struct effect *new_effect = mem_zalloc(sizeof(*new_effect));

	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	/* Go to the next vacant effect and set it to the new one  */
	if (t->effect) {
		effect = t->effect;
		while (effect->next)
			effect = effect->next;
		effect->next = new_effect;
	} else
		t->effect = new_effect;

	/* Fill in the detail */
	return grab_effect_data(p, new_effect);
}

static enum parser_error parse_chest_trap_dice(struct parser *p) {
	struct chest_trap *t = parser_priv(p);
	dice_t *dice = NULL;
	struct effect *effect = t->effect;
	const char *string = NULL;

	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	/* If there is no effect, assume that this is human and not parser error. */
	if (effect == NULL)
		return PARSE_ERROR_NONE;

	while (effect->next) effect = effect->next;

	dice = dice_new();

	if (dice == NULL)
		return PARSE_ERROR_INVALID_DICE;

	string = parser_getstr(p, "dice");

	if (dice_parse_string(dice, string)) {
		effect->dice = dice;
	}
	else {
		dice_free(dice);
		return PARSE_ERROR_INVALID_DICE;
	}

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_trap_expr(struct parser *p) {
	struct chest_trap *t = parser_priv(p);
	struct effect *effect = t->effect;
	expression_t *expression = NULL;
	expression_base_value_f function = NULL;
	const char *name;
	const char *base;
	const char *expr;

	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	/* If there is no effect, assume that this is human and not parser error. */
	if (effect == NULL)
		return PARSE_ERROR_NONE;

	while (effect->next) effect = effect->next;

	/* If there are no dice, assume that this is human and not parser error. */
	if (effect->dice == NULL)
		return PARSE_ERROR_NONE;

	name = parser_getsym(p, "name");
	base = parser_getsym(p, "base");
	expr = parser_getstr(p, "expr");
	expression = expression_new();

	if (expression == NULL)
		return PARSE_ERROR_INVALID_EXPRESSION;

	function = spell_value_base_by_name(base);
	expression_set_base_value(expression, function);

	if (expression_add_operations_string(expression, expr) < 0)
		return PARSE_ERROR_BAD_EXPRESSION_STRING;

	if (dice_bind_expression(effect->dice, name, expression) < 0)
		return PARSE_ERROR_UNBOUND_EXPRESSION;

	/* The dice object makes a deep copy of the expression, so we can free it */
	expression_free(expression);

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_trap_destroy(struct parser *p) {
    struct chest_trap *t = parser_priv(p);
	int val = 0;

	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
    val = parser_getint(p, "val");
	if (val) {
		t->destroy = true;
	}
    return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_trap_magic(struct parser *p) {
    struct chest_trap *t = parser_priv(p);
	int val = 0;

	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
    val = parser_getint(p, "val");
	if (val) {
		t->magic = true;
	}
    return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_trap_msg(struct parser *p) {
    struct chest_trap *t = parser_priv(p);

	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
    t->msg = string_append(t->msg, parser_getstr(p, "text"));
    return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_trap_msg_death(struct parser *p) {
    struct chest_trap *t = parser_priv(p);

	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
    t->msg_death = string_append(t->msg_death, parser_getstr(p, "text"));
    return PARSE_ERROR_NONE;
}

struct parser *init_parse_chest_trap(void) {
    struct parser *p = parser_new();
    parser_setpriv(p, NULL);
    parser_reg(p, "name str name", parse_chest_trap_name);
    parser_reg(p, "code str code", parse_chest_trap_code);
    parser_reg(p, "level int level", parse_chest_trap_level);
	parser_reg(p, "effect sym eff ?sym type ?int radius ?int other", parse_chest_trap_effect);
	parser_reg(p, "dice str dice", parse_chest_trap_dice);
	parser_reg(p, "expr sym name sym base str expr", parse_chest_trap_expr);
	parser_reg(p, "destroy int val", parse_chest_trap_destroy);
	parser_reg(p, "magic int val", parse_chest_trap_magic);
	parser_reg(p, "msg str text", parse_chest_trap_msg);
	parser_reg(p, "msg-death str text", parse_chest_trap_msg_death);
    return p;
}

static errr run_parse_chest_trap(struct parser *p) {
    return parse_file_quit_not_found(p, "chest_trap");
}

static errr finish_parse_chest_trap(struct parser *p) {
	parser_destroy(p);
	return 0;
}

static void cleanup_chest_trap(void)
{
	struct chest_trap *trap = chest_traps;
	while (trap) {
		struct chest_trap *old = trap;
		string_free(trap->name);
		string_free(trap->code);
		string_free(trap->msg);
		string_free(trap->msg_death);
		free_effect(trap->effect);
		trap = trap->next;
		mem_free(old);
	}
}

struct file_parser chest_trap_parser = {
    "chest_trap",
    init_parse_chest_trap,
    run_parse_chest_trap,
    finish_parse_chest_trap,
    cleanup_chest_trap
};

/** Chest theme parser */

struct chest_theme {
	char *name;
	struct poss_item *poss_items;
	int rarity;
	int level;
	int minlevel;
	int maxlevel;
	int change;
};

static struct chest_theme *chest_theme;
static int n_chest_themes;

static enum parser_error parse_chest_name(struct parser *p)
{
    const char *name = parser_getstr(p, "name");
    chest_theme = mem_realloc(chest_theme, sizeof(struct chest_theme) * ((n_chest_themes)+1));
	struct chest_theme *t = chest_theme + n_chest_themes;
	memset(t, 0, sizeof(struct chest_theme));
	n_chest_themes++;
	t->maxlevel = 100;
    t->name = string_make(name);
    parser_setpriv(p, t);
    return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_level(struct parser *p) {
	struct chest_theme *t = parser_priv(p);

    if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	t->level = parser_getint(p, "level");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_minlevel(struct parser *p) {
	struct chest_theme *t = parser_priv(p);

    if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	t->minlevel = parser_getint(p, "minlevel");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_maxlevel(struct parser *p) {
	struct chest_theme *t = parser_priv(p);

    if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	t->maxlevel = parser_getint(p, "maxlevel");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_rarity(struct parser *p) {
	struct chest_theme *t = parser_priv(p);

    if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	t->rarity = parser_getint(p, "rarity");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_switch(struct parser *p) {
	struct chest_theme *t = parser_priv(p);

    if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	t->change = parser_getint(p, "switch");
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_type(struct parser *p) {
	bool found_one_kind = false;

	struct chest_theme *t = parser_priv(p);
	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	int tval = tval_find_idx(parser_getsym(p, "tval"));
	if (tval < 0)
		return PARSE_ERROR_UNRECOGNISED_TVAL;

	int scale = 1;
	if (parser_hasval(p, "scale")) {
		scale = parser_getint(p, "scale");
		if (scale < 1)
			return PARSE_ERROR_INVALID_VALUE;
	}

	/* Find all the right object kinds */
	for (int i = 0; i < z_info->k_max; i++) {
		if (k_info[i].tval != tval) continue;
		if (kf_has(k_info[i].kind_flags, KF_QUEST_ART)) continue;
		if (kf_has(k_info[i].kind_flags, KF_INSTA_ART)) continue;
		if (kf_has(k_info[i].kind_flags, KF_SPECIAL_GEN)) continue;
		struct poss_item *poss = mem_zalloc(sizeof(struct poss_item));
		poss->kidx = i;
		poss->next = t->poss_items;
		poss->scale = scale;
		t->poss_items = poss;
		found_one_kind = true;
	}

	if (!found_one_kind)
		return PARSE_ERROR_INVALID_VALUE;

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_chest_item(struct parser *p) {
	int tval = tval_find_idx(parser_getsym(p, "tval"));
	const char *sval_text = parser_getsym(p, "sval");
	bool negate = false;

	if (*sval_text == '!') {
		negate = true;
		sval_text++;
	}
	int sval = lookup_sval(tval, sval_text);

	struct chest_theme *t = parser_priv(p);
	if (!t)
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (tval < 0)
		return PARSE_ERROR_UNRECOGNISED_TVAL;
	if (sval < 0)
		return PARSE_ERROR_UNRECOGNISED_SVAL;

	int scale = 1;
	if (parser_hasval(p, "scale")) {
		scale = parser_getint(p, "scale");
		if (scale < 1)
			return PARSE_ERROR_INVALID_VALUE;
	}

	if (negate) {
		/* Remove it */
		struct poss_item *pi = t->poss_items;
		int kidx = lookup_kind(tval, sval)->kidx;
		if (kidx <= 0)
			return PARSE_ERROR_INVALID_ITEM_NUMBER;
		struct poss_item *last = NULL;
		while (pi) {
			if ((int)pi->kidx == kidx) {
				if (last)
					last->next = pi->next;
				else
					t->poss_items = pi->next;
				mem_free(pi);
				break;
			}
			last = pi;
			pi = pi->next;
		}
	} else {
		struct poss_item *poss = mem_zalloc(sizeof(struct poss_item));
		poss->kidx = lookup_kind(tval, sval)->kidx;
		if (poss->kidx <= 0)
			return PARSE_ERROR_INVALID_ITEM_NUMBER;
		poss->next = t->poss_items;
		t->poss_items = poss;
		poss->scale = scale;
	}

	return PARSE_ERROR_NONE;
}

struct parser *init_parse_chest(void) {
    struct parser *p = parser_new();
    parser_setpriv(p, NULL);
    parser_reg(p, "name str name", parse_chest_name);
    parser_reg(p, "level int level", parse_chest_level);
    parser_reg(p, "minlevel int minlevel", parse_chest_minlevel);
    parser_reg(p, "maxlevel int maxlevel", parse_chest_maxlevel);
	parser_reg(p, "rarity int rarity", parse_chest_rarity);
	parser_reg(p, "switch int switch", parse_chest_switch);
	parser_reg(p, "type sym tval ?int scale", parse_chest_type);
	parser_reg(p, "item sym tval sym sval ?int scale", parse_chest_item);
    return p;
}

static errr run_parse_chest(struct parser *p) {
    return parse_file_quit_not_found(p, "chest");
}

static errr finish_parse_chest(struct parser *p) {
	parser_destroy(p);
	return 0;
}

static void cleanup_chest(void)
{
	for(int i=0; i<n_chest_themes; i++) {
		struct chest_theme *chest = chest_theme+i;
		string_free(chest->name);
		struct poss_item *item = chest->poss_items;
		while (item) {
			item = chest->poss_items->next;
			mem_free(chest->poss_items);
			chest->poss_items = item;
		};
	};
	n_chest_themes = 0;
	mem_free(chest_theme);
	chest_theme = NULL;
}

struct file_parser chest_parser = {
    "chest",
    init_parse_chest,
    run_parse_chest,
    finish_parse_chest,
    cleanup_chest
};


/**
 * ------------------------------------------------------------------------
 * Chest trap information
 * ------------------------------------------------------------------------ */
/**
 * The name of a chest trap
 */
const char *chest_trap_name(const struct object *obj)
{
	int32_t trap_value = obj->pval;

	/* Non-zero value means there either were or are still traps */
	if (trap_value < 0) {
		return (trap_value == -1) ? "unlocked" : "disarmed";
	} else if (trap_value > 0) {
		struct chest_trap *trap = chest_traps, *found = NULL;
		while (trap) {
			if (trap_value & trap->pval) {
				if (found) {
					return "multiple traps";
				}
				found = trap;
			}
			trap = trap->next;
		}
		if (found) {
			return found->name;
		}
	}

	return "empty";
}

/**
 * Determine if a chest is trapped
 */
bool is_trapped_chest(const struct object *obj)
{
	if (!tval_is_chest(obj))
		return false;

	/* Disarmed or opened chests are not trapped */
	if (obj->pval <= 0)
		return false;

	/* Some chests simply don't have traps */
	return (obj->pval == 1) ? false : true;
}


/**
 * Determine if a chest is locked or trapped
 */
bool is_locked_chest(const struct object *obj)
{
	if (!tval_is_chest(obj))
		return false;

	/* Disarmed or opened chests are not locked */
	return (obj->pval > 0);
}

/**
 * ------------------------------------------------------------------------
 * Chest trap actions
 * ------------------------------------------------------------------------ */
/**
 * Pick a single chest trap for a given level of chest object
 */
static int pick_one_chest_trap(int level)
{
	int count = 0, pick;
	struct chest_trap *trap;

	/* Count possible traps (starting after the "locked" trap) */
	for (trap = chest_traps->next; trap; trap = trap->next) {
		if (trap->level <= level) count++;
	}

	/* Pick a trap, return the pval */
	pick = randint0(count);
	for (trap = chest_traps->next; trap; trap = trap->next) {
		if (!pick--) break;
	}
	return trap->pval;
}

/**
 * Pick a set of chest traps
 * Currently this only depends on the level of the chest object
 */
int pick_chest_traps(struct object *obj)
{
	int level = obj->kind->level;
	int trap = 0;

	/* One in ten chance of no trap */
	if (one_in_(10)) {
		return 1;
	}

	/* Pick a trap, add it */
	trap |= pick_one_chest_trap(level);

	/* Level dependent chance of a second trap (may overlap the first one) */
	if ((level > 5) && one_in_(1 + ((65 - level) / 10))) {
		trap |= pick_one_chest_trap(level);
	}

	/* Chance of a third trap for deep chests (may overlap existing traps) */
	if ((level > 45) && one_in_(65 - level)) {
		trap |= pick_one_chest_trap(level);
		/* Small chance of a fourth trap (may overlap existing traps) */
		if (one_in_(40)) {
			trap |= pick_one_chest_trap(level);
		}
	}

	return trap;
}

/**
 * Unlock a chest
 */
void unlock_chest(struct object *obj)
{
	obj->pval = (0 - obj->pval);
}

/**
 * Determine if a grid contains a chest matching the query type, and
 * return a pointer to the first such chest
 */
struct object *chest_check(const struct player *p, struct loc grid,
		enum chest_query check_type)
{
	struct object *obj;

	/* Scan all objects in the grid */
	for (obj = square_object(cave, grid); obj; obj = obj->next) {
		/* Ignore if requested */
		if (ignore_item_ok(p, obj)) continue;

		/* Check for chests */
		switch (check_type) {
		case CHEST_ANY:
			if (tval_is_chest(obj))
				return obj;
			break;
		case CHEST_OPENABLE:
			if (tval_is_chest(obj) && (obj->pval != 0))
				return obj;
			break;
		case CHEST_TRAPPED:
			if (is_trapped_chest(obj) && obj->known && obj->known->pval)
				return obj;
			break;
		}
	}

	/* No chest */
	return NULL;
}


/**
 * Return the number of grids holding a chests around (or under) the character.
 * If requested, count only trapped chests.
 */
int count_chests(struct loc *grid, enum chest_query check_type)
{
	int d, count;

	/* Count how many matches */
	count = 0;

	/* Check around (and under) the character */
	for (d = 0; d < 9; d++) {
		/* Extract adjacent (legal) location */
		struct loc grid1 = loc_sum(player->grid, ddgrid_ddd[d]);

		/* No (visible) chest is there */
		if (!chest_check(player, grid1, check_type)) continue;

		/* Count it */
		++count;

		/* Remember the location of the last chest found */
		*grid = grid1;
	}

	/* All done */
	return count;
}

/* Select an object from a chest theme. Can return NULL, in which case it will be retried. */
static struct object *chest_theme_select(const struct chest_theme *chest, int depth, bool good, bool great)
{
	if (!chest->poss_items)
		return NULL;
	struct object_kind *kind = select_poss_kind(chest->poss_items, depth, 0);
	if (!kind)
		return NULL;
	struct object *obj = make_object_named(cave, depth, good, great, false, NULL, kind->tval, kind->name);
	return obj;
}

/* Repeatedly select an entry at random, check rarity/level/min and max level */
const struct chest_theme *select_theme(int level)
{
	const struct chest_theme *theme = NULL;

	/* This should prevent chances dropping to 0, so ensuring the loop exits */
	if (level < 1)
		level = 1;
	if (level > 99)
		level = 99;

	assert(n_chest_themes);

	do {
		theme = chest_theme + randint0(n_chest_themes);
		/* Check min, max levels and rarity */
		if (theme->minlevel > level)
			continue;
		if (theme->maxlevel < level)
			continue;
		if ((theme->rarity > 1) && (!one_in_(theme->rarity)))
			continue;
		/* Check level.
		 * This check will always pass on its native level, and fall off towards min and max
		 * level, linearly.
		 */
		if ((theme->level > level) && (rand_range(theme->minlevel, theme->level) > level))
			continue;
		if ((theme->level < level) && (rand_range(theme->level, theme->maxlevel) < level))
			continue;
	} while (!theme);
	return theme;
}

/**
 * Allocate objects upon opening a chest
 *
 * Disperse treasures from the given chest, centered at (x,y).
 *
 * Whether the items are good and/or great is dependent on the level the container
 * was generated at. The depth at which the items are generated depends on the
 * level generated at, the container type and whether the chest is good or great.
 * The number of items created also depends (with wide randomization) on this item
 * generation depth.
 */
static void chest_death(struct loc grid, struct object *chest)
{
	/* Zero pval means empty chest */
	if (!chest->pval)
		return;

	/* Maximum weight of contents */
	int weight = (chest->kind->weight * 2) / 3;

	/* Select good/great:
	 * 1/4 are good at level 0, all at level 15+
	 * None are great until level 10. All are by level 60.
	 * All chests - but especially good and great ones - at lower levels get a bonus to depth
	 * to decrease the chances of getting junk.
	 **/
	bool good = (randint0(20) < chest->origin_depth + 5);
	bool great = (good && (randint0(70) < chest->origin_depth - 10));
	int depth = (chest->origin_depth + MAX(chest->origin_depth, chest->kind->level)) / 2;
	if (great && depth < 40)
		depth += (40-depth) / 5;
	else if (good && depth < 25)
		depth += (25-depth) / 5;
	else if (depth < 15)
		depth += (15-depth) / 5;

	/* Determine how much to drop (see above) */
	int number = Rand_normal(depth + 20, depth * 2) / 20;
	if (number < 1)
		number = 1 - number;
	while (one_in_(number+1))
		number++;
	/* Select the first theme. */
	const struct chest_theme *theme = select_theme(depth);

	/* Drop some valuable objects (non-chests) */
	int totalweight = 0;
	while (number > 0) {
		int reps = 1e5; /* Paranoia - shouldn't be needed unless the theme fn is ridiculously fussy */
		struct object *treasure;
		do {
			treasure = chest_theme_select(theme, depth, good, great);
			reps--;
		} while ((!treasure) && (reps > 0));
		/* Just in case the theme fn can't find an item: make a random object */
		if (!treasure) {
			treasure = make_object(cave, depth, good, great, false, NULL, 0);
		}
		if (!treasure) {
			continue;
		}

		if (tval_is_chest(treasure)) {
			object_delete(cave, player->cave, &treasure);
			continue;
		}

		/* Ensure total weight of treasure is some way below the weight of the treasure plus container.
		 * This is only for realism - it shouldn't be expected to limit contents, given that there are
		 * some useful 1-gram objects.
		 **/
		if (treasure->weight + totalweight > weight) {
			object_delete(cave, player->cave, &treasure);
			continue;
		}

		totalweight += treasure->weight;
		treasure->origin = ORIGIN_CHEST;
		treasure->origin_depth = chest->origin_depth;
		drop_near(cave, &treasure, 0, grid, true, false);
		number--;
		
		/* Now select another theme, sometimes */
		if (randint0(100) < theme->change) {
			theme = select_theme(depth);
		}
	}

	/* Chest is now empty */
	chest->pval = 0;
	chest->known->pval = 0;
}


/**
 * Chests have traps too.
 */
static void chest_trap(struct object *obj)
{
	int traps = obj->pval;
	struct chest_trap *trap;
	bool ident = false;

	/* Ignore disarmed chests */
	if (traps <= 0) return;

	/* Apply trap effects */
	for (trap = chest_traps; trap; trap = trap->next) {
		if (trap->pval & traps) {
			if (trap->msg) {
				msg(trap->msg);
			}
			if (trap->effect) {
				effect_do(trap->effect, source_chest_trap(trap), obj, &ident,
						  false, 0, 0, 0, NULL, 0);
			}
			if (trap->destroy) {
				obj->pval = 0;
				break;
			}
		}
	}
}


/**
 * Attempt to open the given chest at the given location
 *
 * Assume there is no monster blocking the destination
 *
 * Returns true if repeated commands may continue
 */
bool do_cmd_open_chest(struct loc grid, struct object *obj)
{
	int i, j;

	bool flag = true;

	bool more = false;

	/* Attempt to unlock it */
	if (obj->pval > 0) {
		/* Assume locked, and thus not open */
		flag = false;

		/* Get the "disarm" factor */
		i = player->state.skills[SKILL_DISARM_PHYS];

		/* Penalize some conditions */
		if (player->timed[TMD_BLIND] || no_light(player)) i = i / 10;
		if (player->timed[TMD_CONFUSED] || player->timed[TMD_IMAGE]) i = i / 10;

		/* Extract the difficulty */
		j = i - obj->pval;

		/* Always have a small chance of success */
		if (j < 2) j = 2;

		/* Success -- May still have traps */
		if (randint0(100) < j) {
			msgt(MSG_LOCKPICK, "You have picked the lock.");
			player_exp_gain(player, 1);
			flag = true;
		} else {
			/* We may continue repeating */
			more = true;
			event_signal(EVENT_INPUT_FLUSH);
			msgt(MSG_LOCKPICK_FAIL, "You failed to pick the lock.");
		}
	}

	/* Allowed to open */
	if (flag) {
		/* Apply chest traps, if any and player is not trapsafe */
		if (!player_is_trapsafe(player)) {
			chest_trap(obj);
		} else if ((obj->pval > 0) && player_of_has(player, OF_TRAP_IMMUNE)) {
			/* Learn trap immunity if there are traps */
			equip_learn_flag(player, OF_TRAP_IMMUNE);
		}

		/* Let the Chest drop items */
		chest_death(grid, obj);

		/* Ignore chest if autoignore calls for it */
		player->upkeep->notice |= PN_IGNORE;
	}

	/* Empty chests were always ignored in ignore_item_okay so we
	 * might as well ignore it here
	 */
	if (obj->pval == 0)
		obj->known->notice |= OBJ_NOTICE_IGNORE;

	/* Redraw chest, to be on the safe side (it may have been ignored) */
	square_light_spot(cave, grid);

	/* Result */
	return (more);
}


/**
 * Attempt to disarm the chest at the given location
 * Assume there is no monster blocking the destination
 *
 * The calculation of difficulty assumes that there are 6 types of chest
 * trap; if more are added, it will need adjusting.
 *
 * Returns true if repeated commands may continue
 */
bool do_cmd_disarm_chest(struct object *obj)
{
	int skill = player->state.skills[SKILL_DISARM_PHYS], diff;
	struct chest_trap *traps;
	bool physical = false;
	bool magic = false;
	bool more = false;

	/* Check whether the traps are magic, physical or both */
	for (traps = chest_traps; traps; traps = traps->next) {
		if (!(traps->pval & obj->pval)) continue;
		if (traps->magic) {
			magic = true;
		} else {
			physical = true;
		}
	}

	/* Physical disarming is the default, if there are magic traps we adjust */ 
	if (magic) {
		if (physical) {
			skill = (player->state.skills[SKILL_DISARM_MAGIC] +
					 player->state.skills[SKILL_DISARM_PHYS]) / 2;
		} else {
			skill = player->state.skills[SKILL_DISARM_MAGIC];
		}
	}

	/* Penalize some conditions */
	if (player->timed[TMD_BLIND] || no_light(player)) {
		skill /= 10;
	}
	if (player->timed[TMD_CONFUSED] || player->timed[TMD_IMAGE]) {
		skill /= 10;
	}

	/* Extract the difficulty */
	diff = skill - obj->pval;

	/* Get the object name */
	char o_name[80];
	object_desc(o_name, sizeof(o_name), obj, ODESC_BASE, player);

	/* Always have a small chance of success */
	if (diff < 2) diff = 2;

	/* Must find the trap first. */
	if (!obj->known->pval || ignore_item_ok(player, obj)) {
		msg("I don't see any traps.");
	} else if (!is_trapped_chest(obj)) {
		/* Already disarmed/unlocked or no traps */
		msg("The %s is not trapped.", o_name);
	} else if (randint0(100) < diff) {
		/* Success (get a lot of experience) */
		msgt(MSG_DISARM, "You have disarmed the %s.", o_name);
		player_exp_gain(player, obj->pval);
		obj->pval = -obj->pval;
	} else if (randint0(100) < diff) {
		/* Failure -- Keep trying */
		more = true;
		event_signal(EVENT_INPUT_FLUSH);
		msg("You failed to disarm the %s.", o_name);
	} else {
		/* Failure -- Set off the trap */
		msg("You set off a trap!");
		chest_trap(obj);
	}

	/* Result */
	return (more);
}
