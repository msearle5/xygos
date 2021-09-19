/**
 * \file player-calcs.c
 * \brief Player status calculation, signalling ui events based on 
 *	status changes.
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2014 Nick McConnell
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
#include "game-event.h"
#include "game-input.h"
#include "game-world.h"
#include "h-basic.h"
#include "init.h"
#include "mon-msg.h"
#include "mon-util.h"
#include "obj-fault.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-knowledge.h"
#include "obj-pile.h"
#include "obj-power.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-ability.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"

/**
 * Stat Table (INT) -- Devices
 */
byte adj_int_dev[STAT_RANGE];

/**
 * Stat Table (WIS) -- Saving throw
 */
byte adj_wis_sav[STAT_RANGE];

/**
 * Stat Table (DEX) -- disarming
 */
byte adj_dex_dis[STAT_RANGE];

/**
 * Stat Table (INT) -- disarming
 */
byte adj_int_dis[STAT_RANGE];

/**
 * Stat Table (DEX) -- bonus to ac
 */
s16b adj_dex_ta[STAT_RANGE];

/**
 * Stat Table (STR) -- bonus to dam
 */
s16b adj_str_td[STAT_RANGE];

/**
 * Stat Table (DEX) -- bonus to hit
 */
s16b adj_dex_th[STAT_RANGE];

/**
 * Stat Table (STR) -- bonus to hit
 */
s16b adj_str_th[STAT_RANGE];

/**
 * Stat Table (STR) -- weight limit (point at which burdening starts) in grams
 */
s32b adj_str_wgt[STAT_RANGE];

/**
 * Burden Table -- penalty to speed against burden as a proportion of weight limit.
 * 
 * This is a purely exponential function from Limit x 2.0 (10) up. The lower range has
 * been hand tweaked. It's supposed to have round numbers out at round numbers in (4,
 * 10, 20, 40,80, 160), to not have these round numbers used on earlier entries, to
 * have 20 entries for -1, and to never decrease the length of run down the table.
 */
static const byte adj_wgt_speed[1 + (BURDEN_RANGE * (BURDEN_LIMIT - 1))] = {
	/* Limit x 1.0 */
	1,      1,      1,      1,      1,              1,      1,      1,      1,      1,
	1,      1,      1,      1,      1,              1,      1,      1,      1,      1,
	2,      2,      2,      2,      2,              2,      2,      2,      2,      2,
	2,      2,      2,      2,      2,              2,      2,      3,      3,      3,
	3,      3,      3,      3,      3,              3,      3,      3,      3,      3,

	/* Limit x 1.5 */
	4,      4,      4,      4,      4,              4,      4,      4,      4,      4,
	4,      5,      5,      5,      5,              5,      5,      5,      5,      5,
	6,      6,      6,      6,      6,              6,      6,      6,      7,      7,
	7,      7,      7,      7,      7,              8,      8,      8,      8,      8,
	8,      8,      9,      9,      9,              9,      9,      9,      9,      9,

	/* Limit x 2.0 */
	10,     10,     10,     10,     10,             10,     10,     11,     11,     11,
	11,     11,     11,     11,     12,             12,     12,     12,     12,     13,
	13,     13,     13,     13,     13,             14,     14,     14,     14,     14,
	15,     15,     15,     15,     16,             16,     16,     16,     16,     17,
	17,     17,     17,     18,     18,             18,     18,     19,     19,     19,

	/* Limit x 2.5 */
	20,     20,     20,     20,     21,             21,     21,     22,     22,     22,
	22,     23,     23,     23,     24,             24,     24,     25,     25,     26,
	26,     26,     27,     27,     27,             28,     28,     29,     29,     29,
	30,     30,     31,     31,     32,             32,     32,     33,     33,     34,
	34,     35,     35,     36,     36,             37,     37,     38,     38,     39,

	/* Limit x 3.0 */
	40,     40,     41,     41,     42,             42,     43,     44,     44,     45,
	45,     46,     47,     47,     48,             49,     49,     50,     51,     52,
	52,     53,     54,     55,     55,             56,     57,     58,     58,     59,
	60,     61,     62,     63,     64,             64,     65,     66,     67,     68,
	69,     70,     71,     72,     73,             74,     75,     76,     77,     78,

	/* Limit x 3.5 */
	80,     81,     82,     83,     84,             85,     86,     88,     89,     90,
	91,     93,     94,     95,     97,             98,     99,     101,    102,    104,
	105,    107,    108,    110,    111,            113,    114,    116,    117,    119,
	121,    122,    124,    126,    128,            129,    131,    133,    135,    137,
	139,    141,    143,    145,    147,            149,    151,    153,    155,    157,

	/* Limit x 4.0 */
	160,
};

/**
 * Stat Table (STR) -- weapon weight limit in pounds
 */
byte adj_str_hold[STAT_RANGE];


/**
 * Stat Table (STR) -- digging value
 */
byte adj_str_dig[STAT_RANGE];



/**
 * Stat Table (DEX) -- chance of avoiding "theft" and "falling"
 */
byte adj_dex_safe[STAT_RANGE];


/**
 * Stat Table (CON) -- base regeneration rate
 */
byte adj_con_fix[STAT_RANGE];


/**
 * Stat Table (CON) -- extra 1/100th hitpoints per level
 */
s16b adj_con_mhp[STAT_RANGE];

/**
 * Stat Table (STR) -- lookup to convert raw STR values into something suitable for blows calculation - see blow.
 */
byte adj_str_blow[STAT_RANGE];

/**
 * This table is used to help calculate the number of blows the player can
 * make in a single round of attacks (one player turn) with a normal weapon.
 *
 * This number ranges from a single blow/round for weak players to up to six
 * blows/round for powerful warriors.
 *
 * Note that certain artifacts and ego-items give "bonus" blows/round.
 *
 * First, from the player class, we extract some values:
 *
 *    Warrior --> num = 6; mul = 5; div = MAX(30, weapon_weight);
 *    Mage    --> num = 4; mul = 2; div = MAX(40, weapon_weight);
 *    Priest  --> num = 4; mul = 3; div = MAX(35, weapon_weight);
 *    Rogue   --> num = 5; mul = 4; div = MAX(30, weapon_weight);
 *    Ranger  --> num = 5; mul = 4; div = MAX(35, weapon_weight);
 *    Paladin --> num = 5; mul = 5; div = MAX(30, weapon_weight);
 * (all specified in class.txt now)
 *
 * To get "P", we look up the relevant "adj_str_blow[]" (see above),
 * multiply it by "mul", and then divide it by "div", rounding down.
 *
 * To get "D", we look up the relevant "adj_dex_blow[]" (see above).
 *
 * Then we look up the energy cost of each blow using "blows_table[P][D]".
 * The player gets blows/round equal to 100/this number, up to a maximum of
 * "num" blows/round, plus any "bonus" blows/round.
 */
byte blows_table[BLOWS_ROWS][STAT_RANGE];

/**
 * Decide which object comes earlier in the standard inventory listing,
 * defaulting to the first if nothing separates them.
 *
 * \return whether to replace the original object with the new one
 */
bool earlier_object(struct object *orig, struct object *new, bool store)
{
	/* Check we have actual objects */
	if (!new) return false;
	if (!orig) return true;

	/* Usable ammo is before other ammo */
	if (tval_is_ammo(orig) && tval_is_ammo(new)) {
		/* First favour usable ammo */
		if ((player->state.ammo_tval == orig->tval) &&
			(player->state.ammo_tval != new->tval))
			return false;
		if ((player->state.ammo_tval != orig->tval) &&
			(player->state.ammo_tval == new->tval))
			return true;
	}

	/* Objects sort by decreasing type */
	if (orig->tval > new->tval) return false;
	if (orig->tval < new->tval) return true;

	if (!store) {
		/* Non-aware (flavored) items always come last (default to orig) */
		if (!object_flavor_is_aware(new)) return false;
		if (!object_flavor_is_aware(orig)) return true;
	}

	/* Objects sort by increasing sval */
	if (orig->sval < new->sval) return false;
	if (orig->sval > new->sval) return true;

	if (!store) {
		/* Unaware objects always come last (default to orig) */
		if (new->kind->flavor && !object_flavor_is_aware(new)) return false;
		if (orig->kind->flavor && !object_flavor_is_aware(orig)) return true;

		/* Lights sort by decreasing fuel */
		if (tval_is_light(orig)) {
			if (orig->pval > new->pval) return false;
			if (orig->pval < new->pval) return true;
		}
	}

	/* Objects sort by decreasing value, except ammo */
	if (tval_is_ammo(orig)) {
		if (object_value(orig, 1) < object_value(new, 1))
			return false;
		if (object_value(orig, 1) >	object_value(new, 1))
			return true;
	} else {
		if (object_value(orig, 1) >	object_value(new, 1))
			return false;
		if (object_value(orig, 1) <	object_value(new, 1))
			return true;
	}

	/* No preference */
	return false;
}

int equipped_item_slot(struct player_body body, struct object *item)
{
	int i;

	if (item == NULL) return body.count;

	/* Look for an equipment slot with this item */
	for (i = 0; i < body.count; i++)
		if (item == body.slots[i].obj) break;

	/* Correct slot, or body.count if not equipped */
	return i;
}

/**
 * Put the player's inventory and quiver into easily accessible arrays.  The
 * pack may be overfull by one item
 */
void calc_inventory(struct player *p)
{
	int old_inven_cnt = p->upkeep->inven_cnt;
	int n_stack_split = 0;
	int n_pack_remaining = z_info->pack_size - pack_slots_used(p);
	int n_max = 1 + z_info->pack_size + z_info->quiver_size
		+ p->body.count;
	struct object **old_quiver = mem_zalloc(z_info->quiver_size
		* sizeof(*old_quiver));
	struct object **old_pack = mem_zalloc(z_info->pack_size
		* sizeof(*old_pack));
	bool *assigned = mem_alloc(n_max * sizeof(*assigned));
	struct object *current;
	int i, j;

	/*
	 * Equipped items are already taken care of.  Only the others need
	 * to be tested for assignment to the quiver or pack.
	 */
	for (current = p->gear, j = 0; current; current = current->next, ++j) {
		assert(j < n_max);
		assigned[j] = object_is_equipped(p->body, current);
	}
	for (; j < n_max; ++j) {
		assigned[j] = false;
	}

	/* Prepare to fill the quiver */
	p->upkeep->quiver_cnt = 0;

	/* Copy the current quiver and then leave it empty. */
	for (i = 0; i < z_info->quiver_size; i++) {
		if (p->upkeep->quiver[i]) {
			old_quiver[i] = p->upkeep->quiver[i];
			p->upkeep->quiver[i] = NULL;
		} else {
			old_quiver[i] = NULL;
		}
	}

	/* Fill quiver.  First, allocate inscribed items. */
	for (current = p->gear, j = 0; current; current = current->next, ++j) {
		int prefslot;

		/* Skip already assigned (i.e. equipped) items. */
		if (assigned[j]) continue;

		prefslot  = preferred_quiver_slot(current);
		if (prefslot >= 0 && prefslot < z_info->quiver_size
				&& !p->upkeep->quiver[prefslot]) {
			/*
			 * The preferred slot is empty.  Split the stack if
			 * necessary.  Don't allow splitting if it could
			 * result in overfilling the pack by more than one slot.
			 */
			int mult = tval_is_ammo(current) ?
				1 : z_info->thrown_quiver_mult;
			struct object *to_quiver;

			if (current->number * mult
					<= z_info->quiver_slot_size) {
				to_quiver = current;
			} else {
				int nsplit = z_info->quiver_slot_size / mult;

				assert(nsplit < current->number);
				if (nsplit > 0 && n_stack_split
						<= n_pack_remaining) {
					/*
					 * Split off the portion that goes to
					 * the pack.  Since the stack in the
					 * quiver is earlier in the gear list it
					 * will prefer to remain in the quiver
					 * in future calls to calc_inventory()
					 * and will be the preferred target for
					 * combine_pack().
					 */
					to_quiver = current;
					gear_insert_end(p, object_split(current,
						current->number - nsplit));
					++n_stack_split;
				} else {
					to_quiver = NULL;
				}
			}

			if (to_quiver) {
				p->upkeep->quiver[prefslot] = to_quiver;
				p->upkeep->quiver_cnt +=
					to_quiver->number * mult;

				/* In the quiver counts as worn. */
				object_learn_on_wield(p, to_quiver);

				/* That part of the gear has been dealt with. */
				assigned[j] = true;
			}
		}
	}

	/* Now fill the rest of the slots in order. */
	for (i = 0; i < z_info->quiver_size; ++i) {
		struct object *first = NULL;
		int jfirst = -1;

		/* If the slot is full, move on. */
		if (p->upkeep->quiver[i]) continue;

		/* Find the quiver object that should go there. */
		j = 0;
		current = p->gear;
		while (1) {
			if (!current) break;
			assert(j < n_max);

			/*
			 * Only try to assign if not assigned, ammo, and,
			 * if necessary to split, have room for the split
			 * stacks.
			 */
			if (!assigned[j] && tval_is_ammo(current)
					&& (current->number
					<= z_info->quiver_slot_size
					|| (z_info->quiver_slot_size > 0
					&& n_stack_split
					<= n_pack_remaining))) {
				/* Choose the first in order. */
				if (earlier_object(first, current, false)) {
					first = current;
					jfirst = j;
				}
			}

			current = current->next;
			++j;
		}

		/* Stop looking if there's nothing left in the gear. */
		if (!first) break;

		/* Put the item in the slot, splitting (if needed) to fit. */
		if (first->number > z_info->quiver_slot_size) {
			assert(z_info->quiver_slot_size > 0
				&& n_stack_split <= n_pack_remaining);
			/* As above, split off the portion going to the pack. */
			gear_insert_end(p, object_split(first,
				first->number - z_info->quiver_slot_size));
		}
		p->upkeep->quiver[i] = first;
		p->upkeep->quiver_cnt += first->number;

		/* In the quiver counts as worn. */
		object_learn_on_wield(p, first);

		/* That part of the gear has been dealt with. */
		assigned[jfirst] = true;
	}

	/* Note reordering */
	if (character_dungeon) {
		for (i = 0; i < z_info->quiver_size; i++) {
			if (old_quiver[i] && p->upkeep->quiver[i] != old_quiver[i]) {
				msg("You re-arrange your ammunition.");
				break;
			}
		}
	}

	/* Copy the current pack */
	for (i = 0; i < z_info->pack_size; i++) {
		old_pack[i] = p->upkeep->inven[i];
	}

	/* Prepare to fill the inventory */
	p->upkeep->inven_cnt = 0;

	for (i = 0; i <= z_info->pack_size; i++) {
		struct object *first = NULL;
		int jfirst = -1;

		/* Find the object that should go there. */
		j = 0;
		current = p->gear;
		while (1) {
			if (!current) break;
			assert(j < n_max);

			/* Consider it if it hasn't already been handled. */
			if (!assigned[j]) {
				/* Choose the first in order. */
				if (earlier_object(first, current, false)) {
					first = current;
					jfirst = j;
				}
			}

			current = current->next;
			++j;
		}

		/* Allocate */
		p->upkeep->inven[i] = first;
		if (first) {
			++p->upkeep->inven_cnt;
			assigned[jfirst] = true;
		}
	}

	/* Note reordering */
	if (character_dungeon && p->upkeep->inven_cnt == old_inven_cnt) {
		for (i = 0; i < z_info->pack_size; i++) {
			if (old_pack[i] && p->upkeep->inven[i] != old_pack[i]
					 && !object_is_equipped(p->body, old_pack[i])) {
				msg("You re-arrange your pack.");
				break;
			}
		}
	}

	mem_free(assigned);
	mem_free(old_pack);
	mem_free(old_quiver);
}

/**
 * Calculate the players (maximal) hit points
 *
 * Adjust current hitpoints if necessary
 */
static void calc_hitpoints(struct player *p)
{
	long bonus;
	int mhp;

	/* Get "1/100th hitpoint bonus per level" value */
	bonus = adj_con_mhp[p->state.stat_ind[STAT_CON]];

	/* Calculate hitpoints from classes' player_hp tables.
	 * The aim is to produce a weighted average of the classes' hp tables,
	 * proportional to the number of levels per class.
	 **/
	double total = 0;
	for (struct player_class *c = classes; c; c = c->next) {
		int levels = levels_in_class(c->cidx);
		if (levels) {
			double scale = levels;
			scale /= player->lev;
			double part_hp = player->player_hp[(player->lev - 1) + (PY_MAX_LEVEL * c->cidx)];
			part_hp *= scale;
			total += part_hp;
		}
	}
	mhp = total;

	/* This is independent of class */
	mhp += (bonus * p->lev / 100);

	/* Always have at least one hitpoint per level */
	if (mhp < p->lev + 1) mhp = p->lev + 1;

	/* New maximum hitpoints */
	if (p->mhp != mhp) {
		/* Save new limit */
		p->mhp = mhp;

		/* Enforce new limit */
		if (p->chp >= mhp) {
			p->chp = mhp;
			p->chp_frac = 0;
		}

		/* Display hitpoints (later) */
		p->upkeep->redraw |= (PR_HP);
	}
}


/**
 * Calculate and set the current light radius.
 *
 * The light radius will be the total of all lights carried.
 */
static void calc_light(struct player *p, struct player_state *state,
					   bool update)
{
	int i;

	/* Assume no light */
	state->cur_light = 0;

	/* Ascertain lightness if in the town */
	if (!p->depth && is_daytime() && update) {
		/* Update the visuals if necessary*/
		if (p->state.cur_light != state->cur_light)
			p->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);

		return;
	}

	/* Examine all wielded objects, use the brightest */
	for (i = 0; i < p->body.count; i++) {
		int amt = 0;
		struct object *obj = slot_object(p, i);

		/* Skip empty slots */
		if (!obj) continue;

		/* Light radius - innate plus modifier */
		if (of_has(obj->flags, OF_LIGHT_2)) {
			amt = 2;
		} else if (of_has(obj->flags, OF_LIGHT_3)) {
			amt = 3;
		} else if (of_has(obj->flags, OF_LIGHT_4)) {
			amt = 4;
		} else if (of_has(obj->flags, OF_LIGHT_5)) {
			amt = 5;
		}
		amt += obj->modifiers[OBJ_MOD_LIGHT];

		/* Adjustment to allow UNLIGHT players to use +1 LIGHT gear */
		if ((obj->modifiers[OBJ_MOD_LIGHT] > 0) && player_has(p, PF_UNLIGHT)) {
			amt--;
		}

		/* Examine actual lights */
		if (tval_is_light(obj) && !of_has(obj->flags, OF_NO_FUEL) &&
				obj->timeout == 0)
			/* Lights without fuel provide no light */
			amt = 0;

		/* Alter p->state.cur_light if reasonable */
	    state->cur_light += amt;
	}
}

/**
 * Populates `chances` with the player's chance of digging through
 * the diggable terrain types in one turn out of 1600.
 */
void calc_digging_chances(struct player_state *state, int chances[DIGGING_MAX])
{
	int i;

	chances[DIGGING_RUBBLE] = state->skills[SKILL_DIGGING] * 8;
	chances[DIGGING_MAGMA] = (state->skills[SKILL_DIGGING] - 10) * 4;
	chances[DIGGING_QUARTZ] = (state->skills[SKILL_DIGGING] - 20) * 2;
	chances[DIGGING_GRANITE] = (state->skills[SKILL_DIGGING] - 40) * 1;
	/* Approximate a 1/1200 chance per skill point over 30 */
	chances[DIGGING_DOORS] = (state->skills[SKILL_DIGGING] * 4 - 119) / 3;

	/* Don't let any negative chances through */
	for (i = 0; i < DIGGING_MAX; i++)
		chances[i] = MAX(0, chances[i]);
}



/**
 * Calculate the blows a player would get.
 *
 * \param obj is the object for which we are calculating blows
 * \param state is the player state for which we are calculating blows
 * \param extra_blows is the number of +blows (x100) available from this object and
 * this state
 *
 * N.B. state->num_blows is now 100x the number of blows.
 */
int calc_blows(struct player *p, const struct object *obj,
			   struct player_state *state, int extra_blows)
{
	int blows;
	int str_index, dex_index;
	int div;
	int blow_energy;

	int weight = (obj == NULL) ? 0 : (obj->weight / 45);
	int min_weight = MAX(1, p->class->min_weight);

	/* Enforce a minimum "weight" (tenth pounds) */
	div = (weight < min_weight) ? min_weight : weight;

	/* Get the strength vs weight */
	str_index = adj_str_blow[state->stat_ind[STAT_STR]] *
			p->class->att_multiply / div;

	/* Maximal value */
	if (str_index >= (int)(sizeof(blows_table)/sizeof(blows_table[0])))
		str_index = (int)(sizeof(blows_table)/sizeof(blows_table[0])) - 1;

	/* Index by dexterity */
	dex_index = state->stat_ind[STAT_DEX];
	assert(dex_index < STAT_RANGE);

	/* Use the blows table to get energy per blow */
	blow_energy = blows_table[str_index][dex_index];
	blows = MIN((10000 / blow_energy), (100 * p->class->max_attacks));

	/* Require at least one blow, two for O-combat */
	return MAX(blows + extra_blows, OPT(p, birth_percent_damage) ? 200 : 100);
}


/**
 * Computes current weight limit.
 * This is the point at which speed penalties start - 75kg for a very strong player, 45kg
 * for a moderately (18) strong player, 7.5kg for a wimp (3)
 * At twice this, there is a -10 penalty to speed
 * At 3 times, a -40 penalty
 * At 4 times, a -160 penalty
 * No more than 4 times this can be carried
 */
int weight_limit(struct player_state *state)
{
	int i;

	/* Weight limit based only on strength */
	i = adj_str_wgt[state->stat_ind[STAT_STR]];

	/* Return the result */
	return (i);
}


/**
 * Computes weight remaining before burdened.
 */
int weight_remaining(struct player *p)
{
	int i;

	/* Weight limit based only on strength */
	i = weight_limit(&p->state) - p->upkeep->total_weight;

	/* Return the result */
	return (i);
}


/**
 * Adjust a value by a relative factor of the absolute value.  Mimics the
 * inline calculations of value = (value * (den + num)) / num when value is
 * positive.
 * \param v Is a pointer to the value to adjust.
 * \param num Is the numerator of the relative factor.  Use a negative value
 * for a decrease in the value, and a positive value for an increase.
 * \param den Is the denominator for the relative factor.  Must be positive.
 * \param minv Is the minimum absolute value of v to use when computing the
 * adjustment; use zero for this to get a pure relative adjustment.  Must be
 * be non-negative.
 */
static void adjust_skill_scale(int *v, int num, int den, int minv)
{
	if (num >= 0) {
		*v += (MAX(minv, ABS(*v)) * num) / den;
	} else {
		/*
		 * To mimic what (value * (den * num)) / num would give for
		 * positive value, need to round up the adjustment.
		 */
		*v -= (MAX(minv, ABS(*v)) * -num + den - 1) / den;
	}
}


/**
 * Calculate the effect of a shapechange on player state
 */
static void calc_shapechange(struct player_state *state,
							 struct player_shape *shape,
							 int *blows, int *shots, int *might, int *moves)
{
	int i;

	/* Combat stats */
	state->to_a += shape->to_a;
	state->to_h += shape->to_h;
	state->to_d += shape->to_d;

	/* Skills */
	for (i = 0; i < SKILL_MAX; i++) {
		state->skills[i] += shape->skills[i];
	}

	/* Object flags */
	of_union(state->flags, shape->flags);

	/* Stats */
	for (i = 0; i < STAT_MAX; i++) {
		state->stat_add[i] += shape->modifiers[i];
	}

	/* Other modifiers */
	state->skills[SKILL_STEALTH] += shape->modifiers[OBJ_MOD_STEALTH];
	state->skills[SKILL_SEARCH] += (shape->modifiers[OBJ_MOD_SEARCH] * 5);
	state->see_infra += shape->modifiers[OBJ_MOD_INFRA];
	state->skills[SKILL_DIGGING] += (shape->modifiers[OBJ_MOD_TUNNEL] * 20);
	state->dam_red += shape->modifiers[OBJ_MOD_DAM_RED];
	*blows += shape->modifiers[OBJ_MOD_BLOWS];
	*shots += shape->modifiers[OBJ_MOD_SHOTS];
	*might += shape->modifiers[OBJ_MOD_MIGHT];
	*moves += shape->modifiers[OBJ_MOD_MOVES];

	/* Resists and vulnerabilities */
	for (i = 0; i < ELEM_MAX; i++) {
		if (state->el_info[i].res_level == 0) {
			/* Simple, just apply shape res/vuln */
			state->el_info[i].res_level = shape->el_info[i].res_level;
		} else if (state->el_info[i].res_level == -1) {
			/* Shape resists cancel, immunities trump, vulnerabilities */
			if (shape->el_info[i].res_level == 1) {
				state->el_info[i].res_level = 0;
			} else if (shape->el_info[i].res_level == IMMUNITY) {
				state->el_info[i].res_level = IMMUNITY;
			}
		} else if (state->el_info[i].res_level == 1) {
			/* Shape vulnerabilities cancel, immunities enhance, resists */
			if (shape->el_info[i].res_level == -1) {
				state->el_info[i].res_level = 0;
			} else if (shape->el_info[i].res_level == IMMUNITY) {
				state->el_info[i].res_level = IMMUNITY;
			}
		} else if (state->el_info[i].res_level == IMMUNITY) {
			/* Immunity, shape has no effect */
		}
	}

}

static void apply_modifiers(struct player *p, struct player_state *state, s16b *modifiers, int *extra_blows, int *extra_shots, int *extra_might, int *extra_moves)
{
	for(int i=0;i<STAT_MAX;i++)
		state->stat_add[i] += modifiers[OBJ_MOD_STR + i]
			* p->obj_k->modifiers[OBJ_MOD_STR + i];
	state->skills[SKILL_STEALTH] += modifiers[OBJ_MOD_STEALTH]
		* p->obj_k->modifiers[OBJ_MOD_STEALTH];
	state->skills[SKILL_SEARCH] += (modifiers[OBJ_MOD_SEARCH] * 5)
		* p->obj_k->modifiers[OBJ_MOD_SEARCH];

	state->see_infra += modifiers[OBJ_MOD_INFRA]
		* p->obj_k->modifiers[OBJ_MOD_INFRA];

	state->skills[SKILL_DIGGING] += (modifiers[OBJ_MOD_TUNNEL]
		* p->obj_k->modifiers[OBJ_MOD_TUNNEL] * 20);
	state->dam_red += modifiers[OBJ_MOD_DAM_RED]
		* p->obj_k->modifiers[OBJ_MOD_DAM_RED];
	*extra_blows += modifiers[OBJ_MOD_BLOWS]
		* p->obj_k->modifiers[OBJ_MOD_BLOWS];
	*extra_shots += modifiers[OBJ_MOD_SHOTS]
		* p->obj_k->modifiers[OBJ_MOD_SHOTS];
	*extra_might += modifiers[OBJ_MOD_MIGHT]
		* p->obj_k->modifiers[OBJ_MOD_MIGHT];
	*extra_moves += modifiers[OBJ_MOD_MOVES]
		* p->obj_k->modifiers[OBJ_MOD_MOVES];
}

/* Return the + to a given stat from classes stat bonuses, mixed according to their weights.
 */
int class_to_stat(int stat)
{
	int total = 0;

	/* Sum */
	for(int i=1; i<=player->max_lev; i++)
		total += get_class_by_idx(player->lev_class[i])->c_adj[stat];

	/* Round to nearest */
	total *= 2;
	total += player->max_lev;
	return total / (player->max_lev * 2);
}

static int effective_ac_of(struct object *obj, int slot)
{
	const int wlev = levels_in_class(get_class_by_name("Wrestler")->cidx);
	/* No bonus if not a wrestler or using a melee weapon */

	if ((!wlev) || (equipped_item_by_slot_name(player, "weapon")))
		return obj ? obj->ac : 0;
	int ac = obj ? obj->ac : 0;

	/* Increase AC for wrestlers */
	int weight = slot_table[slot].weight;
	if (weight) {
		/* At the specified weight, there is no AC gain */
		int armweight = obj ? obj->weight : 0;
		if (armweight < weight) {
			int bonus = slot_table[slot].ac;	/* AC if slot is empty and max level */
			bonus *= (wlev * (weight - armweight));
			bonus /= (PY_MAX_LEVEL * weight);
			if (obj && obj->to_h < 0)
				bonus >>= -obj->to_h;
			ac += bonus;
		}
	}

	return ac;
}

/** Return the current burden weight, grams */
int burden_weight(struct player *p)
{
	int j = p->upkeep->total_weight;

	/* Add ballast if you are dragging a plane
	 * (= you have flying talents, but are not flying)
	 * Weight is guessed from the number of talents (this could be moved out to the ability.txt)
	 * Only do this for Pilot-only talents (there are other ways to fly such as the Super ability "Flight".
	 * "Superman doesn't need no seatbelt" => "Superman doesn't need no airplane!")
	 **/
	if (!player->flying) {
		int flying_talents = 0;
		for (int i=0;i<PF_MAX;i++) {
			if (ability[i]) {
				if (player_has(p, i)) {
					if (ability[i]->flags & AF_FLYING) {
						if (ability[i]->class && (streq(ability[i]->class, "Pilot")))
							flying_talents++;
					}
				}
			}
		}
		/* 40kg for basic, 50kg for mid-level, 60kg for top-tier */
		if (flying_talents) {
			j += (30000 + (10000 * flying_talents));
		}
	}

	return j;
}

/**
 * Calculate the players current "state", taking into account
 * not only race/class intrinsics, but also objects being worn
 * and temporary spell effects.
 *
 * See also calc_hitpoints().
 *
 * The "weapon" and "gun" do *not* add to the bonuses to hit or to
 * damage, since that would affect non-combat things.  These values
 * are actually added in later, at the appropriate place.
 *
 * If known_only is true, calc_bonuses() will only use the known
 * information of objects; thus it returns what the player _knows_
 * the character state to be.
 */
void calc_bonuses(struct player *p, struct player_state *state, bool known_only,
				  bool update)
{
	int i, j, hold;
	int extra_blows = 0;
	int extra_shots = 0;
	int extra_might = 0;
	int extra_moves = 0;
	struct object *launcher = equipped_item_by_slot_name(p, "shooting");
	struct object *weapon = equipped_item_by_slot_name(p, "weapon");
	bitflag f[OF_SIZE];
	bitflag collect_f[OF_SIZE];
	bool vuln[ELEM_MAX];
	char mom_speed[MOM_SPEED_MAX];
	memset(mom_speed, 0, sizeof(mom_speed));

	/* Hack to allow calculating hypothetical blows for extra STR, DEX - NRM */
	int str_ind = state->stat_ind[STAT_STR];
	int dex_ind = state->stat_ind[STAT_DEX];

	/* Reset */
	memset(state, 0, sizeof *state);

	/* Run special hooks */
	player_hook(calc, state);

	/* Set various defaults */
	state->speed = 110;
	state->num_blows = 100;

	/* Extract race/class info */
	state->see_infra = p->race->infra + p->extension->infra + p->personality->infra;
	for (i = 0; i < SKILL_MAX; i++) {
		state->skills[i] = p->race->r_skills[i]	+ p->extension->r_skills[i]	+ p->personality->r_skills[i];
	}
	for (i = 0; i < ELEM_MAX; i++) {
		vuln[i] = false;
		if (p->race->el_info[i].res_level + p->extension->el_info[i].res_level + p->personality->el_info[i].res_level <= -1) {
			vuln[i] = true;
		} else {
			state->el_info[i].res_level = p->race->el_info[i].res_level + p->extension->el_info[i].res_level + p->personality->el_info[i].res_level;
		}
	}

	/* Modify skills from level/class.
	 * The per-level skill (x_skills) is proportional directly to the number of levels you have in that class.
	 * The base skill (c_skills) is instead proportional to the fraction of total levels that you have in that class.
	 **/
	for (i = 0; i < SKILL_MAX; i++) {
		double total = 0;
		for (struct player_class *c = classes; c; c = c->next) {
			int levels = levels_in_class(c->cidx);
			if (levels) {
				double x_skill = c->x_skills[i] * levels;
				x_skill /= 10;
				double c_skill = c->c_skills[i] * levels;
				c_skill /= p->lev;
				total += x_skill;
				total += c_skill;
			}
		}
		state->skills[i] += total;
	}

	/* Base pflags = from player only, ignoring equipment and timed effects */
	pf_wipe(state->pflags_base);
	pf_copy(state->pflags_base, p->race->pflags[player->lev]);
	pf_union(state->pflags_base, p->extension->pflags[player->lev]);
	pf_union(state->pflags_base, p->personality->pflags[player->lev]);

	for (struct player_class *c = classes; c; c = c->next) {
		int levels = levels_in_class(c->cidx);
		if (levels)
			pf_union(state->pflags_base, c->pflags[levels]);
	}

	pf_union(state->pflags_base, p->ability_pflags);
	pf_union(state->pflags_base, p->shape->pflags);

	/* Remove cancelled flags.
	 * Use state->pflags_base not the more general player_has, to avoid being
	 * dependent on the last run (and so ending up with a glitch on the first
	 * turn after restoring, for example)
	 **/
	bool cancel[PF_MAX];
	memset(cancel, 0, sizeof(cancel));
	for (i = 1; i < PF_MAX; i++) {
		if (ability[i]) {
			if (pf_has(state->pflags_base, i)) {
				for (j = 0; j < PF_MAX; j++)
					cancel[j] |= ability[i]->cancel[j];
			}
		}
	}
	for (i = 1; i < PF_MAX; i++) {
		if (cancel[i])
			pf_off(state->pflags_base, i);
	}

	/* Extract the player flags */
	player_flags(p, collect_f);

	/* Analyze equipment for player flags */
	pf_wipe(state->pflags_equip);

	for (i = 0; i < p->body.count; i++) {
		int index = 0;
		struct object *obj = slot_object(p, i);
		struct fault_data *fault = obj ? obj->faults : NULL;

		while (obj) {
			/* Extract player flags */
			pf_union(state->pflags_equip, obj->pflags);

			/* Move to any unprocessed fault object */
			if (fault) {
				index++;
				obj = NULL;
				while (index < z_info->fault_max) {
					if (fault[index].power) {
						obj = faults[index].obj;
						break;
					} else {
						index++;
					}
				}
			} else {
				obj = NULL;
			}
		}
	}

	/* Extract from timed conditions */
	pf_wipe(state->pflags_temp);
	for (i = 1; i < PF_MAX; i++) {
		if (player->timed[TMD_PF + i]) {
			pf_on(state->pflags_temp, i);
		}
	}

	/* Extract forbids - FIXME, this looks slow. Cache it in state? */
	bool forbid[PF_MAX];
	memset(forbid, 0, sizeof(forbid));
	bitflag has_flag[PF_SIZE];
	pf_copy(has_flag, state->pflags_temp);
	pf_union(has_flag, state->pflags_equip);
	pf_union(has_flag, state->pflags_base);
	for (i = 1; i < PF_MAX; i++) {
		if (ability[i]) {
			if (pf_has(has_flag, i)) {
				for (j = 0; j < PF_MAX; j++) {
					if (ability[j]) {
						if (ability[i]->forbid[j]) {
							forbid[j] = true;
						}
					}
				}
			}
		}
	}

	/* Apply forbids to equipment and temporary flags */
	for (i = 1; i < PF_MAX; i++) {
		if (forbid[i]) {
			pf_off(state->pflags_equip, i);
			pf_off(state->pflags_temp, i);
		}
	}

	/* Combine base, temporary and equipment pflags. This must be done early, so that
	 * "Extract from abilities" knows about all flags.
	 **/
	pf_copy(state->pflags, state->pflags_base);
	pf_union(state->pflags, state->pflags_equip);
	pf_union(state->pflags, state->pflags_temp);

	/* Extract from abilities */
	state->ac = player->race->ac + player->extension->ac + player->personality->ac;
	for (i = 1; i < PF_MAX; i++) {
		if (ability[i]) {
			if (player_has(p, i)) {
				if ((player->flying) || (!(ability[i]->flags & AF_FLYING))) {
					state->ac += ability[i]->ac;
					state->to_h += ability[i]->tohit;
					state->to_d += ability[i]->todam;

					/* Add to both base and combined pflags */
					pf_union(state->pflags_base, ability[i]->pflags); 
					pf_union(state->pflags, ability[i]->pflags);

					of_union(collect_f, ability[i]->oflags);

					/* Sum abilities giving speed based on momentum */
					if ((bool)ability[i]->mom_speed) {
						for(j = 0; j < MOM_SPEED_MAX; j++)
							mom_speed[j] += ability[i]->mom_speed[j];
					}

					/* Apply element info, noting vulnerabilities for later processing */
					for (j = 0; j < ELEM_MAX; j++) {
						if (ability[i]->el_info[j].res_level) {
							if (ability[i]->el_info[j].res_level == -1)
								vuln[j] = true;

							/* OK because res_level hasn't included vulnerability yet */
							if (ability[i]->el_info[j].res_level > state->el_info[j].res_level)
								state->el_info[j].res_level = ability[i]->el_info[j].res_level;

							/* Apply modifiers */
							apply_modifiers(p, state, ability[i]->modifiers, &extra_blows, &extra_shots, &extra_might, &extra_moves);
						}
					}
				}
			}
		}
	}

	/* Analyze equipment */
	for (i = 0; i < p->body.count; i++) {
		int index = 0;
		struct object *obj = slot_object(p, i);
		struct fault_data *fault = obj ? obj->faults : NULL;

		/* Apply AC bonus even if there is no object, because Wrestlers */
		if (!obj)
			state->ac += effective_ac_of(obj, i);

		while (obj) {
			int dig = 0;

			/* Extract the item flags */
			if (known_only) {
				object_flags_known(obj, f);
			} else {
				object_flags(obj, f);
			}
			of_union(collect_f, f);

			/* Apply modifiers */
			apply_modifiers(p, state, obj->modifiers, &extra_blows, &extra_shots, &extra_might, &extra_moves);

			if (tval_is_digger(obj)) {
				if (of_has(obj->flags, OF_DIG_1))
					dig = 1;
				else if (of_has(obj->flags, OF_DIG_2))
					dig = 2;
				else if (of_has(obj->flags, OF_DIG_3))
					dig = 3;
			}
			state->skills[SKILL_DIGGING] += dig * p->obj_k->modifiers[OBJ_MOD_TUNNEL] * 20;
			/* Apply element info, noting vulnerabilites for later processing */
			for (j = 0; j < ELEM_MAX; j++) {
				if (!known_only || obj->known->el_info[j].res_level) {
					if (obj->el_info[j].res_level == -1)
						vuln[j] = true;

					/* OK because res_level hasn't included vulnerability yet */
					if (obj->el_info[j].res_level > state->el_info[j].res_level)
						state->el_info[j].res_level = obj->el_info[j].res_level;
				}
			}

			/* Apply combat bonuses */
			state->ac += effective_ac_of(obj, i);
			if (!known_only || obj->known->to_a)
				state->to_a += obj->to_a;

			if (!slot_type_is(p, i, EQUIP_WEAPON) && !slot_type_is(p, i, EQUIP_GUN)) {
				if (!known_only || obj->known->to_h) {
					state->to_h += obj->to_h;
				}
				if (!known_only || obj->known->to_d) {
					state->to_d += obj->to_d;
				}
			}

			/* Move to any unprocessed fault object */
			if (fault) {
				index++;
				obj = NULL;
				while (index < z_info->fault_max) {
					if (fault[index].power) {
						obj = faults[index].obj;
						break;
					} else {
						index++;
					}
				}
			} else {
				obj = NULL;
			}
		}
	}

	/* Analyze gear */
	for (struct object *obj = player->gear; obj; obj = obj->next) {
		if (!object_is_equipped(player->body, obj)) {
			/* Extract the item carried flags */
			if (known_only) {
				object_carried_flags_known(obj, f);
			} else {
				object_carried_flags(obj, f);
			}
			of_union(collect_f, f);
		}
	}

	/* Apply the collected flags */
	of_union(state->flags, collect_f);

	/* Remove flags, where abilities have a remove set */
	for (i = 1; i < PF_MAX; i++) {
		if (ability[i]) {
			if (player_has(p, i)) {
				of_diff(state->flags, ability[i]->oflags_off);
			}
		}
	}

	/* Now deal with vulnerabilities */
	for (i = 0; i < ELEM_MAX; i++) {
		if (vuln[i] && (state->el_info[i].res_level < IMMUNITY))
			state->el_info[i].res_level--;
	}

	/* Add shapechange info */
	calc_shapechange(state, p->shape, &extra_blows, &extra_shots, &extra_might,
		&extra_moves);

	/* Calculate light */
	calc_light(p, state, update);

	/* Unlight - needs change if anything but resist is introduced for dark */
	if (player_has(p, PF_UNLIGHT) && character_dungeon) {
		state->el_info[ELEM_DARK].res_level = 1;
	}

	/* Calculate the various stat values */
	for (i = 0; i < STAT_MAX; i++) {
		int add, use, ind;

		add = state->stat_add[i];
		add += (p->race->r_adj[i] + p->extension->r_adj[i] + p->personality->r_adj[i] + class_to_stat(i));
		add += ability_to_stat(i);
		state->stat_top[i] =  modify_stat_value(p->stat_max[i], add);
		use = modify_stat_value(p->stat_cur[i], add);

		state->stat_use[i] = use;

		if (use <= 3) {/* Values: n/a */
			ind = 0;
		} else if (use <= 18) {/* Values: 3, 4, ..., 18 */
			ind = (use - 3);
		} else if (use <= 18+219) {/* Ranges: 18/00-18/09, ..., 18/210-18/219 */
			ind = (15 + (use - 18) / 10);
		} else {/* Range: 18/220+ */
			ind = (37);
		}

		assert((0 <= ind) && (ind < STAT_RANGE));

		/* Hack for hypothetical blows - NRM */
		if (!update) {
			if (i == STAT_STR) {
				ind += str_ind;
				ind = MIN(ind, 37);
				ind = MAX(ind, 3);
			} else if (i == STAT_DEX) {
				ind += dex_ind;
				ind = MIN(ind, 37);
				ind = MAX(ind, 3);
			}
		}

		/* Save the new index */
		state->stat_ind[i] = ind;
	}

	/* Modify skills from stats */
	state->skills[SKILL_DISARM_PHYS] += adj_dex_dis[state->stat_ind[STAT_DEX]];
	state->skills[SKILL_DISARM_MAGIC] += adj_int_dis[state->stat_ind[STAT_INT]];
	state->skills[SKILL_DEVICE] += adj_int_dev[state->stat_ind[STAT_INT]];
	state->skills[SKILL_SAVE] += adj_wis_sav[state->stat_ind[STAT_WIS]];
	state->skills[SKILL_DIGGING] += adj_str_dig[state->stat_ind[STAT_STR]];
	state->speed += (state->stat_ind[STAT_SPD] - 7);

	/* Effects of food outside the "Fed" range */
	if (!player_timed_grade_eq(p, TMD_FOOD, "Fed")) {
		int excess = p->timed[TMD_FOOD] - PY_FOOD_FULL;
		if ((excess > 0) && !p->timed[TMD_ATT_VAMP]) {
			/* Scale to units 1/10 of the range and subtract from speed */
			excess = (excess * 10) / (PY_FOOD_MAX - PY_FOOD_FULL);
			/* If you don't eat food, while you can still be "overcharged" and
			 * use up energy fast you should not slow down.
			 */
			if (!player_has(p, PF_NO_FOOD))
				state->speed -= excess;
		} else if (p->timed[TMD_FOOD] < PY_FOOD_WEAK) {
			/* Scale to units 1/20 of the range */
			int lack = ((PY_FOOD_WEAK - p->timed[TMD_FOOD]) * 20) / PY_FOOD_WEAK;
			if (!player_has(player, PF_FORAGING)) {
				/* Apply effects progressively */
				state->stat_add[STAT_STR] -= 1 + (lack / 2);
				if ((lack > 0) && (lack <= 10)) {
					adjust_skill_scale(&state->skills[SKILL_DEVICE],
						-1, 10, 0);
					state->to_h -= (lack + 1) / 2;
				} else if ((lack > 10) && (lack <= 16)) {
					adjust_skill_scale(&state->skills[SKILL_DEVICE],
						-1, 5, 0);
					state->skills[SKILL_DISARM_PHYS] *= 9;
					state->skills[SKILL_DISARM_PHYS] /= 10;
					state->skills[SKILL_DISARM_MAGIC] *= 9;
					state->skills[SKILL_DISARM_MAGIC] /= 10;
					state->to_h -= 6;
				} else if (lack > 16) {
					adjust_skill_scale(&state->skills[SKILL_DEVICE],
						-3, 10, 0);
					state->skills[SKILL_DISARM_PHYS] *= 8;
					state->skills[SKILL_DISARM_PHYS] /= 10;
					state->skills[SKILL_DISARM_MAGIC] *= 8;
					state->skills[SKILL_DISARM_MAGIC] /= 10;
					state->skills[SKILL_SAVE] *= 9;
					state->skills[SKILL_SAVE] /= 10;
					state->skills[SKILL_SEARCH] *=9;
					state->skills[SKILL_SEARCH] /= 10;
					state->to_h -= 7;
				}
			}
		}
	}

	/* Other timed effects */
	player_flags_timed(p, state->flags);

	if (player_timed_grade_eq(p, TMD_STUN, "Heavy Stun")) {
		state->to_h -= 20;
		state->to_d -= 20;
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 5, 0);
		if (update) {
			p->timed[TMD_FASTCAST] = 0;
		}
	} else if (player_timed_grade_eq(p, TMD_STUN, "Stun")) {
		state->to_h -= 5;
		state->to_d -= 5;
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 10, 0);
		if (update) {
			p->timed[TMD_FASTCAST] = 0;
		}
	}
	if (p->timed[TMD_INVULN]) {
		state->to_a += 100;
	}
	if (p->timed[TMD_BLESSED]) {
		state->to_a += 5;
		state->to_h += 10;
		adjust_skill_scale(&state->skills[SKILL_DEVICE], 1, 20, 0);
	}
	if (p->timed[TMD_SHIELD]) {
		state->to_a += 50;
	}
	if (p->timed[TMD_HERO]) {
		state->to_h += 12;
		adjust_skill_scale(&state->skills[SKILL_DEVICE], 1, 20, 0);
	}
	if (p->timed[TMD_SHERO]) {
		state->skills[SKILL_TO_HIT_MELEE] += 75;
		state->skills[SKILL_TO_HIT_MARTIAL] += 90;
		state->to_a -= 10;
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 10, 0);
	}
	if (p->timed[TMD_FAST] || p->timed[TMD_SPRINT]) {
		state->speed += 10;
	}
	if (p->timed[TMD_SLOW]) {
		state->speed -= 10;
	}
	if (p->timed[TMD_SINFRA]) {
		state->see_infra += 5;
	}
	if (p->timed[TMD_TERROR]) {
		state->speed += 10;
	}
	if (p->timed[TMD_OPP_ACID] && (state->el_info[ELEM_ACID].res_level < IMMUNITY-1)) {
			state->el_info[ELEM_ACID].res_level++;
	}
	if (p->timed[TMD_OPP_ELEC] && (state->el_info[ELEM_ELEC].res_level < IMMUNITY-1)) {
			state->el_info[ELEM_ELEC].res_level++;
	}
	if (p->timed[TMD_OPP_FIRE] && (state->el_info[ELEM_FIRE].res_level < IMMUNITY-1)) {
			state->el_info[ELEM_FIRE].res_level++;
	}
	if (p->timed[TMD_OPP_COLD] && (state->el_info[ELEM_COLD].res_level < IMMUNITY-1)) {
			state->el_info[ELEM_COLD].res_level++;
	}
	if (p->timed[TMD_OPP_POIS] && (state->el_info[ELEM_POIS].res_level < IMMUNITY-1)) {
			state->el_info[ELEM_POIS].res_level++;
	}
	if (p->timed[TMD_CONFUSED]) {
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 4, 0);
	}
	if (p->timed[TMD_AMNESIA]) {
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 5, 0);
	}
	if (p->timed[TMD_POISONED]) {
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 20, 0);
	}
	if (p->timed[TMD_INFECTED]) {
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 20, 0);
	}
	if (p->timed[TMD_IMAGE]) {
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 5, 0);
	}
	if (p->timed[TMD_BLOODLUST]) {
		state->to_d += p->timed[TMD_BLOODLUST] / 2;
		extra_blows += p->timed[TMD_BLOODLUST] / 20;
	}
	if (p->timed[TMD_STEALTH]) {
		state->skills[SKILL_STEALTH] += 10;
	}

	/* Wrestlers get extra blows, + to hit, and + to damage */
	extra_blows *= 100;
	const int wlev = levels_in_class(get_class_by_name("Wrestler")->cidx);
	if ((wlev) && (!equipped_item_by_slot_name(player, "weapon"))) {
		extra_blows += (wlev * 5) + ((wlev * wlev) / 10);
		state->to_d += wlev;
		state->to_h += wlev / 3;
	}

	/* Pilots (or anyone with a suitable ability) get extra speed */
	if (player->flying)
		state->speed += mom_speed[MIN((size_t)player->momentum, sizeof(mom_speed)-1)];

	/* Analyze flags - check for fear */
	if (of_has(state->flags, OF_AFRAID)) {
		state->to_h -= 20;
		state->to_a += 8;
		adjust_skill_scale(&state->skills[SKILL_DEVICE], -1, 20, 0);
	}

	/* Analyze weight */
	j = burden_weight(p);

	i = weight_limit(state);
	int burden = ((j * BURDEN_RANGE) / i) - BURDEN_RANGE;
	if (burden >= 0) {
		if (burden >= (int)sizeof(adj_wgt_speed))
			burden = sizeof(adj_wgt_speed) - 1;
		state->speed -= adj_wgt_speed[burden];
	}
	if (state->speed < 0)
		state->speed = 0;
	if (state->speed > 199)
		state->speed = 199;

	/* Apply modifier bonuses (Un-inflate stat bonuses) */
	state->to_a += adj_dex_ta[state->stat_ind[STAT_DEX]];
	state->to_d += adj_str_td[state->stat_ind[STAT_STR]];
	state->to_h += adj_dex_th[state->stat_ind[STAT_DEX]];
	state->to_h += adj_str_th[state->stat_ind[STAT_STR]];

	/* Bound skills */
	if (state->skills[SKILL_DIGGING] < 1) state->skills[SKILL_DIGGING] = 1;
	if (state->skills[SKILL_STEALTH] > 30) state->skills[SKILL_STEALTH] = 30;
	if (state->skills[SKILL_STEALTH] < 0) state->skills[SKILL_STEALTH] = 0;
	hold = adj_str_hold[state->stat_ind[STAT_STR]];


	/* Analyze launcher */
	state->heavy_shoot = false;
	if (launcher) {
		if (hold < launcher->weight / 454) {
			state->to_h += 2 * (hold - launcher->weight / 454);
			state->heavy_shoot = true;
		}

		state->num_shots = 10;

		/* Type of ammo */
		if (kf_has(launcher->kind->kind_flags, KF_SHOOTS_6MM))
			state->ammo_tval = TV_AMMO_6;
		else if (kf_has(launcher->kind->kind_flags, KF_SHOOTS_9MM))
			state->ammo_tval = TV_AMMO_9;
		else if (kf_has(launcher->kind->kind_flags, KF_SHOOTS_12MM))
			state->ammo_tval = TV_AMMO_12;

		/* Multiplier */
		state->ammo_mult = launcher->pval;

		/* Apply special flags */
		if (!state->heavy_shoot) {
			state->num_shots += extra_shots;
			state->ammo_mult += extra_might;

			/* Ranger style fast shooting */
			if (player_has(p, PF_FAST_SHOT)) {
				state->num_shots += p->lev / 3;
			}

			/* Marksman style (specific types only)
			 * The bonus is small at the point you can first gain it,
			 * and more powerful weapon classes get less speed (and
			 * more accuracy, but that's not intended to make up for
			 * it - it's supposed to make it more of a real choice
			 * on which weapon class to specialise in).
			 * Assumes that you can gain rifle/handgun at level 5,
			 * the caliber at level 15.
			 **/
			bool rifle = (randcalc(launcher->kind->pval, 0, AVERAGE) >= 3);	/* XXX This may change */
			int shots = 0;
			int tohit = 0;
			if (rifle && (player_has(p, PF_RIFLE_SPECIALIST))) {
				shots = 1 + ((p->lev - 5) / 3);
				tohit = (p->lev - 5) / 4;
				if ((player_has(p, PF_6MM_RIFLE_SPECIALIST)) && (state->ammo_tval == TV_AMMO_6)) {
					shots += (p->lev - 15) / 3;
					tohit += (p->lev - 15) / 3;
				} else if ((player_has(p, PF_9MM_RIFLE_SPECIALIST)) && (state->ammo_tval == TV_AMMO_9)) {
					shots += (p->lev - 15) / 5;
					tohit += (p->lev - 15) / 5;
				} else if ((player_has(p, PF_12MM_RIFLE_SPECIALIST)) && (state->ammo_tval == TV_AMMO_12)) {
					shots += (p->lev - 15) / 7;
					tohit += (p->lev - 15) / 7;
				}
			} else if (!rifle && (player_has(p, PF_HANDGUN_SPECIALIST))) {
				shots = 1 + ((p->lev - 5) / 2);
				tohit = (p->lev - 5) / 7;
				if ((player_has(p, PF_6MM_RIFLE_SPECIALIST)) && (state->ammo_tval == TV_AMMO_6)) {
					shots += (p->lev - 15) / 2;
					tohit += (p->lev - 15) / 4;
				} else if ((player_has(p, PF_9MM_RIFLE_SPECIALIST)) && (state->ammo_tval == TV_AMMO_9)) {
					shots += (p->lev - 15) / 4;
					tohit += (p->lev - 15) / 7;
				} else if ((player_has(p, PF_12MM_RIFLE_SPECIALIST)) && (state->ammo_tval == TV_AMMO_12)) {
					shots += (p->lev - 15) / 6;
					tohit += (p->lev - 15) / 10;
				}
			}
			/* Just in case you somehow got the ability early */
			if (shots < 0)
				shots = 0;
			if (tohit < 0)
				tohit = 0;
			state->num_shots += shots;
			state->to_h += tohit;
		}

		/* Require at least one shot */
		if (state->num_shots < 10) state->num_shots = 10;
	}

	/* Analyze weapon */
	state->heavy_wield = false;
	state->bless_wield = false;
	if (weapon) {
		/* It is hard to hold a heavy weapon */
		if (hold < weapon->weight / 454) {
			state->to_h += 2 * (hold - weapon->weight / 454);
			state->heavy_wield = true;
		}

		/* Normal weapons */
		if (!state->heavy_wield) {
			state->num_blows = calc_blows(p, weapon, state, extra_blows);
			state->skills[SKILL_DIGGING] += (weapon->weight / 454);
		}
	} else {
		/* Unarmed */
		state->num_blows = calc_blows(p, NULL, state, extra_blows);
	}

	/* Movement speed */
	state->num_moves = extra_moves;

	return;
}

/**
 * Calculate bonuses, and print various things on changes.
 */
static void update_bonuses(struct player *p)
{
	int i;

	struct player_state state = p->state;
	struct player_state known_state = p->known_state;


	/* ------------------------------------
	 * Calculate bonuses
	 * ------------------------------------ */

	calc_bonuses(p, &state, false, true);
	calc_bonuses(p, &known_state, true, true);


	/* ------------------------------------
	 * Notice changes
	 * ------------------------------------ */

	/* Analyze stats */
	for (i = 0; i < STAT_MAX; i++) {
		/* Notice changes */
		if (state.stat_top[i] != p->state.stat_top[i])
			/* Redisplay the stats later */
			p->upkeep->redraw |= (PR_STATS);

		/* Notice changes */
		if (state.stat_use[i] != p->state.stat_use[i])
			/* Redisplay the stats later */
			p->upkeep->redraw |= (PR_STATS);

		/* Notice changes */
		if (state.stat_ind[i] != p->state.stat_ind[i]) {
			/* Change in CON affects Hitpoints */
			if (i == STAT_CON)
				p->upkeep->update |= (PU_HP);
		}
	}

	/* Hack -- Telepathy Change */
	if (of_has(state.flags, OF_TELEPATHY) !=
		of_has(p->state.flags, OF_TELEPATHY))
		/* Update monster visibility */
		p->upkeep->update |= (PU_MONSTERS);
	/* Hack -- See Invis Change */
	if (of_has(state.flags, OF_SEE_INVIS) !=
		of_has(p->state.flags, OF_SEE_INVIS))
		/* Update monster visibility */
		p->upkeep->update |= (PU_MONSTERS);

	/* Redraw speed (if needed) */
	if (state.speed != p->state.speed)
		p->upkeep->redraw |= (PR_SPEED);

	/* Redraw armor (if needed) */
	if ((known_state.ac != p->known_state.ac) || 
		(known_state.to_a != p->known_state.to_a))
		p->upkeep->redraw |= (PR_ARMOR);

	/* Notice changes in the "light radius" */
	if (p->state.cur_light != state.cur_light) {
		/* Update the visuals */
		p->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);
	}

	/* Notice changes to the weight limit. */
	if (weight_limit(&p->state) != weight_limit(&state)) {
		p->upkeep->redraw |= (PR_INVEN);
	}

	/* Hack -- handle partial mode */
	if (!p->upkeep->only_partial) {
		/* Take note when "heavy gun" changes */
		if (p->state.heavy_shoot != state.heavy_shoot) {
			/* Message */
			if (state.heavy_shoot)
				msg("You have trouble handling such a heavy gun.");
			else if (equipped_item_by_slot_name(p, "shooting"))
				msg("You have no trouble handling your gun.");
			else
				msg("You feel relieved to put down your heavy gun.");
		}

		/* Take note when "heavy weapon" changes */
		if (p->state.heavy_wield != state.heavy_wield) {
			/* Message */
			if (state.heavy_wield)
				msg("You have trouble wielding such a heavy weapon.");
			else if (equipped_item_by_slot_name(p, "weapon"))
				msg("You have no trouble wielding your weapon.");
			else
				msg("You feel relieved to put down your heavy weapon.");	
		}

		/* Take note when "illegal weapon" changes */
		if (p->state.bless_wield != state.bless_wield) {
			/* Message */
			if (state.bless_wield) {
				msg("You feel attuned to your weapon.");
			} else if (equipped_item_by_slot_name(p, "weapon")) {
				msg("You feel less attuned to your weapon.");
			}
		}

		/* Take note when "armor state" changes */
		if (p->state.cumber_armor != state.cumber_armor) {
			/* Message */
			if (state.cumber_armor)
				msg("The weight of your armor encumbers your movement.");
			else
				msg("You feel able to move more freely.");
		}
	}

	memcpy(&p->state, &state, sizeof(state));
	memcpy(&p->known_state, &known_state, sizeof(known_state));
}




/**
 * ------------------------------------------------------------------------
 * Monster and object tracking functions
 * ------------------------------------------------------------------------ */

/**
 * Track the given monster
 */
void health_track(struct player_upkeep *upkeep, struct monster *mon)
{
	upkeep->health_who = mon;
	upkeep->redraw |= PR_HEALTH;
}

/**
 * Track the given monster race
 */
void monster_race_track(struct player_upkeep *upkeep, struct monster_race *race)
{
	/* Save this monster ID */
	upkeep->monster_race = race;

	/* Window stuff */
	upkeep->redraw |= (PR_MONSTER);
}

/**
 * Track the given object
 */
void track_object(struct player_upkeep *upkeep, struct object *obj)
{
	upkeep->object = obj;
	upkeep->object_kind = NULL;
	upkeep->redraw |= (PR_OBJECT);
}

/**
 * Track the given object kind
 */
void track_object_kind(struct player_upkeep *upkeep, struct object_kind *kind)
{
	upkeep->object = NULL;
	upkeep->object_kind = kind;
	upkeep->redraw |= (PR_OBJECT);
}

/**
 * Cancel all object tracking
 */
void track_object_cancel(struct player_upkeep *upkeep)
{
	upkeep->object = NULL;
	upkeep->object_kind = NULL;
	upkeep->redraw |= (PR_OBJECT);
}

/**
 * Is the given item tracked?
 */
bool tracked_object_is(struct player_upkeep *upkeep, struct object *obj)
{
	return (upkeep->object == obj);
}



/**
 * ------------------------------------------------------------------------
 * Generic "deal with" functions
 * ------------------------------------------------------------------------ */

/**
 * Handle "player->upkeep->notice"
 */
void notice_stuff(struct player *p)
{
	/* Notice stuff */
	if (!p->upkeep->notice) return;

	/* Deal with ignore stuff */
	if (p->upkeep->notice & PN_IGNORE) {
		p->upkeep->notice &= ~(PN_IGNORE);
		ignore_drop(p);
	}

	/* Combine the pack */
	if (p->upkeep->notice & PN_COMBINE) {
		p->upkeep->notice &= ~(PN_COMBINE);
		combine_pack(p);
	}

	/* Dump the monster messages */
	if (p->upkeep->notice & PN_MON_MESSAGE) {
		p->upkeep->notice &= ~(PN_MON_MESSAGE);

		/* Make sure this comes after all of the monster messages */
		show_monster_messages();
	}
}

/**
 * Handle "player->upkeep->update"
 */
void update_stuff(struct player *p)
{
	/* Update stuff */
	if (!p->upkeep->update) return;


	if (p->upkeep->update & (PU_INVEN)) {
		p->upkeep->update &= ~(PU_INVEN);
		calc_inventory(p);
	}

	if (p->upkeep->update & (PU_BONUS)) {
		p->upkeep->update &= ~(PU_BONUS);
		update_bonuses(p);
	}

	if (p->upkeep->update & (PU_TORCH)) {
		p->upkeep->update &= ~(PU_TORCH);
		calc_light(p, &p->state, true);
	}

	if (p->upkeep->update & (PU_HP)) {
		p->upkeep->update &= ~(PU_HP);
		calc_hitpoints(p);
	}

	/* Character is not ready yet, no map updates */
	if (!character_generated) return;

	/* Map is not shown, no map updates */
	if (!map_is_visible()) return;

	if (p->upkeep->update & (PU_UPDATE_VIEW)) {
		p->upkeep->update &= ~(PU_UPDATE_VIEW);
		update_view(cave, p);
	}

	if (p->upkeep->update & (PU_DISTANCE)) {
		p->upkeep->update &= ~(PU_DISTANCE);
		p->upkeep->update &= ~(PU_MONSTERS);
		update_monsters(p, true);
	}

	if (p->upkeep->update & (PU_MONSTERS)) {
		p->upkeep->update &= ~(PU_MONSTERS);
		update_monsters(p, false);
	}

	if (p->upkeep->update & (PU_PANEL)) {
		p->upkeep->update &= ~(PU_PANEL);
		event_signal(EVENT_PLAYERMOVED);
	}
}



struct flag_event_trigger
{
	u32b flag;
	game_event_type event;
};



/**
 * Events triggered by the various flags.
 */
static const struct flag_event_trigger redraw_events[] =
{
	{ PR_MISC,    EVENT_RACE_CLASS },
	{ PR_TITLE,   EVENT_PLAYERTITLE },
	{ PR_LEV,     EVENT_PLAYERLEVEL },
	{ PR_EXP,     EVENT_EXPERIENCE },
	{ PR_STATS,   EVENT_STATS },
	{ PR_ARMOR,   EVENT_AC },
	{ PR_HP,      EVENT_HP },
	{ PR_GOLD,    EVENT_GOLD },
	{ PR_HEALTH,  EVENT_MONSTERHEALTH },
	{ PR_DEPTH,   EVENT_DUNGEONLEVEL },
	{ PR_SPEED,   EVENT_PLAYERSPEED },
	{ PR_STATE,   EVENT_STATE },
	{ PR_STATUS,  EVENT_STATUS },
	{ PR_DTRAP,   EVENT_DETECTIONSTATUS },
	{ PR_FEELING, EVENT_FEELING },
	{ PR_LIGHT,   EVENT_LIGHT },

	{ PR_INVEN,   EVENT_INVENTORY },
	{ PR_EQUIP,   EVENT_EQUIPMENT },
	{ PR_MONLIST, EVENT_MONSTERLIST },
	{ PR_ITEMLIST, EVENT_ITEMLIST },
	{ PR_MONSTER, EVENT_MONSTERTARGET },
	{ PR_OBJECT, EVENT_OBJECTTARGET },
	{ PR_MESSAGE, EVENT_MESSAGE },
};

/**
 * Handle "player->upkeep->redraw"
 */
void redraw_stuff(struct player *p)
{
	size_t i;
	u32b redraw = p->upkeep->redraw;

	/* Redraw stuff */
	if (!redraw) return;

	/* Character is not ready yet, no screen updates */
	if (!character_generated) return;

	/* Map is not shown, subwindow updates only */
	if (!map_is_visible()) 
		redraw &= PR_SUBWINDOW;

	/* Hack - rarely update while resting or running, makes it over quicker */
	if (((player_resting_count(p) % 100) || (p->upkeep->running % 100))
		&& !(redraw & (PR_MESSAGE | PR_MAP)))
		return;

	/* For each listed flag, send the appropriate signal to the UI */
	for (i = 0; i < N_ELEMENTS(redraw_events); i++) {
		const struct flag_event_trigger *hnd = &redraw_events[i];

		if (redraw & hnd->flag)
			event_signal(hnd->event);
	}

	/* Then the ones that require parameters to be supplied. */
	if (redraw & PR_MAP) {
		/* Mark the whole map to be redrawn */
		event_signal_point(EVENT_MAP, -1, -1);
	}

	p->upkeep->redraw &= ~redraw;

	/* Map is not shown, subwindow updates only */
	if (!map_is_visible()) return;

	/*
	 * Do any plotting, etc. delayed from earlier - this set of updates
	 * is over.
	 */
	event_signal(EVENT_END);
}


/**
 * Handle "player->upkeep->update" and "player->upkeep->redraw"
 */
void handle_stuff(struct player *p)
{
	if (p->upkeep->update) update_stuff(p);
	if (p->upkeep->redraw) redraw_stuff(p);
}

