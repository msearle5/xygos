/**
 * \file player-birth.c
 * \brief Character creation
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
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
#include "cmd-core.h"
#include "cmds.h"
#include "game-event.h"
#include "game-world.h"
#include "init.h"
#include "mon-lore.h"
#include "monster.h"
#include "obj-fault.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-init.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-power.h"
#include "obj-properties.h"
#include "obj-randart.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-ability.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-quest.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "savefile.h"
#include "store.h"
#include "ui-birth.h"
#include "world.h"

/**
 * Overview
 * ========
 * This file contains the game-mechanical part of the birth process.
 * To follow the code, start at player_birth towards the bottom of
 * the file - that is the only external entry point to the functions
 * defined here.
 *
 * Player (in the Angband sense of character) birth is modelled as a
 * a series of commands from the UI to the game to manipulate the
 * character and corresponding events to inform the UI of the outcomes
 * of these changes.
 *
 * The current aim of this section is that after any birth command
 * is carried out, the character should be left in a playable state.
 * In particular, this means that if a savefile is supplied, the
 * character will be set up according to the "quickstart" rules until
 * another race or class is chosen, or until the stats are reset by
 * the UI.
 *
 * Once the UI signals that the player is happy with the character, the
 * game does housekeeping to ensure the character is ready to start the
 * game (clearing the history log, making sure options are set, etc)
 * before returning control to the game proper.
 */


/* These functions are defined at the end of the file */
static int roman_to_int(const char *roman);
static int int_to_roman(int n, char *roman, size_t bufsize);


/*
 * Forward declare
 */
typedef struct birther /*lovely*/ birther; /*sometimes we think she's a dream*/

/**
 * A structure to hold "rolled" information, and any
 * other useful state for the birth process.
 *
 * XXX Demand Obama's birth certificate
 */
struct birther
{
	struct player_race *race;
	struct player_race *ext;
	struct player_race *personality;
	struct player_class *class;

	int16_t age;
	int16_t wt;
	int16_t ht;
	int16_t sc;

	int32_t au;

	int16_t stat[STAT_MAX];

	char *history;
	char name[PLAYER_NAME_LEN];
};


/**
 * ------------------------------------------------------------------------
 * All of these should be in some kind of 'birth state' struct somewhere else
 * ------------------------------------------------------------------------ */


static int stats[STAT_MAX];
static int points_spent[STAT_MAX];
static int points_left;

static bool quickstart_allowed;
static bool rolled_stats = false;

/**
 * The last character displayed, to allow the user to flick between two.
 * We rely on prev.age being zero to determine whether there is a stored
 * character or not, so initialise it here.
 */
static birther prev;

/**
 * If quickstart is allowed, we store the old character in this,
 * to allow for it to be reloaded if we step back that far in the
 * birth process.
 */
static birther quickstart_prev;




/**
 * Save the currently rolled data into the supplied 'player'.
 */
static void save_roller_data(birther *tosave)
{
	int i;

	/* Save the data */
	tosave->race = player->race;
	tosave->ext = player->extension;
	tosave->personality = player->personality;
	tosave->class = player->class;
	tosave->age = player->age;
	tosave->wt = player->wt_birth;
	tosave->ht = player->ht_birth;
	tosave->au = player->au_birth;

	/* Save the stats */
	for (i = 0; i < STAT_MAX; i++)
		tosave->stat[i] = player->stat_birth[i];

	if (tosave->history) {
		string_free(tosave->history);
	}
	tosave->history = player->history;
	player->history = NULL;
	my_strcpy(tosave->name, player->full_name, sizeof(tosave->name));
}


/**
 * Load stored player data from 'player' as the currently rolled data,
 * optionally placing the current data in 'prev_player' (if 'prev_player'
 * is non-NULL).
 *
 * It is perfectly legal to specify the same "birther" for both 'player'
 * and 'prev_player'.
 */
static void load_roller_data(birther *saved, birther *prev_player)
{
	int i;

     /* The initialisation is just paranoia - structure assignment is
        (perhaps) not strictly defined to work with uninitialised parts
        of structures. */
	birther temp;
	memset(&temp, 0, sizeof(birther));

	/* Save the current data if we'll need it later */
	if (prev_player)
		save_roller_data(&temp);

	/* Load previous data */
	player->race     = saved->race;
	player->extension = saved->ext;
	player->personality = saved->personality;
	player->class    = saved->class;
	player->age      = saved->age;
	player->wt       = player->wt_birth = saved->wt;
	player->ht       = player->ht_birth = saved->ht;
	player->au_birth = saved->au;
	player->au       = z_info->start_gold;

	/* Load previous stats */
	for (i = 0; i < STAT_MAX; i++) {
		player->stat_max[i] = player->stat_cur[i] = player->stat_birth[i]
			= saved->stat[i];
		player->stat_map[i] = i;
	}

	/* Load previous history */
	if (player->history) {
		string_free(player->history);
	}
	player->history = string_make(saved->history);
	my_strcpy(player->full_name, saved->name, sizeof(player->full_name));

	/* Save the current data if the caller is interested in it. */
	if (prev_player) {
		if (prev_player->history) {
			string_free(prev_player->history);
		}
		*prev_player = temp;
	}
}


/**
 * Roll for a characters stats
 *
 * For efficiency, we include a chunk of "calc_bonuses()".
 */
static void get_stats(int stat_use[STAT_MAX])
{
	int i, j;

	int dice[3 * STAT_MAX];

	/* Roll and verify some stats */
	while (true) {
		/* Roll some dice */
		for (j = i = 0; i < 3 * STAT_MAX; i++) {
			/* Roll the dice */
			dice[i] = randint1(3 + i % 3);

			/* Collect the maximum */
			j += dice[i];
		}

		/* Verify totals */
		if ((j > 7 * STAT_MAX) && (j < 9 * STAT_MAX)) break;
	}

	/* Roll the stats */
	for (i = 0; i < STAT_MAX; i++) {
		int bonus;

		/* Extract 5 + 1d3 + 1d4 + 1d5 */
		j = 5 + dice[3 * i] + dice[3 * i + 1] + dice[3 * i + 2];

		/* Save that value */
		player->stat_max[i] = j;

		/* Obtain a "bonus" for "race", "extension", "personality" and "class" */
		bonus = player->race->r_adj[i] + player->extension->r_adj[i] + player->class->c_adj[i] + player->personality->r_adj[i];

		/* Start fully healed */
		player->stat_cur[i] = player->stat_max[i];

		/* Start with unscrambled stats */
		player->stat_map[i] = i;

		/* Efficiency -- Apply the racial/class bonuses */
		stat_use[i] = modify_stat_value(player->stat_max[i], bonus);

		player->stat_birth[i] = player->stat_max[i];
	}
}

/** Worst (= most negative) negative deviation, as a proportion of the expected value
 * in 10000ths
 */
int hp_roll_worst(int hitdie, int *level)
{
	int worst = 0;
	int rdev = 0;
	for(int i=1; i<PY_MAX_LEVEL-10; i++) {
		int mean = (i / 2) + ((hitdie * i) / (PY_MAX_LEVEL - 1));
		rdev += level[i];
		if (i >= 5) {
			int dev = rdev - mean;
			dev *= 10000;
			dev /= mean;
			if (dev < worst)
				worst = dev;
		}
	}
	return worst;
}

/** Sum of squares of of negative deviations */
int hp_roll_score(int hitdie, int *level)
{
	int sum = 0;
	int rdev = 0;
	for(int i=1; i<PY_MAX_LEVEL; i++) {
		int mean = (i / 2) + ((hitdie * i) / (PY_MAX_LEVEL - 1));
		rdev += level[i];
		int dev = rdev - mean;
		if (dev < 0)
			sum += (dev * dev);
	}
	return sum;
}

/** Produce hitpoints which are random at all levels except the first
 * which always gets max hit points, but which at maximum level always
 * has the same value (the average value of 49 hitdie rolls, + 1 max).
 * It should also not deviate too far from the average at any other
 * level.
 */
static void roll_hp_class(int hitdie, int16_t *player_hp)
{
	/* Average expected HP - ignoring the first level */
	int target = hitdie;

	/* Roll all hitpoints */
	int level[PY_MAX_LEVEL];
	int sum = 0;
	for(int i=1;i<PY_MAX_LEVEL;i++) {
		level[i] = 1+(randint0(target * 2) / (PY_MAX_LEVEL - 1));
		sum += level[i];
	}

	/* Reroll a level at random and take the maximum or minimum,
	 * until the sum is right.
	 */
	assert(target >= PY_MAX_LEVEL - 1);
	while (sum != target) {
		int l = randint1(PY_MAX_LEVEL-1);
		int prev = level[l];
		int reroll = 1+(randint0(target * 2) / (PY_MAX_LEVEL - 1));
		int best;
		if (sum < target)
			best = MAX(prev, reroll);
		else
			best = MIN(prev, reroll);
		sum -= level[l];
		level[l] = best;
		sum += best;
	}

	/* Reduce deviation at midlevels without affecting the sum by
	 * swapping pairs of rolls.
	 * Select a pair at random, and determine a score with and
	 * without the change - keep the swap if the score has improved.
	 * The score is the geometric mean of all negative deviations, and
	 * the process finishes when the worst-case -ve deviation between
	 * levels 5 and 40 has been reduced below 5%.
	 * Give up if 100K rolls have been made without progress - this can
	 * happen with some unexpected inputs, such as a very low hitdie.
	 */
	int runrolls = 0;
	do {
		int before = hp_roll_score(hitdie, level);
		int from = randint1(PY_MAX_LEVEL-1);
		int to = randint1(PY_MAX_LEVEL-1);
		int tmp = level[from];
		level[from] = level[to];
		level[to] = tmp;
		int after = hp_roll_score(hitdie, level);
		if (before <= after) {
			// revert it - this has increased the deviation
			tmp = level[from];
			level[from] = level[to];
			level[to] = tmp;
		} else {
			runrolls = 0;
		}
		runrolls++;
	} while ((hp_roll_worst(hitdie, level) < -500) && (runrolls < 100000)); // 5%

	/* Copy into the player's hitpoints */
	player_hp[0] = (2 * hitdie) / PY_MAX_LEVEL;
	for (int i = 1; i < PY_MAX_LEVEL; i++)
		player_hp[i] = player_hp[i-1] + level[i];
}

/** Roll hitpoints for all classes */
void roll_hp(void)
{
	for (struct player_class *c = classes; c; c = c->next) {
		int hitdie = hitdie_class(c);
		roll_hp_class(hitdie, player->player_hp + (PY_MAX_LEVEL * c->cidx));
	}
}

static void get_bonuses(void)
{
	/* Calculate the bonuses and hitpoints */
	player->upkeep->update |= (PU_BONUS | PU_HP);

	/* Update stuff */
	update_stuff(player);

	/* Fully healed */
	player->chp = player->mhp;
}


/**
 * Get the racial history using the "history charts".
 */
char *get_history(struct history_chart *chart)
{
	struct history_entry *entry;
	char *res = NULL;

	while (chart) {
		int roll = randint1(100);
		for (entry = chart->entries; entry; entry = entry->next)
			if (roll <= entry->roll)
				break;
		assert(entry);

		res = string_append(res, entry->text);
		chart = entry->succ;
	}

	return res;
}

/**
 * Computes character's height, and weight
 */
void get_height_weight(struct player *p)
{
	/* Calculate the height/weight */
	p->ht = p->ht_birth = Rand_normal(p->race->base_hgt + p->extension->base_hgt, p->race->mod_hgt + p->extension->mod_hgt);
	p->wt = p->wt_birth = Rand_normal(p->race->base_wgt + p->extension->base_wgt, p->race->mod_wgt + p->extension->mod_wgt);
}

/**
 * Computes character's age, height, and weight
 */
static void get_ahw(struct player *p)
{
	/* Calculate the age */
	p->age = p->race->b_age + p->extension->b_age + randint1(p->race->m_age + p->extension->m_age);
	get_height_weight(p);
}

/**
 * Creates the player's body
 */
void player_embody(struct player *p)
{
	player_set_body(p, player_race_body(p));
}

/**
 * Get the player's starting money
 */
static void get_money(void)
{
	player->au = player->au_birth = Rand_normal(z_info->start_gold, z_info->start_gold_spread);
}

void player_init(struct player *p)
{
	int i;
	struct player_options opts_save = p->opts;
	bool randarts = OPT(p, birth_randarts);

	player_cleanup_members(p);

	/* Wipe the player */
	memset(p, 0, sizeof(struct player));

	/* Determine number of artifacts to use */
	z_info->a_max = z_info->a_base;
	if (randarts)
		z_info->a_max += z_info->rand_art;

	/* Start with no artifacts made yet */
	for (i = 0; z_info && i < z_info->a_max; i++) {
		mark_artifact_created(&a_info[i], false);
		mark_artifact_seen(&a_info[i], false);
	}

	/* Start with no quests */
	player_quests_reset(p);

	for (i = 1; z_info && i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];
		kind->tried = false;
		kind->aware = false;
	}

	for (i = 1; z_info && i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];
		struct monster_lore *lore = get_lore(race);
		race->cur_num = 0;
		race->max_num = 100;
		if (rf_has(race->flags, RF_UNIQUE))
			race->max_num = 1;
		lore->pkills = 0;
		lore->thefts = 0;
	}

	p->upkeep = mem_zalloc(sizeof(struct player_upkeep));
	p->upkeep->inven = mem_zalloc((z_info->pack_size + 1) *
								  sizeof(struct object *));
	p->upkeep->quiver = mem_zalloc(z_info->quiver_size *
								   sizeof(struct object *));
	p->timed = mem_zalloc(TMD_MAX * sizeof(int16_t));
	p->obj_k = mem_zalloc(sizeof(struct object));
	p->obj_k->brands = mem_zalloc(z_info->brand_max * sizeof(bool));
	p->obj_k->slays = mem_zalloc(z_info->slay_max * sizeof(bool));
	p->obj_k->faults = mem_zalloc(z_info->fault_max *
								  sizeof(struct fault_data));

	/* Options should persist */
	p->opts = opts_save;

	/* First turn. */
	turn = 1;
	p->total_energy = 0;
	p->resting_turn = 0;

	/* Default to the first race/class in the edit file */
	p->race = races;
	p->extension = extensions;
	p->personality = personalities;
	while (!p->extension->personality)
		p->extension = p->extension->next;
	while (p->personality->next)
		p->personality = p->personality->next;
	p->class = classes;

	/* Player starts unshapechanged */
	p->shape = lookup_player_shape("normal");
}

/**
 * Try to wield everything wieldable in the inventory.
 */
void wield_all(struct player *p)
{
	struct object *obj, *new_pile = NULL, *new_known_pile = NULL;
	int slot;

	/* Scan through the slots */
	for (obj = p->gear; obj; obj = obj->next) {
		struct object *obj_temp;

		/* Skip non-objects */
		assert(obj);

		/* Make sure we can wield it */
		slot = wield_slot(obj);
		if (slot < 0 || slot >= p->body.count)
			continue;

		obj_temp = slot_object(p, slot);
		if (obj_temp)
			continue;

		/* Split if necessary */
		if (obj->number > 1) {
			/* All but one go to the new object */
			struct object *new = object_split(obj, obj->number - 1);

			/* Add to the pile of new objects to carry */
			pile_insert(&new_pile, new);
			pile_insert(&new_known_pile, new->known);
		}

		/* Wear the new stuff */
		p->body.slots[slot].obj = obj;
		object_learn_on_wield(p, obj);

		/* Increment the equip counter by hand */
		p->upkeep->equip_cnt++;
	}

	/* Now add the unwielded split objects to the gear */
	if (new_pile) {
		pile_insert_end(&player->gear, new_pile);
		pile_insert_end(&player->gear_k, new_known_pile);
	}
	return;
}

void add_start_items(struct player *p, const struct start_item *si, bool skip, bool pay, int origin)
{
	struct object *obj;
	for (; si; si = si->next) {
		int num = rand_range(si->min, si->max);
		struct object_kind *kind;
		bool ok = true;
		bool ignore = false;

		/* Loop until success. Failing because of a 'special case' here allows generation
		 * again: this is a property of the object and only occurs randomly so won't cause
		 * further looping. Start_kit and no_food OTOH cause the item to be ignored.
		 */
		do {
			ok = true;

			if (si->artifact) {
				kind = lookup_kind(si->artifact->tval, si->artifact->sval);
				break;
			}

			if (si->sval > SV_UNKNOWN)
				kind = lookup_kind(si->tval, si->sval);
			else
				kind = get_obj_num(0, false, si->tval);
			assert(kind);

			/* Without start_kit, only start with 1 food and 1 light */
			if (skip) {
				if (!tval_is_food_k(kind) && !tval_is_light_k(kind)) {
					ignore = true;
					break;
				}

				num = 1;
			}

			/* Exclude if configured to do so based on birth options. */
			if (si->eopts) {
				bool included = true;
				int eind = 0;

				while (si->eopts[eind] && included) {
					if (si->eopts[eind] > 0) {
						if (p->opts.opt[si->eopts[eind]]) {
							included = false;
						}
					} else {
						if (!p->opts.opt[-si->eopts[eind]]) {
							included = false;
						}
					}
					++eind;
				}
				if (!included) continue;
			}

			/* Discard special cases and worthless items, onless asked for specifically */
			if (si->sval == SV_UNKNOWN) {
				/* If you can't eat food, don't start with it */
				if (tval_is_food_k(kind))
					if (player_has(p, PF_NO_FOOD)) {
						ignore = true;
						break;
					}

				if ((kf_has(kind->kind_flags, KF_SPECIAL_GEN)) && (!special_item_can_gen(kind)))
					ok = false;

				/* Discard worthless items */
				if (kind->cost == 0)
					ok = false;
			}
		} while (!ok);

		if (ignore)
			continue;

		/* Prepare a new item */
		obj = object_new();

		if (si->artifact) {
			object_prep(obj, kind, si->artifact->alloc_min, RANDOMISE);
			obj->artifact = si->artifact;
			copy_artifact_data(obj, obj->artifact);
			mark_artifact_created(obj->artifact, true);
		}
		else {
			object_prep(obj, kind, player->max_depth, RANDOMISE);
			for(int i=0; i<MAX_EGOS; i++) {
				if (si->ego[i]) {
					obj->ego[i] = si->ego[i];
					ego_apply_magic(obj, 0);
				}
			}
		}
		obj->number = num;
		obj->origin = origin;

		object_know_all(obj);

		/* Deduct the cost of the item from starting cash */
		if (pay)
			p->au -= object_value_real(obj, obj->number);

		/* Carry the item */
		if (pack_slots_used(p) <= z_info->pack_size)
			inven_carry(p, obj, true, false);
		else
			drop_near(cave, &obj, 0, player->grid, true, true);
		kind->everseen = true;
	}
}

/**
 * Initialize the global player as if the full birth process happened.
 * \param nrace Is the name of the race to use.  It may be NULL to use *races.
 * \param nclass Is the name of the class to use.  It may be NULL to use
 * *classes.
 * \param nplayer Is the name to use for the player.  It may be NULL.
 * \return The return value will be true if the full birth process will be
 * successful.  It will be false if the process failed.  One reason for that
 * would be that the requested race or class could not be found.
 * Requires a prior call to init_angband().  Intended for use by test cases
 * or stub front ends that need a fully initialized player.
 */
bool player_make_simple(const char *nrace, const char *next, const char *nclass,
	const char* nplayer)
{
	int ir = 0, ic = 0, ie = 0, ip = 0;

	if (nrace) {
		const struct player_race *rc = races;
		int nr = 0;

		while (rc != extensions) {
			if (!rc) return false;
			if (streq(rc->name, nrace)) break;
			rc = rc->next;
			++ir;
			++nr;
		}
		while (rc != extensions) {
			rc = rc->next;
			++nr;
		}
		ir = nr - ir  - 1;
	} else {
		const struct player_race *rc = races;
		while (rc != extensions) {
			rc = rc->next;
			++ir;
		}
		--ir;
	}

	if (next) {
		const struct player_race *ec = extensions;
		int ne = 0;

		while (ec != personalities) {
			if (!ec) return false;
			if (streq(ec->name, next)) break;
			ec = ec->next;
			++ie;
			++ne;
		}
		while (ec != personalities) {
			ec = ec->next;
			++ne;
		}
		ie = ne - ie  - 1;
	} else {
		const struct player_race *rc = extensions;
		while (rc != personalities) {
			rc = rc->next;
			++ie;
		}
		--ie;
	}

	if (nclass) {
		const struct player_class *cc = classes;
		int nc = 0;

		while (1) {
			if (!cc) return false;
			if (streq(cc->name, nclass)) break;
			cc = cc->next;
			++ic;
			++nc;
		}
		while (cc) {
			cc = cc->next;
			++nc;
		}
		ic = nc - ic - 1;
	}

	cmdq_push(CMD_BIRTH_INIT);
	cmdq_push(CMD_BIRTH_RESET);
	cmdq_push(CMD_CHOOSE_RACE);
	cmd_set_arg_choice(cmdq_peek(), "choice", ir);
	cmdq_push(CMD_CHOOSE_EXT);
	cmd_set_arg_choice(cmdq_peek(), "choice", ie);
	cmdq_push(CMD_CHOOSE_PERSONALITY);
	cmd_set_arg_choice(cmdq_peek(), "choice", ip);
	cmdq_push(CMD_CHOOSE_CLASS);
	cmd_set_arg_choice(cmdq_peek(), "choice", ic);
	cmdq_push(CMD_NAME_CHOICE);
	cmd_set_arg_string(cmdq_peek(), "name",
		(nplayer == NULL) ? "Simple" : nplayer);
	cmdq_push(CMD_ACCEPT_CHARACTER);
	setup_menus();
	cmdq_execute(CTX_BIRTH);

	return true;
}

/**
 * Init players with some belongings
 *
 * Having an item identifies it and makes the player "aware" of its purpose.
 */
static void player_outfit(struct player *p)
{
	int i;

	/* Currently carrying nothing */
	p->upkeep->total_weight = 0;

	/* Give the player obvious object knowledge */
	p->obj_k->dd = 1;
	p->obj_k->ds = 1;
	p->obj_k->ac = 1;
	for (i = 1; i < OF_MAX; i++) {
		struct obj_property *prop = lookup_obj_property(OBJ_PROPERTY_FLAG, i);
		if (prop->subtype == OFT_LIGHT) of_on(p->obj_k->flags, i);
		if (prop->subtype == OFT_DIG) of_on(p->obj_k->flags, i);
		if (prop->subtype == OFT_THROW) of_on(p->obj_k->flags, i);
	}

	/* Give the player starting equipment */
	add_start_items(p, p->class->start_items, (!OPT(p, birth_start_kit)), true, ORIGIN_BIRTH);
	add_start_items(p, p->race->start_items, (!OPT(p, birth_start_kit)), true, ORIGIN_BIRTH);
	add_start_items(p, p->extension->start_items, (!OPT(p, birth_start_kit)), true, ORIGIN_BIRTH);
	add_start_items(p, p->personality->start_items, (!OPT(p, birth_start_kit)), true, ORIGIN_BIRTH);

	/* Sanity check */
	if (p->au < 0)
		p->au = 0;

	/* Now try wielding everything */
	wield_all(p);

	/* Update knowledge */
	update_player_object_knowledge(p);
}


/**
 * Cost of each "point" of a stat.
 */
static const unsigned char birth_stat_costs[STAT_MAX][18 + 1] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 5, 6, 7,10,15,20 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 5, 6, 7,10,15,20 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 5, 6, 7,10,15,20 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 5, 6, 7,10,15,20 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 4, 5, 6, 7,10,15,20 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,10,15,25,40,60,100,100,100 },
};

/* It was feasible to get base 17 in 3 stats with the autoroller */
#define MAX_BIRTH_POINTS 150 /* major stats are 50 for 17 */

static void recalculate_stats(int *stats_local_local, int points_left_local)
{
	int i;

	/* Variable stat maxes */
	for (i = 0; i < STAT_MAX; i++) {
		player->stat_cur[i] = player->stat_max[i] =
				player->stat_birth[i] = stats_local_local[i];
		player->stat_map[i] = i;
	}

	/* Gold is inversely proportional to cost */
	player->au_birth = Rand_normal(z_info->start_gold, z_info->start_gold_spread) + (50 * points_left_local);

	/* Update bonuses, hp, etc. */
	get_bonuses();

	/* Tell the UI about all this stuff that's changed. */
	event_signal(EVENT_GOLD);
	event_signal(EVENT_AC);
	event_signal(EVENT_HP);
	event_signal(EVENT_STATS);
}

static void reset_stats(int stats_local[STAT_MAX], int points_spent_local_local[STAT_MAX],
						int *points_left_local, bool update_display)
{
	int i;

	/* Calculate and signal initial stats and points totals. */
	*points_left_local = MAX_BIRTH_POINTS;

	for (i = 0; i < STAT_MAX; i++) {
		/* Initial stats are all 10 and costs are zero */
		stats_local[i] = 10;
		points_spent_local_local[i] = 0;
	}

	/* Use the new "birth stat" values to work out the "other"
	   stat values (i.e. after modifiers) and tell the UI things have 
	   changed. */
	if (update_display) {
		recalculate_stats(stats_local, *points_left_local);
		event_signal_birthpoints(points_spent_local_local, *points_left_local);	
	}
}

static bool buy_stat(int choice, int stats_local[STAT_MAX],
					 int points_spent_local[STAT_MAX], int *points_left_local,
					 bool update_display)
{
	/* Must be a valid stat, and have a "base" of below 18 to be adjusted */
	if (!(choice >= STAT_MAX || choice < 0) &&	(stats_local[choice] < 18)) {
		/* Get the cost of buying the extra point (beyond what
		   it has already cost to get this far). */
		int stat_cost = birth_stat_costs[choice][stats_local[choice] + 1];

		if (stat_cost <= *points_left_local) {
			stats_local[choice]++;
			points_spent_local[choice] += stat_cost;
			*points_left_local -= stat_cost;

			if (update_display) {
				/* Tell the UI the new points situation. */
				event_signal_birthpoints(points_spent_local, *points_left_local);

				/* Recalculate everything that's changed because
				   the stat has changed, and inform the UI. */
				recalculate_stats(stats_local, *points_left_local);
			}

			return true;
		}
	}

	/* Didn't adjust stat. */
	return false;
}


static bool sell_stat(int choice, int stats_local[STAT_MAX], int points_spent_local[STAT_MAX],
	int *points_left_local, bool update_display)
{
	/* Must be a valid stat, and we can't "sell" stats below the base of 10. */
	if (!(choice >= STAT_MAX || choice < 0) && (stats_local[choice] > 10)) {
		int stat_cost = birth_stat_costs[choice][stats_local[choice]];

		stats_local[choice]--;
		points_spent_local[choice] -= stat_cost;
		*points_left_local += stat_cost;

		if (update_display) {
			/* Tell the UI the new points situation. */
			event_signal_birthpoints(points_spent_local, *points_left_local);

			/* Recalculate everything that's changed because
			   the stat has changed, and inform the UI. */
			recalculate_stats(stats_local, *points_left_local);
		}

		return true;
	}

	/* Didn't adjust stat. */
	return false;
}


/**
 * This picks some reasonable starting values for stats based on the
 * current race/class combo, etc.  For now I'm disregarding concerns
 * about role-playing, etc, and using the simple outline from
 * http://angband.oook.cz/forum/showpost.php?p=17588&postcount=6:
 *
 * 0. buy base STR 17
 * 1. if possible buy adj DEX of 18/10
 * 2. spend up to half remaining points on each of spell-stat and con, 
 *    but only up to max base of 16 unless a pure class 
 *    [mage or priest or warrior]
 * 3. If there are any points left, spend as much as possible in order 
 *    on DEX and then the non-spell-stat.
 */
static void generate_stats(int st[STAT_MAX], int spent[STAT_MAX], int *left)
{
	int step = 0;
	bool maxed[STAT_MAX] = { 0 };
	/* The assumption here is that techniques are distributed across all stats so there is no
	 * reason to prefer one, but devices do require INT and they are more important than any
	 * users of WIS. And more important to a weaker "caster" type than to a melee type.
	 **/
	int spell_stat = STAT_INT;
	int dex_break = 0;
	bool caster = player->class->max_attacks < 5 ? true : false;
	bool warrior = player->class->max_attacks > 5 ? true : false;

	/* Wrestlers are warrior-types, but only have 4 max attacks */
	if (streq(player->class->name, "Wrestler")) {
		caster = false;
		warrior = true;
	}

	while (*left && step >= 0) {
	
		switch (step) {
		
			/* Buy base STR 17 */
			case 0: {
			
				if (!maxed[STAT_STR] && st[STAT_STR] < 17) {
					if (!buy_stat(STAT_STR, st, spent,
								  left, false))
						maxed[STAT_STR] = true;
				} else {
					step++;
					
					/* If pure caster skip to step 3 */
					if (caster){
						step = 3;
					}
				}

				break;
			}

			/* Try and buy adj DEX of 18/10 */
			case 1: {
				if (!maxed[STAT_DEX] && st[STAT_DEX] < 18+10) {
					if (!buy_stat(STAT_DEX, st, spent,
								  left, true)) {
						maxed[STAT_DEX] = true;
					}
					dex_break = st[STAT_DEX];
				} else {
					step++;
				}

				break;
			}

			/* If we can't get 18/10 dex, sell it back. */
			case 2: {
				while (st[STAT_DEX] > dex_break) {
					sell_stat(STAT_DEX, st, spent, left,
							  false);
					maxed[STAT_DEX] = false;
				}
				step++;
				break;
			}

			/* 
			 * Spend up to half remaining points on each of spell-stat and 
			 * con, but only up to max base of 16 unless a pure class 
			 * [mage or priest or warrior]
			 */
			case 3: 
			{
				int points_trigger = *left / 2;
				
				if (warrior) {
					points_trigger = *left;
				} else {
					while (!maxed[spell_stat] &&
						   (caster || st[spell_stat] < 18) &&
						   spent[spell_stat] < points_trigger) {

						if (!buy_stat(spell_stat, st, spent,
									  left, false)) {
							maxed[spell_stat] = true;
						}

						if (spent[spell_stat] > points_trigger) {
						
							sell_stat(spell_stat, st, spent,
									  left, false);
							maxed[spell_stat] = true;
						}
					}
				}

				/* Skip CON for casters because DEX is more important early
				 * and is handled in 4 */
				while (!maxed[STAT_CON] &&
					   st[STAT_CON] < 16 &&
					   spent[STAT_CON] < points_trigger) {
					   
					if (!buy_stat(STAT_CON, st, spent,
								  left, false)) {
						maxed[STAT_CON] = true;
					}

					if (spent[STAT_CON] > points_trigger) {
						sell_stat(STAT_CON, st, spent,
								  left, false);
						maxed[STAT_CON] = true;
					}
				}
				
				step++;
				break;
			}

			/* 
			 * If there are any points left, spend as much as possible in 
			 * order on speed, DEX, and the non-spell-stat. 
			 */
			case 4:{
			
				int next_stat;

				if (!maxed[STAT_SPD]) {
					next_stat = STAT_SPD;
				} else if (!maxed[STAT_DEX]) {
					next_stat = STAT_DEX;
				} else if (!maxed[STAT_INT] && spell_stat != STAT_INT) {
					next_stat = STAT_INT;
				} else if (!maxed[STAT_WIS] && spell_stat != STAT_WIS) {
					next_stat = STAT_WIS;
				} else if (!maxed[STAT_CHR]) {
					next_stat = STAT_CHR;
				} else {
					step++;
					break;
				}

				/* Buy until we can't buy any more. */
				while (buy_stat(next_stat, st, spent, left,
								false));
				maxed[next_stat] = true;

				break;
			}

			default: {
			
				step = -1;
				break;
			}
		}

		/* Recalculate everything that's changed because the stat has changed (because stat_top is referred to). */
		recalculate_stats(stats, points_left);
	}

	/* Tell the UI the new points situation. */
	event_signal_birthpoints(spent, *left);

	/* Recalculate everything that's changed because
	   the stat has changed, and inform the UI. */
	recalculate_stats(st, *left);
}

int hitdie_class(const struct player_class *c)
{
	return player->race->r_mhp + player->extension->r_mhp + player->personality->r_mhp + c->c_mhp;
}


/**
 * This fleshes out a full player based on the choices currently made,
 * and so is called whenever things like race or class are chosen.
 */
void player_generate(struct player *p, struct player_race *r, struct player_race *e,
					 struct player_class *c, struct player_race *per, bool old_history)
{
	int i;

	if (!c)
		c = p->class;
	if (!r)
		r = p->race;
	if (!e)
		e = p->extension;
	if (!per)
		per = p->personality;

	p->class = c;
	p->race = r;
	p->extension = e;
	p->personality = per;

	/* Level 1 */
	p->max_lev = p->lev = 1;

	/* Set all class levels to the initial class */
	memset(p->lev_class, p->class->cidx, sizeof(p->lev_class));
	p->switch_class = p->class->cidx;

	/* Primary class */
	set_primary_class();

	/* Experience factor */
	p->expfact_low = p->race->r_exp + p->extension->r_exp + p->personality->r_exp;
	p->expfact_high = p->race->r_high_exp + p->extension->r_high_exp + p->personality->r_exp;

	/* HP array */
	if (!(p->player_hp))
		p->player_hp = mem_alloc(sizeof(int16_t ) * PY_MAX_LEVEL * (1 + classes->cidx));

	/* For each class... */
	for (struct player_class *c = classes; c; c = c->next) {
		/* Hitdice */
		int hitdie = hitdie_class(c);
		int16_t *player_hp = player->player_hp + (PY_MAX_LEVEL * c->cidx);

		/* Pre-calculate level 1 hitdice */
		player_hp[0] = (hitdie * 2) / PY_MAX_LEVEL;

		/*
		 * Fill in overestimates of hitpoints for additional levels.  Do not
		 * do the actual rolls so the player can not reset the birth screen
		 * to get a desirable set of initial rolls.
		 */
		for (i = 1; i < p->lev; i++) {
			player_hp[i] = player_hp[i - 1] + (hitdie / PY_MAX_LEVEL);
		}
	}

	/* Initial hitpoints */
	p->mhp = p->player_hp[(PY_MAX_LEVEL * p->class->cidx)];

	/* Roll for age/height/weight */
	get_ahw(p);

	/* Always start with a well fed player */
	p->timed[TMD_FOOD] = PY_FOOD_FULL - 1;

	if (!old_history) {
		if (p->history) {
			string_free(p->history);
		}
		p->history = get_history(p->race->history);
	}
}


/**
 * Reset everything back to how it would be on loading the game.
 */
static void do_birth_reset(bool use_quickstart, birther *quickstart_prev_local)
{
	/* If there's quickstart data, we use it to set default
	   character choices. */
	if (use_quickstart && quickstart_prev_local)
		load_roller_data(quickstart_prev_local, NULL);

	player_generate(player, NULL, NULL, NULL, NULL, use_quickstart && quickstart_prev_local);

	player->depth = 0;

	/* Update stats with bonuses, etc. */
	get_bonuses();
}

void do_cmd_birth_init(struct command *cmd)
{
	char *buf;

	/* The dungeon is not ready */
	character_dungeon = false;

	/*
	 * If there's a quickstart character, store it for later use.
	 * If not, default to whatever the first of the choices is.
	 */
	if (player->ht_birth) {
		/* Handle incrementing name suffix */
		buf = find_roman_suffix_start(player->full_name);
		if (buf) {
			/* Try to increment the roman suffix */
			int success = int_to_roman(
				roman_to_int(buf) + 1,
				buf,
				sizeof(player->full_name) - (buf - (char *)&player->full_name));

			if (!success) {
				msg("Sorry, could not deal with suffix");
			}
		}

		save_roller_data(&quickstart_prev);
		quickstart_allowed = true;
	} else {
		player_generate(player, player_id2race(0), player_id2ext(0), player_id2class(0), player_id2personality(0), false);
		quickstart_allowed = false;
	}

	/* We're ready to start the birth process */
	event_signal_flag(EVENT_ENTER_BIRTH, quickstart_allowed);
}

void do_cmd_birth_reset(struct command *cmd)
{
	player_init(player);
	reset_stats(stats, points_spent, &points_left, false);
	do_birth_reset(quickstart_allowed, &quickstart_prev);
	rolled_stats = false;
}

void do_cmd_choose_race(struct command *cmd)
{
	int choice = 0;
	cmd_get_arg_choice(cmd, "choice", &choice);
	player_generate(player, player_id2race(choice), NULL, NULL, NULL, false);

	reset_stats(stats, points_spent, &points_left, false);
	generate_stats(stats, points_spent, &points_left);
	rolled_stats = false;
}

void do_cmd_choose_ext(struct command *cmd)
{
	int choice = 0;
	cmd_get_arg_choice(cmd, "choice", &choice);
	player_generate(player, NULL, get_ext_from_menu(choice), NULL, NULL, false);

	reset_stats(stats, points_spent, &points_left, false);
	generate_stats(stats, points_spent, &points_left);
	rolled_stats = false;
}

void do_cmd_choose_personality(struct command *cmd)
{
	int choice = 0;
	cmd_get_arg_choice(cmd, "choice", &choice);
	player_generate(player, NULL, NULL, NULL, player_id2personality(choice), false);

	reset_stats(stats, points_spent, &points_left, false);
	generate_stats(stats, points_spent, &points_left);
	rolled_stats = false;
}

void do_cmd_choose_class(struct command *cmd)
{
	int choice = 0;
	cmd_get_arg_choice(cmd, "choice", &choice);
	player_generate(player, NULL, NULL, player_id2class(choice), NULL, false);

	reset_stats(stats, points_spent, &points_left, false);
	generate_stats(stats, points_spent, &points_left);
	rolled_stats = false;
}

void do_cmd_buy_stat(struct command *cmd)
{
	/* .choice is the stat to sell */
	if (!rolled_stats) {
		int choice = 0;
		cmd_get_arg_choice(cmd, "choice", &choice);
		buy_stat(choice, stats, points_spent, &points_left, true);
	}
}

void do_cmd_sell_stat(struct command *cmd)
{
	/* .choice is the stat to sell */
	if (!rolled_stats) {
		int choice = 0;
		cmd_get_arg_choice(cmd, "choice", &choice);
		sell_stat(choice, stats, points_spent, &points_left, true);
	}
}

void do_cmd_reset_stats(struct command *cmd)
{
	/* .choice is whether to regen stats */
	int choice = 0;

	reset_stats(stats, points_spent, &points_left, true);

	cmd_get_arg_choice(cmd, "choice", &choice);
	if (choice)
		generate_stats(stats, points_spent, &points_left);

	rolled_stats = false;
}

void do_cmd_refresh_stats(struct command *cmd)
{
	/* Refreshing is a no-op when using rolled stats. */
	if (rolled_stats) return;
	event_signal_birthpoints(points_spent, points_left);
}

void do_cmd_roll_stats(struct command *cmd)
{
	int i;

	save_roller_data(&prev);

	/* Get a new character */
	get_stats(stats);

	/* Update stats with bonuses, etc. */
	get_bonuses();

	/* There's no real need to do this here, but it's tradition. */
	get_ahw(player);
	if (player->history)
		string_free(player->history);
	player->history = get_history(player->race->history);

	event_signal(EVENT_GOLD);
	event_signal(EVENT_AC);
	event_signal(EVENT_HP);
	event_signal(EVENT_STATS);

	/* Give the UI some dummy info about the points situation. */
	points_left = 0;
	for (i = 0; i < STAT_MAX; i++)
		points_spent[i] = 0;

	event_signal_birthpoints(points_spent, points_left);

	/* Lock out buying and selling of stats based on rolled stats. */
	rolled_stats = true;
}

void do_cmd_prev_stats(struct command *cmd)
{
	/* Only switch to the stored "previous"
	   character if we've actually got one to load. */
	if (prev.age) {
		load_roller_data(&prev, &prev);
		get_bonuses();
	}

	event_signal(EVENT_GOLD);
	event_signal(EVENT_AC);
	event_signal(EVENT_HP);
	event_signal(EVENT_STATS);	
}

void do_cmd_choose_name(struct command *cmd)
{
	const char *str = NULL;
	cmd_get_arg_string(cmd, "name", &str);

	/* Set player name */
	my_strcpy(player->full_name, str, sizeof(player->full_name));

	string_free((char *) str);
}

void do_cmd_choose_history(struct command *cmd)
{
	const char *str = NULL;

	/* Forget the old history */
	if (player->history)
		string_free(player->history);

	/* Get the new history */
	cmd_get_arg_string(cmd, "history", &str);
	player->history = string_make(str);

	string_free((char *) str);
}

void do_cmd_accept_character(struct command *cmd)
{
	options_init_cheat();

	/* Reseed the RNG - this avoids world_init_towns() using the same distances */
	Rand_init();

	if (streq(player->personality->name, "Split")) {
		player->split_p = true;
		personality_split_level(0, 1);
		player->chp = player->mhp;	/* as the change in personality may have changed your max HP */
	}
	roll_hp();

	/* Embody */
	player_embody(player);

	/* Give the player some money */
	get_money();

	/* No quest in progress */
	player->active_quest = -1;

	/* Make a world: towns */
	world_init_towns();

	/* Race, class etc. specific initialization */
	player_hookz(init);

	/* Prompt for birth talents and roll out per-level talent points */
	int level_tp = setup_talents();
	int orig_tp = player->talent_points;
	cmd_abilities(player, true, player->talent_points, NULL);
	init_talent(level_tp, orig_tp);

	/* No artifact generated */
	player->artifact = string_make("of You");

	ignore_birth_init();

	/* Clear old messages, add new starting message */
	history_clear(player);
	history_add(player, "Began the mission to save the galaxy.", HIST_PLAYER_BIRTH);

	/* Note player birth in the message recall */
	message_add(" ", MSG_GENERIC);
	message_add("  ", MSG_GENERIC);
	message_add("====================", MSG_GENERIC);
	message_add("  ", MSG_GENERIC);
	message_add(" ", MSG_GENERIC);

	/* Initialise the spells */
	player_spells_init(player);

	/* Know all icons for ID on walkover */
	if (OPT(player, birth_know_icons))
		player_learn_all_icons(player);

	/* Hack - player knows all combat icons, and "use energy".
	 * Maybe make them not icons? NRM
	 **/
	player->obj_k->to_a = 1;
	player->obj_k->to_h = 1;
	player->obj_k->to_d = 1;
	player->obj_k->modifiers[OBJ_MOD_USE_ENERGY] = 1;

	/* Initialise the stores, dungeon */
	chunk_list_max = 0;

	/* Player learns innate icons */
	player_learn_innate(player);

	/* Restore the standard artifacts (randarts may have been loaded) */
	cleanup_parser(&randart_parser);
	deactivate_randart_file();
	run_parser(&artifact_parser);

	select_artifact_max();

	/* Now only randomize the artifacts if required */
	seed_randart = randint0(0x10000000);
	if (OPT(player, birth_randarts)) {
		do_randart(seed_randart, true, false);
	} else {
		do_randart(seed_randart, true, true);
	}
	deactivate_randart_file();

	/* Seed for flavors */
	seed_flavor = randint0(0x10000000);
	flavor_init();

	/* Know all flavors for auto-ID of consumables */
	if (OPT(player, birth_know_flavors))
		flavor_set_all_aware();

	/* Outfit the player, if they can sell the stuff */
	player_outfit(player);

	/* Cooldowns at zero */
	if (!player->spell)
		player->spell = mem_alloc(sizeof(*player->spell) * total_spells);
	memset(player->spell, 0, sizeof(*player->spell) * total_spells);

	/* Stop the player being quite so dead */
	player->is_dead = false;

	/* Character is now "complete" */
	character_generated = true;
	player->upkeep->playing = true;

	/* Disable repeat command, so we don't try to be born again */
	cmd_disable_repeat();

	/* No longer need the cached history. */
	string_free(prev.history);
	prev.history = NULL;
	string_free(quickstart_prev.history);
	quickstart_prev.history = NULL;

	auto_char_dump();

	/* Now we're really done.. */
	event_signal(EVENT_LEAVE_BIRTH);
}



/**
 * ------------------------------------------------------------------------
 * Roman numeral functions, for dynastic successions
 * ------------------------------------------------------------------------ */


/**
 * Find the start of a possible Roman numerals suffix by going back from the
 * end of the string to a space, then checking that all the remaining chars
 * are valid Roman numerals.
 * 
 * Return the start position, or NULL if there isn't a valid suffix. 
 */
char *find_roman_suffix_start(const char *buf)
{
	const char *start = strrchr(buf, ' ');
	const char *p;
	
	if (start) {
		start++;
		p = start;
		while (*p) {
			if (*p != 'I' && *p != 'V' && *p != 'X' && *p != 'L' &&
			    *p != 'C' && *p != 'D' && *p != 'M') {
				start = NULL;
				break;
			}
			++p;			    
		}
	}
	return (char *)start;
}

/**
 * Converts an arabic numeral (int) to a roman numeral (char *).
 *
 * An arabic numeral is accepted in parameter `n`, and the corresponding
 * upper-case roman numeral is placed in the parameter `roman`.  The
 * length of the buffer must be passed in the `bufsize` parameter.  When
 * there is insufficient room in the buffer, or a roman numeral does not
 * exist (e.g. non-positive integers) a value of 0 is returned and the
 * `roman` buffer will be the empty string.  On success, a value of 1 is
 * returned and the zero-terminated roman numeral is placed in the
 * parameter `roman`.
 */
static int int_to_roman(int n, char *roman, size_t bufsize)
{
	/* Roman symbols */
	char roman_symbol_labels[13][3] =
		{"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX",
		 "V", "IV", "I"};
	const int  roman_symbol_values[13] =
		{1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};

	/* Clear the roman numeral buffer */
	roman[0] = '\0';

	/* Roman numerals have no zero or negative numbers */
	if (n < 1)
		return 0;

	/* Build the roman numeral in the buffer */
	while (n > 0) {
		int i = 0;

		/* Find the largest possible roman symbol */
		while (n < roman_symbol_values[i])
			i++;

		/* No room in buffer, so abort */
		if (strlen(roman) + strlen(roman_symbol_labels[i]) + 1
			> bufsize)
			break;

		/* Add the roman symbol to the buffer */
		my_strcat(roman, roman_symbol_labels[i], bufsize);

		/* Decrease the value of the arabic numeral */
		n -= roman_symbol_values[i];
	}

	/* Ran out of space and aborted */
	if (n > 0) {
		/* Clean up and return */
		roman[0] = '\0';

		return 0;
	}

	return 1;
}


/**
 * Converts a roman numeral (char *) to an arabic numeral (int).
 *
 * The null-terminated roman numeral is accepted in the `roman`
 * parameter and the corresponding integer arabic numeral is returned.
 * Only upper-case values are considered. When the `roman` parameter
 * is empty or does not resemble a roman numeral, a value of -1 is
 * returned.
 *
 * XXX This function will parse certain non-sense strings as roman
 *     numerals, such as IVXCCCVIII
 */
static int roman_to_int(const char *roman)
{
	size_t i;
	int n = 0;
	char *p;

	char roman_token_chr1[] = "MDCLXVI";
	const char *roman_token_chr2[] = {0, 0, "DM", 0, "LC", 0, "VX"};

	const int roman_token_vals[7][3] = {{1000},
	                              {500},
	                              {100, 400, 900},
	                              {50},
	                              {10, 40, 90},
	                              {5},
	                              {1, 4, 9}};

	if (strlen(roman) == 0)
		return -1;

	/* Check each character for a roman token, and look ahead to the
	   character after this one to check for subtraction */
	for (i = 0; i < strlen(roman); i++) {
		char c1, c2;
		int c1i, c2i;

		/* Get the first and second chars of the next roman token */
		c1 = roman[i];
		c2 = roman[i + 1];

		/* Find the index for the first character */
		p = strchr(roman_token_chr1, c1);
		if (p)
			c1i = p - roman_token_chr1;
		else
			return -1;

		/* Find the index for the second character */
		c2i = 0;
		if (roman_token_chr2[c1i] && c2) {
			p = strchr(roman_token_chr2[c1i], c2);
			if (p) {
				c2i = (p - roman_token_chr2[c1i]) + 1;
				/* Two-digit token, so skip a char on the next pass */
				i++;
			}
		}

		/* Increase the arabic numeral */
		n += roman_token_vals[c1i][c2i];
	}

	return n;
}
