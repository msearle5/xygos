/**
 * \file effect-handler-general.c
 * \brief Handler functions for general effects
 *
 * Copyright (c) 2007 Andi Sidwell
 * Copyright (c) 2016 Ben Semmler, Nick McConnell
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

#include "cave.h"
#include "effect-handler.h"
#include "game-input.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-desc.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "mon-predicate.h"
#include "mon-summon.h"
#include "mon-util.h"
#include "obj-chest.h"
#include "obj-fault.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-init.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-power.h"
#include "obj-randart.h"
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-ability.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-quest.h"
#include "player-timed.h"
#include "player-quest.h"
#include "player-util.h"
#include "project.h"
#include "source.h"
#include "target.h"
#include "trap.h"
#include "ui-command.h"
#include "ui-input.h"
#include "ui-knowledge.h"
#include "ui-menu.h"
#include "ui-output.h"
#include "ui-store.h"
#include "world.h"

#include <math.h>

static void shapechange(const char *shapename, bool verbose);
static int recyclable_blocks(const struct object *obj);
static bool brand_object(struct object *obj, const char *name);
static bool mundane_object(struct object *obj, bool silent);

/**
 * Set value for a chain of effects
 */
static int set_value = 0;

int effect_calculate_value(effect_handler_context_t *context, bool use_boost)
{
	int final = 0;

	if (set_value) {
		return set_value;
	}

	if (context->value.base > 0 ||
		(context->value.dice > 0 && context->value.sides > 0)) {
		final = context->value.base +
			damroll(context->value.dice, context->value.sides);
	}

	/* Device boost */
	if (use_boost) {
		final *= (100 + context->boost);
		final /= 100;
	}

	return final;
}

/**
 * Stat adjectives
 */
static const char *desc_stat(int stat, bool positive)
{
	struct obj_property *prop = lookup_obj_property(OBJ_PROPERTY_STAT, stat);
	if (positive) {
		return prop->adjective;
	}
	return prop->neg_adj;
}

/**
 * Check for monster targeting another monster
 */
struct monster *monster_target_monster(effect_handler_context_t *context)
{
	if (context->origin.what == SRC_MONSTER) {
		struct monster *mon = cave_monster(cave, context->origin.which.monster);
		if (!mon) return NULL;
		if (mon->target.midx > 0) {
			struct monster *t_mon = cave_monster(cave, mon->target.midx);
			assert(t_mon);
			return t_mon;
		}
	}
	return NULL;
}

/**
 * Selects items that have at least one removable fault.
 */
static bool item_tester_uncursable(const struct object *obj)
{
	struct fault_data *c = obj->known->faults;
	if (c) {
		size_t i;
		for (i = 1; i < z_info->fault_max; i++) {
			if (c[i].power < 100) {
				return true;
			}
		}
	}
    return false;
}

/**
 * Selects items that can accept a fault.
 */
static bool item_tester_breakable(const struct object *obj)
{
	/* Can't break artifacts, or nonequippables */
	if (obj->artifact)
		return false;

	/* Check it can be worn */
	if (!obj_can_wear(obj))
		return false;

	/* Caught 'em all? */
	struct fault_data *c = obj->known->faults;
	bool ok = false;
	if (c) {
		for (int i = 1; i < z_info->fault_max; i++) {
			if (c[i].power == 0) {
				ok = true;
				break;
			}
		}
	} else {
		ok = true;
	}
    return ok;
}

/** Artifact creation.
 * Can't be an artifact or ego, must be equipment, must be a single item
 */
static bool item_tester_artifact_creation(const struct object *obj)
{
	if (obj->artifact)
		return false;
	if (obj->ego[0])
		return false;
	/* Check it can be worn */
	if (!obj_can_wear(obj))
		return false;
	if (obj->number > 1)
		return false;
	return true;
}

/**
 * Removes an individual fault from an object.
 */
static void remove_object_fault(struct object *obj, int index, bool message)
{
	struct fault_data *c = &obj->faults[index];
	char *name = faults[index].name;
	char *removed = format("The %s fault is repaired!", name);
	int i;

	c->power = 0;
	c->timeout = 0;
	if (message) {
		msg(removed);
	}

	/* Check to see if that was the last one */
	for (i = 1; i < z_info->fault_max; i++) {
		if (obj->faults[i].power) {
			return;
		}
	}

	mem_free(obj->faults);
	obj->faults = NULL;
}

static bool do_repair_object(struct object *obj, int strength, random_value value, int index)
{
	struct fault_data fault = obj->faults[index];
	char o_name[80];
	bool remove = false;
	bool success = false;

	if (fault.power >= 100) {
		/* Fault is permanent */
		return false;
	} else if (strength >= fault.power) {
		/* Successfully removed this fault */
		remove_object_fault(obj->known, index, false);
		remove_object_fault(obj, index, true);
		success = true;
	} else if (kf_has(obj->kind->kind_flags, KF_ACT_FAILED)) {
		light_special_activation(obj);
		remove = true;
	} else if (!of_has(obj->flags, OF_FRAGILE)) {
		/* Failure to remove, object is now fragile */
		object_desc(o_name, sizeof(o_name), obj, ODESC_FULL, player);
		msgt(MSG_FAULTY, "The attempt fails; your %s is now fragile.", o_name);
		of_on(obj->flags, OF_FRAGILE);
		player_learn_flag(player, OF_FRAGILE);
	} else if (one_in_(4)) {
		/* Failure - unlucky fragile object is destroyed */
		remove = true;
		msg("There is a bang and a flash!");
		take_hit(player, damroll(5, 5), "failed repairing");
	}
	if (remove) {
		bool none_left = false;
		struct object *destroyed;
		if (object_is_carried(player, obj)) {
			destroyed = gear_object_for_use(player, obj, 1, false, &none_left);
			if (destroyed->artifact) {
				/* Artifacts are marked as lost */
				history_lose_artifact(player, destroyed->artifact);
			}
			object_delete(player->cave, NULL, &destroyed->known);
			object_delete(cave, player->cave, &destroyed);
		} else {
			square_delete_object(cave, obj->grid, obj, true, true);
		}
	} else {
		/* Success (message already printed) / Non-destructive failure */
		if (!success)
			msg("The repair fails.");
	}
	return success;
}

/**
 * Attempts to remove a fault from an object.
 */
static bool repair_object(struct object *obj, int strength, random_value value, bool all)
{
	int index = 0;
	bool success = false;

	if (all) {
		/* Find the highest difficulty fault */
		int power = 0;
		int index = -1;
		for (int i = 1; i < z_info->fault_max; i++) {
			if ((obj->known->faults[i].power > 0) &&
				(obj->known->faults[i].power < 100) &&
				player_knows_fault(player, i)) {
				power = MAX(power, obj->faults[i].power);
				index = i;
			}
		}
		if (index < 0) {
			msg("It has no faults that can be repaired.");
			return false;
		}
		/* Fix the highest difficulty fault */
		if ((success = do_repair_object(obj, strength, value, index))) {
			/* If that was successful, silently repair the others */
			for (int i = 1; i < z_info->fault_max; i++) {
			if ((obj->known->faults[i].power > 0) &&
				(obj->known->faults[i].power < 100) &&
				player_knows_fault(player, i) &&
				i != index) {
					remove_object_fault(obj->known, i, false);
					remove_object_fault(obj, i, true);
				}
			}
		}
	} else {
		if (get_fault(&index, obj, value)) {
			success = do_repair_object(obj, strength, value, index);
		} else {
			return false;
		}
	}

	player->upkeep->notice |= (PN_COMBINE);
	player->upkeep->update |= (PU_BONUS);
	player->upkeep->redraw |= (PR_EQUIP | PR_INVEN);
	return success;
}

/**
 * Attempts to add a fault to an object.
 */
static bool break_object(struct object *obj, random_value value)
{
	bool broken = false;

	if (item_tester_breakable(obj)) {

		/* Randomly select a fault and adds it to the item in question. */
		int max_tries = 5000;

		/* This is assumed to intentional (if this wasn't the case messages
		 * at least would have to be changed), so higher level players should
		 * be more controlled and produce less high-level faults.
		 * Result: at level 50, 30 +- 5%. At level 1, 50 +- 49%.
		 */
		int lev = levels_in_class(get_class_by_name("Engineer")->cidx);
		int mean_power = 50 - ((lev * 20) / PY_MAX_LEVEL);
		int range_power = 49 - ((lev * 44) / PY_MAX_LEVEL);
		int min_power = mean_power - range_power;
		int max_power = mean_power + range_power;
		int power = rand_range(min_power, max_power);

		while (max_tries) {
			int pick = randint1(z_info->fault_max - 1);
			if (!faults[pick].poss[obj->tval]) {
				max_tries--;
				continue;
			}
			if (append_object_fault(obj, pick, power)) {
				broken = true;
				break;
			}
		}
	}

	if (broken) {
		msg("You induce a fault.");

		player->upkeep->notice |= (PN_COMBINE);
		player->upkeep->update |= (PU_BONUS);
		player->upkeep->redraw |= (PR_EQUIP | PR_INVEN);
		return true;
	} else {
		msg("It remains untouched.");
	}

	return false;
}

/**
 * Selects items that have at least one unknown icon.
 */
static bool item_tester_unknown(const struct object *obj)
{
    return object_icons_known(obj) ? false : true;
}

/**
 * Selects items that have at least one unknown fault icon.
 */
static bool item_tester_unknown_faults(const struct object *obj)
{
    return object_faults_known(obj) ? false : true;
}

/**
 * Selects items that have the fragile flags set
 */
static bool item_tester_fragile(const struct object *obj)
{
    return of_has(obj->flags, OF_FRAGILE);
}


/**
 * Used by the enchant() function (chance of failure)
 */
s16b *enchant_table;
int n_enchant_table;

/**
 * Hook to specify "recyclable into blocks"
 */
static bool item_tester_recyclable(const struct object *obj)
{
	return (recyclable_blocks(obj) > 0);
}

/**
 * Hook to specify "can be branded" - check that at least one ego slot is available,
 * is not an artifact and that an ego exists for that item kind.
 */
static bool item_tester_hook_brandable(const struct object *obj)
{
	if (obj->artifact)
		return false;
	if (obj->ego[MAX_EGOS-1])
		return false;
	for(int i=0;i<z_info->e_max;i++) {
		struct poss_item *poss = e_info[i].poss_items; 
		for (; poss; poss = poss->next)
			if (poss->kidx == obj->kind->kidx)
				return true;
	}
	return false;
}

/**
 * Hook to specify "can be made mundane" - that is, it is an artifact, ego, faulty or at least has +
 */
static bool item_tester_hook_mundane(const struct object *obj)
{
	if (obj->artifact)
		return true;
	if (obj->ego[0])
		return true;
	if (obj->faults)
		return true;
	if (obj->ac > obj->kind->ac)
		return true;
	if (obj->to_a > 0)
		return true;
	if (obj->to_h > 0)
		return true;
	if (obj->to_d > 0)
		return true;
	return false;
}

/**
 * Tries to increase an items bonus score, if possible.
 *
 * \returns true if the bonus was increased
 */
static bool enchant_score(s16b *score, bool is_artifact)
{
	int chance;

	/* Artifacts resist enchantment half the time */
	if (is_artifact && randint0(100) < 50) return false;

	/* Figure out the chance to enchant */
	if (*score < 0) chance = 0;
	else if (*score >= n_enchant_table) chance = 1000;
	else chance = enchant_table[*score];

	/* If we roll less-than-or-equal to chance, it fails */
	if (randint1(1000) <= chance) return false;

	/* Increment the score */
	++*score;

	return true;
}

/**
 * Helper function for enchant() which tries increasing an item's bonuses
 *
 * \returns true if a bonus was increased
 */
static bool enchant2(struct object *obj, s16b *score)
{
	bool result = false;
	bool is_artifact = obj->artifact ? true : false;
	if (enchant_score(score, is_artifact)) result = true;
	return result;
}

/**
 * Enchant an item
 *
 * Revamped!  Now takes item pointer, number of times to try enchanting, and a
 * flag of what to try enchanting.  Artifacts resist enchantment some of the
 * time. Also, any enchantment attempt (even unsuccessful) kicks off a parallel
 * attempt to repair a faulty item.
 *
 * Note that an item can technically be enchanted all the way to +15 if you
 * wait a very, very, long time.  Going from +9 to +10 only works about 5% of
 * the time, and from +10 to +11 only about 1% of the time.
 *
 * Note that this function can now be used on "piles" of items, and the larger
 * the pile, the lower the chance of success.
 *
 * \returns true if the item was changed in some way
 */
static bool enchant(struct object *obj, int n, int eflag)
{
	int i, prob;
	bool res = false;

	/* Large piles resist enchantment */
	prob = obj->number * 100;

	/* Missiles are easy to enchant */
	if (tval_is_ammo(obj)) prob = prob / 20;

	/* Try "n" times */
	for (i = 0; i < n; i++)
	{
		/* Roll for pile resistance */
		if (prob > 100 && randint0(prob) >= 100) continue;

		/* Try the three kinds of enchantment we can do */
		if ((eflag & ENCH_TOHIT) && enchant2(obj, &obj->to_h)) res = true;
		if ((eflag & ENCH_TODAM) && enchant2(obj, &obj->to_d)) res = true;
		if ((eflag & ENCH_TOAC)  && enchant2(obj, &obj->to_a)) res = true;
	}

	/* Update knowledge */
	assert(obj->known);
	obj->known->to_h = obj->to_h;
	obj->known->to_d = obj->to_d;
	obj->known->to_a = obj->to_a;

	/* Failure */
	if (!res) return (false);

	/* Recalculate bonuses, gear */
	player->upkeep->update |= (PU_BONUS | PU_INVEN);

	/* Combine the pack (later) */
	player->upkeep->notice |= (PN_COMBINE);

	/* Redraw stuff */
	player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);

	/* Success */
	return (true);
}


/**
 * Enchant an item (in the inventory or on the floor)
 * Note that "num_ac" requires armour, else weapon
 * Returns true if attempted, false if cancelled
 *
 * Enchanting with the TOBOTH flag will try to enchant
 * both to_hit and to_dam with the same flag.  This
 * may not be the most desirable behavior (ACB).
 */
static bool enchant_spell(int num_hit, int num_dam, int num_ac, int num_brand, int num_mundane, struct command *cmd)
{
	bool okay = false;

	struct object *obj;

	char o_name[80];

	const char *q, *s;
	int itemmode = (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR);

	item_tester filter = num_ac ?
		tval_is_armor : tval_is_weapon;
	if (num_brand)
		filter = item_tester_hook_brandable;
	if (num_mundane)
		filter = item_tester_hook_mundane;

	/* Get an item */
	q = "Enhance which item? ";
	s = "You have nothing to enhance.";
	if (cmd) {
		if (cmd_get_item(cmd, "tgtitem", &obj, q, s, filter,
				itemmode)) {
			return false;
		}
	} else if (!get_item(&obj, q, s, 0, filter, itemmode))
		return false;

	/* Description */
	object_desc(o_name, sizeof(o_name), obj, ODESC_BASE, player);

	/* Describe */
	const char *bright = "soft";
	int lights = (num_hit + num_dam + num_ac);
	if (lights > 1)
		bright = "bright"; 
	if ((!num_brand) && (!num_mundane)) {
		msg("%s %s glow%s %sly!",
			(object_is_carried(player, obj) ? "Your" : "The"), o_name,
				   ((obj->number > 1) ? "" : "s"), bright);
	}

	/* Enchant */
	if (num_dam && enchant(obj, num_hit, ENCH_TOBOTH)) okay = true;
	else if (num_hit && enchant(obj, num_hit, ENCH_TOHIT)) okay = true;
	else if (num_dam && enchant(obj, num_dam, ENCH_TODAM)) okay = true;
	if (num_ac && enchant(obj, num_ac, ENCH_TOAC)) okay = true;
	if (num_brand)
		okay = brand_object(obj, NULL);
	if (num_mundane)
		okay = mundane_object(obj, false);

	/* Failure */
	if (!okay) {
		event_signal(EVENT_INPUT_FLUSH);

		/* Message */
		if ((!num_brand) && (!num_mundane))
			msg("The enhancement failed.");
	}

	/* Something happened */
	return (true);
}

/**
 * Brand weapons (or ammo)
 *
 * Turns the (non-magical) object into an ego-item of 'brand_type'.
 * 
 * Returns true if successful
 */
static bool brand_object(struct object *obj, const char *name)
{
	struct ego_item *ego = NULL;
	struct ego_item *ego2 = NULL;
	int negos = 0;

	if (obj) {
		/* Count egos */
		while ((obj->ego[negos]) && (negos < MAX_EGOS))
			negos++;

		/* Find a matching ego */
		if (name) {
			ego = lookup_ego_item(name, obj->kind->tval, obj->kind->sval);
		} else {
			ego = select_ego_base(player->max_depth, obj);
		}

		/* You can never modify artifacts, maxed ego items or worthless items */
		if (ego && obj->kind->cost && !obj->artifact && !(negos >= MAX_EGOS)) {
			char o_name[80];

			/* If there are MAX_EGOS already, stop always.
			 * If there are >0, <MAX_EGOS, often stop.
			 */ 
			if ((negos == 0) || (one_in_(1<<(negos * 2)))) {

				object_desc(o_name, sizeof(o_name), obj, ODESC_BASE, player);

				/* Sometimes things go wrong! */
				if (one_in_(25)) {
					msg("The %s flashes briefly, but something's not right...", o_name, (obj->number > 1) ? "glow" : "glows");
					return mundane_object(obj, false);
				}

				/* Make it an ego item */
				obj->ego[negos] = ego;
				ego_apply_magic_from(obj, 0, negos);

				/* Sometimes they go very right!
				 * Take care to only show the message if a second ego
				 * can be found.
				 **/
				if ((!negos) && (one_in_(25))) {
					ego2 = select_ego_base(player->max_depth, obj);
					if (ego2) {
						msg("The %s %s into dazzling brilliance!", o_name,
						(obj->number > 1) ? "erupt" : "erupts");
					}
				}

				if (!ego2) {
					/* Describe */
					msg("The %s %s brilliantly!", o_name,
						(obj->number > 1) ? "glow" : "glows");
				}

				if (ego2) {
					negos++;
					obj->ego[negos] = ego2;
					ego_apply_magic_from(obj, 0, negos);
				}

				/* Identify the object */
				object_know_all(obj);
				update_player_object_knowledge(player);

				/* Update the gear */
				player->upkeep->update |= (PU_INVEN);

				/* Combine the pack (later) */
				player->upkeep->notice |= (PN_COMBINE);

				/* Window stuff */
				player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);

				/* Enchant */
				if (kind_tval_is_weapon(obj->kind))
					enchant(obj, randint0(3) + 4, ENCH_TOHIT | ENCH_TODAM);
				else if (kind_tval_is_armor(obj->kind))
					enchant(obj, randint0(3) + 4, ENCH_TOAC);
				return true;
			}
		}
	}

	event_signal(EVENT_INPUT_FLUSH);
	msg("It glows feebly for a moment, then winks out. The branding failed.");

	return false;
}
/**
 * Make mundane.
 *
 * Turns the object into a plain default + version without faults, egos, artifact.
 * This does have a chance of increasing random hit/dam/ac.
 *
 * Returns true if successful
 */
static bool mundane_object(struct object *obj, bool silent)
{
	bool success = false;

	/* Identify the object */
	if (item_tester_hook_mundane(obj)) {
		success = true;
		object_know_all(obj);
	}

	/* Moan */
	const char *moan = "drawn-out";
	if (obj->faults)
		moan = "resonant";
	if (obj->ego)
		moan = "awful";
	if (obj->artifact)
		moan = "terrible";

	/* Flatten */
	struct object_kind *kind = obj->kind;
	struct object *prev = obj->prev;
	struct object *next = obj->next;
	struct loc grid = obj->grid;
	struct object *known = obj->known;
	mem_free(obj->slays);
	mem_free(obj->brands);
	mem_free(obj->faults);
	object_prep(obj, kind, player->depth, RANDOMISE);
	obj->prev = prev;
	obj->next = next;
	obj->grid = grid;
	if (known) {
		struct object *prev = known->prev;
		struct object *next = known->next;
		struct loc grid = known->grid;
		mem_free(known->slays);
		mem_free(known->brands);
		mem_free(known->faults);
		object_prep(known, kind, player->depth, RANDOMISE);
		known->prev = prev;
		known->next = next;
		known->grid = grid;
	}
	obj->known = known;

	/* Update the gear */
	player->upkeep->update |= (PU_INVEN | PU_BONUS);

	/* Combine the pack (later) */
	player->upkeep->notice |= (PN_COMBINE);

	/* Window stuff */
	player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);

	event_signal(EVENT_INPUT_FLUSH);
	if (!silent) {
		if (success) {
			char o_name[80];
			object_desc(o_name, sizeof(o_name), obj, ODESC_BASE, player);
			msg("There is a %s moan, and the %s shudders...", moan, o_name);
		} else {
			msg("There is a distant moan, but nothing more appears to happen.");
		}
	}
	return success;
}

/**
 * ------------------------------------------------------------------------
 * Effect handlers
 * ------------------------------------------------------------------------ */
/**
 * Dummy effect, to tell the effect code to pick one of the next
 * context->value.base effects at random.
 */
bool effect_handler_RANDOM(effect_handler_context_t *context)
{
	return true;
}

/**
 * Feed the player, or set their satiety level.
 */
bool effect_handler_NOURISH(effect_handler_context_t *context)
{
	int amount = effect_calculate_value(context, false);
	amount *= z_info->food_value;
	if (context->subtype == 0) {
		/* Increase food level by amount */
		player_inc_timed(player, TMD_FOOD, MAX(amount, 0), false, false);
	} else if (context->subtype == 1) {
		/* Decrease food level by amount */
		player_dec_timed(player, TMD_FOOD, MAX(amount, 0), false);
	} else if (context->subtype == 2) {
		/* Set food level to amount, vomiting if necessary */
		bool message = player->timed[TMD_FOOD] > amount;
		if (message) {
			msg("You vomit!");
		}
		player_set_timed(player, TMD_FOOD, MAX(amount, 0), false);
	} else if (context->subtype == 3) {
		/* Increase food level to amount if needed */
		if (player->timed[TMD_FOOD] < amount) {
			player_set_timed(player, TMD_FOOD, MAX(amount + 1, 0), false);
		}
	} else {
		return false;
	}
	context->ident = true;
	return true;
}

bool target_set_interactive(int mode, int x, int y);
/* Check WIS, and if it passes you swallowed it.
 * In that case, feed you (same as a normal pepper) and if you pass a CON check and are not already opposing cold,
 * do so. If you pass a more difficult check, also breathe fire.
 */
bool effect_handler_HABANERO(effect_handler_context_t *context)
{
	if (stat_check(STAT_WIS, 15)) {
		msg("It's seriously HOT! But you manage to swallow the fiery pepper.");
		player_inc_timed(player, TMD_FOOD, 8 * z_info->food_value, false, false);
		if ((player->timed[TMD_OPP_COLD] == 0) && stat_check(STAT_CON, 12)) {
			player_inc_timed(player, TMD_OPP_COLD, damroll(3, 6), false, false);
			if (stat_check(STAT_CON, 18)) {
				msg("You burp fire!");
				event_signal(EVENT_MESSAGE_FLUSH);
				struct loc target = player->grid;
					target_set_interactive(TARGET_KILL, -1, -1);
					target_get(&target);
				project(context->origin, 20, target, 5 + damroll(1, 10) + player->lev, ELEM_FIRE,  PROJECT_ARC | PROJECT_GRID | PROJECT_ITEM | PROJECT_KILL | PROJECT_LOL, 40, 10, context->obj);
			}
		}
	} else  {
		/* No effect */
		msg("It's seriously HOT, and you spit it out.");
	}
	context->ident = true;
	return true;
}

/* Check WIS, and if it passes you swallowed it.
 * In that case, feed you (to max, including slowing) and transform into a giant!
 */
bool effect_handler_SNOZZCUMBER(effect_handler_context_t *context)
{
	if (stat_check(STAT_WIS, 17)) {
		msg("It's extremely bitter, but you manage to swallow the disgusting vegetable.");
		player_set_timed(player, TMD_FOOD, 99 * z_info->food_value, false);
		shapechange("giant", true);
	} else  {
		/* No effect */
		msg("It's extremely bitter and you spit it out in disgust.");
	}
	context->ident = true;
	return true;
}

bool effect_handler_CRUNCH(effect_handler_context_t *context)
{
	if (one_in_(2))
		msg("It's crunchy.");
	else
		msg("It nearly breaks your tooth!");
	context->ident = true;
	return true;
}

/**
 * Cure a player status condition.
 */
bool effect_handler_CURE(effect_handler_context_t *context)
{
	int type = context->subtype;
	(void) player_clear_timed(player, type, true);
	context->ident = true;
	return true;
}

/**
 * Set a (positive or negative) player status condition.
 */
bool effect_handler_TIMED_SET(effect_handler_context_t *context)
{
	int amount = effect_calculate_value(context, false);
	player_set_timed(player, context->subtype, MAX(amount, 0), true);
	context->ident = true;
	return true;

}

/**
 * Extend a (positive or negative) player status condition.
 * If context->other is set, increase by that amount if the player already
 * has the status
 */
bool effect_handler_TIMED_INC(effect_handler_context_t *context)
{
	int amount = effect_calculate_value(context, false);
	struct monster *t_mon = monster_target_monster(context);
	struct loc decoy = cave_find_decoy(cave);

	context->ident = true;

	/* Destroy decoy if it's a monster attack */
	if (cave->mon_current > 0 && decoy.y && decoy.x) {
		square_destroy_decoy(cave, decoy);
		return true;
	}

	/* Check for monster targeting another monster */
	if (t_mon) {
		int mon_tmd_effect = -1;

		/* Will do until monster and player timed effects are fused */
		switch (context->subtype) {
			case TMD_CONFUSED: {
				mon_tmd_effect = MON_TMD_CONF;
				break;
			}
			case TMD_SLOW: {
				mon_tmd_effect = MON_TMD_SLOW;
				break;
			}
			case TMD_PARALYZED:
			case TMD_HELD: {
				mon_tmd_effect = MON_TMD_HOLD;
				break;
			}
			case TMD_BLIND: {
				mon_tmd_effect = MON_TMD_STUN;
				break;
			}
			case TMD_AFRAID: {
				mon_tmd_effect = MON_TMD_FEAR;
				break;
			}
			case TMD_AMNESIA: {
				mon_tmd_effect = MON_TMD_SLEEP;
				break;
			}
			default: {
				break;
			}
		}
		if (mon_tmd_effect >= 0) {
			mon_inc_timed(t_mon, mon_tmd_effect, MAX(amount, 0), 0);
		}
		return true;
	}

	if (!player->timed[context->subtype] || !context->other) {
		player_inc_timed(player, context->subtype, MAX(amount, 0), true, true);
	} else {
		player_inc_timed(player, context->subtype, context->other, true, true);
	}
	return true;
}

/**
 * Extend a (positive or negative) player status condition unresistably.
 * If context->other is set, increase by that amount if the player already
 * has the status
 */
bool effect_handler_TIMED_INC_NO_RES(effect_handler_context_t *context)
{
	int amount = effect_calculate_value(context, false);

	if (!player->timed[context->subtype] || !context->other)
		player_inc_timed(player, context->subtype, MAX(amount, 0), true, false);
	else
		player_inc_timed(player, context->subtype, context->other, true, false);
	context->ident = true;
	return true;
}

/**
 * Extend a (positive or negative) monster status condition.
 */
bool effect_handler_MON_TIMED_INC(effect_handler_context_t *context)
{
	assert(context->origin.what == SRC_MONSTER);

	int amount = effect_calculate_value(context, false);
	struct monster *mon = cave_monster(cave, context->origin.which.monster);

	if (mon) {
		mon_inc_timed(mon, context->subtype, MAX(amount, 0), 0);
		context->ident = true;
	}

	return true;
}

/**
 * Reduce a (positive or negative) player status condition.
 * If context->other is set, decrease by the current value / context->other
 */
bool effect_handler_TIMED_DEC(effect_handler_context_t *context)
{
	int amount = effect_calculate_value(context, false);
	if (context->other)
		amount = player->timed[context->subtype] / context->other;
	(void) player_dec_timed(player, context->subtype, MAX(amount, 0), true);
	context->ident = true;
	return true;
}

/**
 * Create a glyph.
 */
bool effect_handler_GLYPH(effect_handler_context_t *context)
{
	struct loc decoy = cave_find_decoy(cave);

	/* Always notice */
	context->ident = true;

	/* Only one decoy at a time */
	if (!loc_is_zero(decoy) && (context->subtype == GLYPH_DECOY)) {
		msg("You can only deploy one decoy at a time.");
		return false;
	}

	/* See if the effect works */
	if (!square_istrappable(cave, player->grid)) {
		msg("There is no clear floor on which to deploy the decoy.");
		return false;
	}

	/* Push objects off the grid */
	if (square_object(cave, player->grid))
		push_object(player->grid);

	/* Create a glyph */
	square_add_glyph(cave, player->grid, context->subtype);

	return true;
}

/**
 * Create a web.
 */
bool effect_handler_WEB(effect_handler_context_t *context)
{
	int rad = 1;
	struct monster *mon = NULL;
	struct loc grid;

	/* Get the monster creating */
	if (cave->mon_current > 0) {
		mon = cave_monster(cave, cave->mon_current);
	} else {
		/* Player can't currently create webs */
		return false;
	}

	/* Always notice */
	context->ident = true;

	/* Increase the radius for higher spell power */
	if (mon->race->spell_power > 40) rad++;
	if (mon->race->spell_power > 80) rad++;

	/* Check within the radius for clear floor */
	for (grid.y = mon->grid.y - rad; grid.y <= mon->grid.y + rad; grid.y++) {
		for (grid.x = mon->grid.x - rad; grid.x <= mon->grid.x + rad; grid.x++){
			if (distance(grid, mon->grid) > rad ||
				!square_in_bounds_fully(cave, grid)) continue;

			/* Require a floor grid with no existing traps or glyphs */
			if (!square_iswebbable(cave, grid)) continue;

			/* Create a web */
			square_add_web(cave, grid);
		}
	}

	return true;
}

/**
 * Restore a stat; the stat index is context->subtype
 */
bool effect_handler_RESTORE_STAT(effect_handler_context_t *context)
{
	int stat = context->subtype;

	/* ID */
	context->ident = true;

	/* Check bounds */
	if (stat < 0 || stat >= STAT_MAX) return false;

	/* Not needed */
	if (player->stat_cur[stat] == player->stat_max[stat])
		return true;

	/* Restore */
	player->stat_cur[stat] = player->stat_max[stat];

	/* Recalculate bonuses */
	player->upkeep->update |= (PU_BONUS);
	update_stuff(player);

	/* Message */
	msg("You feel less %s.", desc_stat(stat, false));

	return (true);
}

/**
 * Drain a stat temporarily.  The stat index is context->subtype.
 */
bool effect_handler_DRAIN_STAT(effect_handler_context_t *context)
{
	int stat = context->subtype;
	int flag = sustain_flag(stat);

	/* Bounds check */
	if (flag < 0) return false;

	/* ID */
	context->ident = true;

	/* Sustain */
	if (player_of_has(player, flag)) {
		/* Notice effect */
		equip_learn_flag(player, flag);

		/* Message */
		msg("You feel very %s for a moment, but the feeling passes.",
				   desc_stat(stat, false));

		return (true);
	}

	/* Attempt to reduce the stat */
	if (player_stat_dec(player, stat, false)){
		int dam = effect_calculate_value(context, false);

		/* Notice effect */
		equip_learn_flag(player, flag);

		/* Message */
		msgt(MSG_DRAIN_STAT, "You feel very %s.", desc_stat(stat, false));
		if (dam)
			take_hit(player, dam, "stat drain");
	}

	return (true);
}

/**
 * Lose a stat point permanently, in a stat other than the one specified
 * in context->subtype.
 */
bool effect_handler_LOSE_RANDOM_STAT(effect_handler_context_t *context)
{
	int safe_stat = context->subtype;
	int loss_stat = randint1(STAT_MAX - 1);

	/* Avoid the safe stat */
	loss_stat = (loss_stat + safe_stat) % STAT_MAX;

	/* Attempt to reduce the stat */
	if (player_stat_dec(player, loss_stat, true)) {
		msgt(MSG_DRAIN_STAT, "You feel very %s.", desc_stat(loss_stat, false));
	}

	/* ID */
	context->ident = true;

	return (true);
}


/**
 * Gain a stat point.  The stat index is context->subtype.
 */
bool effect_handler_GAIN_STAT(effect_handler_context_t *context)
{
	int stat = context->subtype;

	/* Attempt to increase */
	if (player_stat_inc(player, stat)) {
		msg("You feel very %s!", desc_stat(stat, true));
	}

	/* Notice */
	context->ident = true;

	return (true);
}

/**
 * Restores any drained experience
 */
bool effect_handler_RESTORE_EXP(effect_handler_context_t *context)
{
	/* Restore experience */
	if (player->exp < player->max_exp) {
		/* Message */
		if (context->origin.what != SRC_NONE)
			msg("You feel your memories returning.");
		player_exp_gain(player, player->max_exp - player->exp);

		/* Recalculate max. hitpoints */
		update_stuff(player);
	}

	/* Did something */
	context->ident = true;

	return (true);
}

/* Note the divisor of 2, a slight hack to simplify food description */
bool effect_handler_GAIN_EXP(effect_handler_context_t *context)
{
	int amount = effect_calculate_value(context, false);
	if (player->exp < PY_MAX_EXP) {
		msg("You feel more experienced.");
		player_exp_gain_scaled(player, amount / 2);
	}
	context->ident = true;

	return true;
}

/**
 * Drain some light from the player's light source, if possible
 */
bool effect_handler_DRAIN_LIGHT(effect_handler_context_t *context)
{
	int drain = effect_calculate_value(context, false);

	int light_slot = slot_by_name(player, "light");
	struct object *obj = slot_object(player, light_slot);

	if (obj && !of_has(obj->flags, OF_NO_FUEL) && (obj->timeout > 0)) {
		/* Reduce fuel */
		obj->timeout -= drain;
		if (obj->timeout < 1) obj->timeout = 1;

		/* Notice */
		if (!player->timed[TMD_BLIND]) {
			msg("Your light dims.");
			context->ident = true;
		}

		/* Redraw stuff */
		player->upkeep->redraw |= (PR_EQUIP);
	}

	return true;
}

static bool do_remove_fault(effect_handler_context_t *context, bool all)
{
	const char *prompt = "Repair which item? ";
	const char *rejmsg = "You have no repairable items.";
	int itemmode = (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR);
	int strength = effect_calculate_value(context, false);
	struct object *obj = NULL;

	context->ident = true;

	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, prompt,
				rejmsg, item_tester_uncursable, itemmode)) {
			return false;
		}
	} else if (!get_item(&obj, prompt, rejmsg, 0, item_tester_uncursable,
			itemmode))
		return false;

	return repair_object(obj, strength, context->value, all);
}

/**
 * Attempt to add faults to an object
 */
bool effect_handler_ADD_FAULT(effect_handler_context_t *context)
{
	const char *prompt = "Break which item? ";
	const char *rejmsg = "It cannot be broken that easily.";
	int itemmode = (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR);
	struct object *obj = NULL;

	context->ident = true;

	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, prompt,
				rejmsg, item_tester_breakable, itemmode)) {
			return false;
		}
	} else if (!get_item(&obj, prompt, rejmsg, 0, item_tester_breakable,
			itemmode))
		return false;

	return break_object(obj, context->value);
}

/**
 * Attempt to repair an object
 */
bool effect_handler_REMOVE_FAULT(effect_handler_context_t *context)
{
	return do_remove_fault(context, false);
}

/**
 * Attempt to repair an object completely
 */
bool effect_handler_REMOVE_FAULTS(effect_handler_context_t *context)
{
	return do_remove_fault(context, true);
}

/**
 * Set word of recall as appropriate
 */
bool effect_handler_RECALL(effect_handler_context_t *context)
{
	int target_depth;
	context->ident = true;

	/* No recall */
	if (OPT(player, birth_no_recall) && !player->orbitable) {
		msg("Nothing happens - you have chosen not to recall until Xygos is free.");
		return true;
	}

	/* No recall in the endgame */
	if (!player->town) {
		if (!player->total_winner) {
			if (is_blocking_quest(player, player->depth))
				msg("Nothing happens - something nearby is blocking it.");
			else
				msg("Nothing happens - something below you is blocking it.");
		} else {
			/* If you are a Total Winner, this is another (equivalent, maybe more intuitive) way to ascend */
			textui_cmd_suicide();
		}
		return true;
	}

	/* No recall from quest levels with force_descend */

	if (OPT(player, birth_force_descend) && (is_blocking_quest(player, player->depth))) {
		msg("Nothing happens - something nearby is blocking it.");
		return true;
	}

	/* No recall from single combat */
	if (player->upkeep->arena_level) {
		msg("Nothing happens - the room you are in blocks it.");
		return true;
	}

	/* Warn the player if they're descending to an unrecallable level */
	target_depth = dungeon_get_next_level(player, player->max_depth, 1);
	if (OPT(player, birth_force_descend) && !(player->depth) &&
			(is_blocking_quest(player, target_depth))) {
		if (!get_check("Are you sure you want to descend? ")) {
			return false;
		}
	}

	/* Activate recall */
	if (!player->word_recall) {
		/* Reset recall depth */
		if (player->active_quest < 0) {
			if (player->depth > 0) {
				if (player->depth != player->max_depth) {
					if (get_check("Set recall depth to current depth? ")) {
						player->town->recall_depth = player->max_depth = player->depth;
					}
				} else {
					player->town->recall_depth = player->max_depth;
				}
			} else {
				if (OPT(player, birth_levels_persist)) {
					/* Persistent levels players get to choose */
					if (!player_get_recall_depth(player)) return false;
				}
			}
		}

		player->word_recall = randint0(20) + 15;
		msg("The air about you becomes charged...");
	} else {
		/* Deactivate recall */
		if (!get_check("Recall is already active.  Do you want to cancel it? "))
			return false;

		player->word_recall = 0;
		msg("A tension leaves the air around you...");
	}

	/* Redraw status line */
	player->upkeep->redraw |= PR_STATUS;
	handle_stuff(player);

	return true;
}

bool effect_handler_DEEP_DESCENT(effect_handler_context_t *context)
{
	int i;

	/* Calculate target depth */
	int target_increment = (4 / z_info->stair_skip) + 1;
	int target_depth = dungeon_get_next_level(player, player->depth,
											  target_increment);
	int levels = 5;

	/* Endgame */
	if (!player->town)
		levels = 1;

	for (i = levels; i > 0; i--) {
		if (is_blocking_quest(player, target_depth)) break;
		if (target_depth >= z_info->max_depth - 1) break;
		target_depth++;
	}

	if (target_depth > player->depth) {
		msgt(MSG_TPLEVEL, "The air around you starts to swirl...");
		player->deep_descent = 3 + randint1(4);

		/* Redraw status line */
		player->upkeep->redraw |= PR_STATUS;
		handle_stuff(player);
	} else {
		msgt(MSG_TPLEVEL, "The air swirls briefly, then settles. Something's blocking your descent.");
	}
	context->ident = true;
	return true;
}

bool effect_handler_ALTER_REALITY(effect_handler_context_t *context)
{
	msg("The world changes!");
	dungeon_change_level(player, (player->active_quest >= 0) ? 0 : player->depth);
	context->ident = true;
	return true;
}

/**
 * Map an area around a point, usually the player.
 * The height to map above and below the player is context->y,
 * the width either side of the player context->x.
 * For player level dependent areas, we use the hack of applying value dice
 * and sides as the height and width.
 */
bool effect_handler_MAP_AREA(effect_handler_context_t *context)
{
	int i, x, y;
	int x1, x2, y1, y2;
	int dist_y = context->y ? context->y : context->value.dice;
	int dist_x = context->x ? context->x : context->value.sides;
	struct loc centre = origin_get_loc(context->origin);

	/* Pick an area to map */
	y1 = centre.y - dist_y;
	y2 = centre.y + dist_y;
	x1 = centre.x - dist_x;
	x2 = centre.x + dist_x;

	/* Drag the co-ordinates into the dungeon */
	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (y2 > cave->height - 1) y2 = cave->height - 1;
	if (x2 > cave->width - 1) x2 = cave->width - 1;

	/* Scan the dungeon */
	for (y = y1; y < y2; y++) {
		for (x = x1; x < x2; x++) {
			struct loc grid = loc(x, y);

			/* Some squares can't be mapped */
			if (square_isno_map(cave, grid)) continue;

			/* All non-walls are "checked" */
			if (!square_seemslikewall(cave, grid)) {
				if (!square_in_bounds_fully(cave, grid)) continue;

				/* Memorize normal features */
				if (!square_isfloor(cave, grid))
					square_memorize(cave, grid);

				/* Memorize known walls */
				for (i = 0; i < 8; i++) {
					int yy = y + ddy_ddd[i];
					int xx = x + ddx_ddd[i];

					/* Memorize walls (etc) */
					if (square_seemslikewall(cave, loc(xx, yy)))
						square_memorize(cave, loc(xx, yy));
				}
			}

			/* Forget unprocessed, unknown grids in the mapping area */
			if (square_isnotknown(cave, grid))
				square_forget(cave, grid);
		}
	}

	/* Unmark grids */
	for (y = y1 - 1; y < y2 + 1; y++) {
		for (x = x1 - 1; x < x2 + 1; x++) {
			struct loc grid = loc(x, y);
			if (!square_in_bounds(cave, grid)) continue;
			square_unmark(cave, grid);
		}
	}

	/* Fully update the visuals */
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);

	/* Redraw whole map, monster list */
	player->upkeep->redraw |= (PR_MAP | PR_MONLIST | PR_ITEMLIST);

	/* Notice */
	context->ident = true;

	return true;
}

/**
 * Map an area around the recently detected monsters.
 * The height to map above and below each monster is context->y,
 * the width either side of each monster context->x.
 * For player level dependent areas, we use the hack of applying value dice
 * and sides as the height and width.
 */
bool effect_handler_READ_MINDS(effect_handler_context_t *context)
{
	int i;
	int dist_y = context->y ? context->y : context->value.dice;
	int dist_x = context->x ? context->x : context->value.sides;
	bool found = false;

	/* Scan monsters */
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);

		/* Skip dead monsters */
		if (!mon->race) continue;

		/* Detect all appropriate monsters */
		if (mflag_has(mon->mflag, MFLAG_MARK)) {
			/* Map around it */
			effect_simple(EF_MAP_AREA, source_monster(i), "0", 0, 0, 0,
						  dist_y, dist_x, NULL);
			found = true;
		}
	}

	if (found) {
		msg("Images form in your mind!");
		context->ident = true;
		return true;
	}

	return false;
}

/**
 * Detect traps around the player.  The height to detect above and below the
 * player is context->y, the width either side of the player context->x.
 */
bool effect_handler_DETECT_TRAPS(effect_handler_context_t *context)
{
	int x, y;
	int x1, x2, y1, y2;

	bool detect = false;

	struct object *obj;

	/* Pick an area to detect */
	y1 = player->grid.y - context->y;
	y2 = player->grid.y + context->y;
	x1 = player->grid.x - context->x;
	x2 = player->grid.x + context->x;

	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (y2 > cave->height - 1) y2 = cave->height - 1;
	if (x2 > cave->width - 1) x2 = cave->width - 1;


	/* Scan the dungeon */
	for (y = y1; y < y2; y++) {
		for (x = x1; x < x2; x++) {
			struct loc grid = loc(x, y);

			if (!square_in_bounds_fully(cave, grid)) continue;

			/* Detect traps */
			if (square_isplayertrap(cave, grid))
				/* Reveal trap */
				if (square_reveal_trap(cave, grid, true, false))
					detect = true;

			/* Scan all objects in the grid to look for traps on chests */
			for (obj = square_object(cave, grid); obj; obj = obj->next) {
				/* Skip anything not a trapped chest */
				if (!is_trapped_chest(obj)
						|| ignore_item_ok(player, obj)) {
					continue;
				}

				/* Identify once */
				if (!obj->known || obj->known->pval != obj->pval) {
					/* Hack - know the pile */
					square_know_pile(cave, grid);

					/* Know the trap */
					obj->known->pval = obj->pval;

					/* Notice it */
					disturb(player);

					/* We found something to detect */
					detect = true;
				}
			}
			/* Mark as trap-detected */
			sqinfo_on(square(cave, loc(x, y))->info, SQUARE_DTRAP);
		}
	}

	/* Describe */
	if (detect)
		msg("You sense the presence of traps!");

	/* Trap detection always makes you aware, even if no traps are present */
	else
		msg("You sense no traps.");

	/* Notice */
	context->ident = true;

	return true;
}

/**
 * Detect doors around the player.  The height to detect above and below the
 * player is context->y, the width either side of the player context->x.
 */
bool effect_handler_DETECT_DOORS(effect_handler_context_t *context)
{
	int x, y;
	int x1, x2, y1, y2;

	bool doors = false;

	/* Pick an area to detect */
	y1 = player->grid.y - context->y;
	y2 = player->grid.y + context->y;
	x1 = player->grid.x - context->x;
	x2 = player->grid.x + context->x;

	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (y2 > cave->height - 1) y2 = cave->height - 1;
	if (x2 > cave->width - 1) x2 = cave->width - 1;

	/* Scan the dungeon */
	for (y = y1; y < y2; y++) {
		for (x = x1; x < x2; x++) {
			struct loc grid = loc(x, y);

			if (!square_in_bounds_fully(cave, grid)) continue;

			/* Detect secret doors */
			if (square_issecretdoor(cave, grid)) {
				/* Put an actual door */
				place_closed_door(cave, grid);

				/* Memorize */
				square_memorize(cave, grid);
				square_light_spot(cave, grid);

				/* Obvious */
				doors = true;
			}

			/* Forget unknown doors in the mapping area */
			if (square_isdoor(player->cave, grid) &&
				square_isnotknown(cave, grid)) {
				square_forget(cave, grid);
			}
		}
	}

	/* Describe */
	if (doors)
		msg("You sense the presence of doors!");
	else if (context->aware)
		msg("You sense no doors.");

	context->ident = true;

	return true;
}

/**
 * Detect stairs around the player.  The height to detect above and below the
 * player is context->y, the width either side of the player context->x.
 */
bool effect_handler_DETECT_STAIRS(effect_handler_context_t *context)
{
	int x, y;
	int x1, x2, y1, y2;

	bool stairs = false;

	/* Pick an area to detect */
	y1 = player->grid.y - context->y;
	y2 = player->grid.y + context->y;
	x1 = player->grid.x - context->x;
	x2 = player->grid.x + context->x;

	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (y2 > cave->height - 1) y2 = cave->height - 1;
	if (x2 > cave->width - 1) x2 = cave->width - 1;

	/* Scan the dungeon */
	for (y = y1; y < y2; y++) {
		for (x = x1; x < x2; x++) {
			struct loc grid = loc(x, y);

			if (!square_in_bounds_fully(cave, grid)) continue;

			/* Detect stairs */
			if (square_isstairs(cave, grid)) {
				/* Memorize */
				square_memorize(cave, grid);
				square_light_spot(cave, grid);

				/* Obvious */
				stairs = true;
			}
		}
	}

	/* Describe */
	if (stairs)
		msg("You sense the presence of stairs!");
	else if (context->aware)
		msg("You sense no stairs.");

	context->ident = true;
	return true;
}

/**
 * Detect buried gold around the player.  The height to detect above and below
 * the player is context->y, the width either side of the player context->x.
 */
bool effect_handler_DETECT_GOLD(effect_handler_context_t *context)
{
	int x, y;
	int x1, x2, y1, y2;

	bool gold_buried = false;

	/* Pick an area to detect */
	y1 = player->grid.y - context->y;
	y2 = player->grid.y + context->y;
	x1 = player->grid.x - context->x;
	x2 = player->grid.x + context->x;

	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (y2 > cave->height - 1) y2 = cave->height - 1;
	if (x2 > cave->width - 1) x2 = cave->width - 1;

	/* Scan the dungeon */
	for (y = y1; y < y2; y++) {
		for (x = x1; x < x2; x++) {
			struct loc grid = loc(x, y);

			if (!square_in_bounds_fully(cave, grid)) continue;

			/* Magma/Quartz + Known Gold */
			if (square_hasgoldvein(cave, grid)) {
				/* Memorize */
				square_memorize(cave, grid);
				square_light_spot(cave, grid);

				/* Detect */
				gold_buried = true;
			} else if (square_hasgoldvein(player->cave, grid)) {
				/* Something removed previously seen or
				 * detected buried gold.  Notice the change. */
				square_forget(cave, grid);
			}
		}
	}

	/* Message unless we're silently detecting */
	if (context->origin.what != SRC_NONE) {
		if (gold_buried) {
			msg("You sense the presence of buried treasure!");
		} else if (context->aware) {
			msg("You sense no buried treasure.");
		}
	}

	context->ident = true;
	return true;
}

/**
 * This is a helper for effect_handler_SENSE_OBJECTS and
 * effect_handler_DETECT_OBJECTS to remove remembered objects at locations
 * sensed or detected as empty.
 */
void forget_remembered_objects(struct chunk *c, struct chunk *knownc, struct loc grid)
{
	struct object *obj = square_object(knownc, grid);

	while (obj) {
		struct object *next = obj->next;
		struct object *original = c->objects[obj->oidx];

		assert(original);
		square_excise_object(knownc, grid, obj);
		obj->grid = loc(0, 0);

		/* Delete objects which no longer exist anywhere */
		if (obj->notice & OBJ_NOTICE_IMAGINED) {
			delist_object(knownc, obj);
			object_delete(player->cave, NULL, &obj);
			original->known = NULL;
			delist_object(c, original);
			object_delete(cave, player->cave, &original);
		}
		obj = next;
	}
}

/**
 * Sense objects around the player.  The height to sense above and below the
 * player is context->y, the width either side of the player context->x
 */
bool effect_handler_SENSE_OBJECTS(effect_handler_context_t *context)
{
	int x, y;
	int x1, x2, y1, y2;

	bool objects = false;

	/* Pick an area to sense */
	y1 = player->grid.y - context->y;
	y2 = player->grid.y + context->y;
	x1 = player->grid.x - context->x;
	x2 = player->grid.x + context->x;

	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (y2 > cave->height - 1) y2 = cave->height - 1;
	if (x2 > cave->width - 1) x2 = cave->width - 1;

	/* Scan the area for objects */
	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			struct loc grid = loc(x, y);
			struct object *obj = square_object(cave, grid);

			if (!obj) {
				/* If empty, remove any remembered objects. */
				forget_remembered_objects(cave, player->cave, grid);
				continue;
			}

			/* Notice an object is detected */
			objects = true;

			/* Mark the pile as aware */
			square_sense_pile(cave, grid);
		}
	}

	if (objects)
		msg("You sense the presence of objects!");
	else if (context->aware)
		msg("You sense no objects.");

	/* Redraw whole map, monster list */
	player->upkeep->redraw |= PR_ITEMLIST;

	context->ident = true;
	return true;
}

/**
 * Detect objects around the player.  The height to detect above and below the
 * player is context->y, the width either side of the player context->x
 */
bool effect_handler_DETECT_OBJECTS(effect_handler_context_t *context)
{
	int x, y;
	int x1, x2, y1, y2;

	bool objects = false;

	/* Pick an area to detect */
	y1 = player->grid.y - context->y;
	y2 = player->grid.y + context->y;
	x1 = player->grid.x - context->x;
	x2 = player->grid.x + context->x;

	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (y2 > cave->height - 1) y2 = cave->height - 1;
	if (x2 > cave->width - 1) x2 = cave->width - 1;

	/* Scan the area for objects */
	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			struct loc grid = loc(x, y);
			struct object *obj = square_object(cave, grid);

			if (!obj) {
				/* If empty, remove any remembered objects. */
				forget_remembered_objects(cave, player->cave, grid);
				continue;
			}

			/* Notice an object is detected */
			if (!ignore_item_ok(player, obj)) {
				objects = true;
			}

			/* Mark the pile as seen */
			square_know_pile(cave, grid);
		}
	}

	if (objects)
		msg("You detect the presence of objects!");
	else if (context->aware)
		msg("You detect no objects.");

	/* Redraw whole map, monster list */
	player->upkeep->redraw |= PR_ITEMLIST;

	context->ident = true;
	return true;
}

/**
 * Detect monsters which satisfy the given predicate around the player.
 * The height to detect above and below the player is y_dist,
 * the width either side of the player x_dist.
 */
static bool detect_monsters(int y_dist, int x_dist, monster_predicate pred)
{
	int i, x, y;
	int x1, x2, y1, y2;

	bool monsters = false;

	/* Set the detection area */
	y1 = player->grid.y - y_dist;
	y2 = player->grid.y + y_dist;
	x1 = player->grid.x - x_dist;
	x2 = player->grid.x + x_dist;

	if (y1 < 0) y1 = 0;
	if (x1 < 0) x1 = 0;
	if (y2 > cave->height - 1) y2 = cave->height - 1;
	if (x2 > cave->width - 1) x2 = cave->width - 1;

	/* Scan monsters */
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);

		/* Skip dead monsters */
		if (!mon->race) continue;

		/* Location */
		y = mon->grid.y;
		x = mon->grid.x;

		/* Only detect nearby monsters */
		if (x < x1 || y < y1 || x > x2 || y > y2) continue;

		/* Detect all appropriate, obvious monsters */
		if (pred(mon) && !monster_is_camouflaged(mon)) {
			/* Detect the monster */
			mflag_on(mon->mflag, MFLAG_MARK);
			mflag_on(mon->mflag, MFLAG_SHOW);

			/* Note invisible monsters */
			if (monster_is_invisible(mon)) {
				struct monster_lore *lore = get_lore(mon->race);
				rf_on(lore->flags, RF_INVISIBLE);
			}

			/* Update monster recall window */
			if (player->upkeep->monster_race == mon->race)
				/* Redraw stuff */
				player->upkeep->redraw |= (PR_MONSTER);

			/* Update the monster */
			update_mon(player, mon, cave, false);

			/* Detect */
			monsters = true;
		}
	}

	return monsters;
}

/**
 * Detect living monsters around the player.  The height to detect above and
 * below the player is context->value.dice, the width either side of the player
 * context->value.sides.
 */
bool effect_handler_DETECT_LIVING_MONSTERS(effect_handler_context_t *context)
{
	bool monsters = detect_monsters(context->y, context->x, monster_is_living);

	if (monsters)
		msg("You sense life!");
	else if (context->aware)
		msg("You sense no life.");

	context->ident = true;
	return true;
}


/**
 * Detect visible monsters around the player; note that this means monsters
 * which are in principle visible, not monsters the player can currently see.
 *
 * The height to detect above and
 * below the player is context->value.dice, the width either side of the player
 * context->value.sides.
 */
bool effect_handler_DETECT_VISIBLE_MONSTERS(effect_handler_context_t *context)
{
	bool monsters = detect_monsters(context->y, context->x,
									monster_is_not_invisible);

	if (monsters)
		msg("You sense the presence of monsters!");
	else if (context->aware)
		msg("You sense no monsters.");

	context->ident = true;
	return true;
}


/**
 * Detect invisible monsters around the player.  The height to detect above and
 * below the player is context->value.dice, the width either side of the player
 * context->value.sides.
 */
bool effect_handler_DETECT_INVISIBLE_MONSTERS(effect_handler_context_t *context)
{
	bool monsters = detect_monsters(context->y, context->x,
									monster_is_invisible);

	if (monsters)
		msg("You sense the presence of invisible creatures!");
	else if (context->aware)
		msg("You sense no invisible creatures.");

	context->ident = true;
	return true;
}

/**
 * Detect monsters susceptible to fear around the player.  The height to detect
 * above and below the player is context->value.dice, the width either side of
 * the player context->value.sides.
 */
bool effect_handler_DETECT_FEARFUL_MONSTERS(effect_handler_context_t *context)
{
	bool monsters = detect_monsters(context->y, context->x, monster_is_fearful);

	if (monsters)
		msg("These monsters could provide good sport.");
	else if (context->aware)
		msg("You smell no fear in the air.");

	context->ident = true;
	return true;
}

/**
 * Detect evil monsters around the player.  The height to detect above and
 * below the player is context->value.dice, the width either side of the player
 * context->value.sides.
 */
bool effect_handler_DETECT_EVIL(effect_handler_context_t *context)
{
	bool monsters = detect_monsters(context->y, context->x, monster_is_evil);

	if (monsters)
		msg("You sense the presence of evil creatures!");
	else if (context->aware)
		msg("You sense no evil creatures.");

	context->ident = true;
	return true;
}

/**
 * Detect monsters possessing a spirit around the player.
 * The height to detect above and below the player is context->value.dice,
 * the width either side of the player context->value.sides.
 */
bool effect_handler_DETECT_SOUL(effect_handler_context_t *context)
{
	bool monsters = detect_monsters(context->y, context->x, monster_has_spirit);

	if (monsters)
		msg("You sense the presence of spirits!");
	else if (context->aware)
		msg("You sense no spirits.");

	context->ident = true;
	return true;
}

/**
 * Identify an unknown icon of an item.
 */
bool effect_handler_IDENTIFY(effect_handler_context_t *context)
{
	struct object *obj;
	const char *q, *s;
	int itemmode = (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR);
	bool used = false;

	context->ident = true;

	/* Get an item */
	q = "Identify which item? ";
	s = "You have nothing to identify.";
	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, q, s,
				item_tester_unknown, itemmode)) {
			return used;
		}
	} else if (!get_item(&obj, q, s, 0, item_tester_unknown, itemmode))
		return used;

	/* Identify the object */
	object_learn_unknown_icon(player, obj);

	return true;
}

/**
 * Convert an item into blocks.
 */
bool effect_handler_RECYCLE(effect_handler_context_t *context)
{
	struct object *obj;
	const char *q, *s;
	int itemmode = (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR);
	bool used = false;

	context->ident = true;

	/* Get an item */
	q = "Recycle which item into blocks? ";
	s = "You have nothing which you could recycle.";
	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, q, s,
				item_tester_recyclable, itemmode)) {
			return used;
		}
	} else if (!get_item(&obj, q, s, 0, item_tester_recyclable, itemmode))
		return used;

	/* Recycle the object. First print a message. */
	int blocks = recyclable_blocks(obj);
	const struct object_material *mat = material + obj->kind->material;
	if (blocks > 1)
		msg("You convert it into %d %s blocks.", blocks, mat->name);
	else
		msg("You convert it into a %s block.", mat->name);

	/* Then remove it */
	bool none_left = false;
	struct object *destroyed;
	if (object_is_carried(player, obj)) {
		destroyed = gear_object_for_use(player, obj, 1, false, &none_left);
		if (destroyed->artifact) {
			/* Artifacts are marked as lost */
			history_lose_artifact(player, destroyed->artifact);
		}
		object_delete(player->cave, NULL, &destroyed->known);
		object_delete(cave, player->cave, &destroyed);
	}

	/* Return blocks */
	struct start_item item = { TV_BLOCK, 0, 1, 1 };
	item.sval = lookup_sval(TV_BLOCK, mat->name);
	item.min = item.max = blocks;
	add_start_items(player, &item, false, false, ORIGIN_CHEAT);

	return true;
}

/**
 * Identify an unknown fault icon of an item.
 */
bool effect_handler_ID_FAULT(effect_handler_context_t *context)
{
	struct object *obj;
	const char *q, *s;
	int itemmode = (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR);
	bool used = false;

	context->ident = true;

	/* Get an item */
	q = "Identify faults on which item? ";
	s = "You have nothing with unidentified faults.";
	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, q, s,
				item_tester_unknown_faults, itemmode)) {
			return used;
		}
	} else if (!get_item(&obj, q, s, 0, item_tester_unknown_faults, itemmode))
		return used;

	/* Identify the object */
	object_learn_unknown_faults(player, obj);

	return true;
}

/**
 * Remove the fragile status from an item.
 */
bool effect_handler_REMOVE_FRAGILE(effect_handler_context_t *context)
{
	struct object *obj;
	const char *q, *s;
	int itemmode = (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR);
	bool used = false;

	context->ident = true;

	/* Get an item */
	q = "Rebuild which item? ";
	s = "You have nothing to rebuild.";
	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, q, s,
				item_tester_fragile, itemmode)) {
			return used;
		}
	} else if (!get_item(&obj, q, s, 0, item_tester_fragile, itemmode))
		return used;

	/* Remove fragile bit */
	msg("You rebuild it, making it no longer fragile. (It may still have faults.)");
	of_off(obj->flags, OF_FRAGILE);

	return true;
}

/**
 * Create stairs at the player location
 */
bool effect_handler_CREATE_STAIRS(effect_handler_context_t *context)
{
	context->ident = true;

	/* Only allow stairs to be created on empty floor */
	if (!square_isfloor(cave, player->grid)) {
		msg("There is no empty floor here.");
		return false;
	}

	/* Fails for persistent levels (for now) */
	if (OPT(player, birth_levels_persist)) {
		msg("Nothing happens!");
		return false;
	}

	/* Push objects off the grid */
	if (square_object(cave, player->grid))
		push_object(player->grid);

	square_add_stairs(cave, player->grid, player->depth);

	return true;
}

/**
 * Apply disenchantment to the player's stuff.
 */
bool effect_handler_DISENCHANT(effect_handler_context_t *context)
{
	int i, count = 0;
	struct object *obj;
	char o_name[80];

	/* Count slots */
	for (i = 0; i < player->body.count; i++) {

		/* Ignore lights */
		if (slot_type_is(player, i, EQUIP_LIGHT)) continue;

		/* Count disenchantable slots */
		count++;
	}

	/* Pick one at random */
	for (i = player->body.count - 1; i >= 0; i--) {

		/* Ignore lights */
		if (slot_type_is(player, i, EQUIP_LIGHT)) continue;

		if (one_in_(count--)) break;
	}

	/* Notice */
	context->ident = true;

	/* Get the item */
	obj = slot_object(player, i);

	/* No item, nothing happens */
	if (!obj) return true;

	/* Nothing to disenchant */
	if ((obj->to_h <= 0) && (obj->to_d <= 0) && (obj->to_a <= 0))
		return true;

	/* Describe the object */
	object_desc(o_name, sizeof(o_name), obj, ODESC_BASE, player);

	/* Artifacts have a 60% chance to resist */
	if (obj->artifact && (randint0(100) < 60)) {
		/* Message */
		msg("Your %s (%c) resist%s disenchantment!", o_name, I2A(i),
			((obj->number != 1) ? "" : "s"));

		return true;
	}

	/* Apply disenchantment, depending on which kind of equipment */

	if (slot_type_is(player, i, EQUIP_WEAPON) || slot_type_is(player, i, EQUIP_GUN)) {
		/* Disenchant to-hit */
		if (obj->to_h > 0) obj->to_h--;
		if ((obj->to_h > 5) && (randint0(100) < 20)) obj->to_h--;
		obj->known->to_h = obj->to_h;

		/* Disenchant to-dam */
		if (obj->to_d > 0) obj->to_d--;
		if ((obj->to_d > 5) && (randint0(100) < 20)) obj->to_d--;
		obj->known->to_d = obj->to_d;
	} else {
		/* Disenchant to-ac */
		if (obj->to_a > 0) obj->to_a--;
		if ((obj->to_a > 5) && (randint0(100) < 20)) obj->to_a--;
		obj->known->to_a = obj->to_a;
	}

	/* Message */
	msg("Your %s (%c) %s disenchanted!", o_name, I2A(i),
		((obj->number != 1) ? "were" : "was"));

	/* Recalculate bonuses */
	player->upkeep->update |= (PU_BONUS);

	/* Window stuff */
	player->upkeep->redraw |= (PR_EQUIP);

	return true;
}

/**
 * Enchant an item (in the inventory or on the floor)
 * Note that armour, to hit or to dam is controlled by context->subtype
 *
 * Work on incorporating enchant_spell() has been postponed...NRM
 */
bool effect_handler_ENCHANT(effect_handler_context_t *context)
{
	int value = randcalc(context->value, player->depth, RANDOMISE);
	bool used = false;
	context->ident = true;

	if ((context->subtype & ENCH_TOBOTH) == ENCH_TOBOTH) {
		if (enchant_spell(value, value, 0, 0, 0, context->cmd))
			used = true;
	}
	else if (context->subtype & ENCH_TOHIT) {
		if (enchant_spell(value, 0, 0, 0, 0, context->cmd))
			used = true;
	}
	else if (context->subtype & ENCH_TODAM) {
		if (enchant_spell(0, value, 0, 0, 0, context->cmd))
			used = true;
	}
	if (context->subtype & ENCH_TOAC) {
		if (enchant_spell(0, 0, value, 0, 0, context->cmd))
			used = true;
	}

	return used;
}

/**
 * Recharge a wand or staff from the pack or on the floor.  Recharge strength
 * is context->value.base.
 *
 * It is harder to recharge high level, and highly charged wands.
 */
bool effect_handler_RECHARGE(effect_handler_context_t *context)
{
	int i, t;
	int strength = context->value.base;
	int itemmode = (USE_INVEN | USE_FLOOR | SHOW_RECHARGE);
	struct object *obj;
	bool used = false;
	const char *q, *s;

	/* Immediately obvious */
	context->ident = true;

	/* Used to show recharge failure rates */
	player->upkeep->recharge_pow = strength;

	/* Get an item */
	q = "Recharge which item? ";
	s = "You have nothing to recharge.";
	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, q, s,
				tval_can_have_charges, itemmode)) {
			return used;
		}
	} else if (!get_item(&obj, q, s, 0, tval_can_have_charges, itemmode)) {
		return (used);
	}

	i = recharge_failure_chance(obj, strength);
	/* Back-fire */
	if ((i <= 1) || one_in_(i)) {
		struct object *destroyed;
		bool none_left = false;

		msg("The recharge backfires!");
		msg("There is a bright flash of light.");

		/* Reduce and describe inventory */
		if (object_is_carried(player, obj)) {
			destroyed = gear_object_for_use(player, obj, 1, true,
				&none_left);
		} else {
			destroyed = floor_object_for_use(player, obj, 1, true,
				&none_left);
		}
		if (destroyed->known)
			object_delete(player->cave, NULL, &destroyed->known);
		object_delete(cave, player->cave, &destroyed);
	} else {
		/* Extract a "power" */
		int ease_of_recharge = (100 - obj->kind->level) / 10;
		t = (strength / (10 - ease_of_recharge)) + 1;

		/* Recharge based on the power */
		if (t > 0) obj->pval += 2 + randint1(t);
	}

	/* Combine the pack (later) */
	player->upkeep->notice |= (PN_COMBINE);

	/* Redraw stuff */
	player->upkeep->redraw |= (PR_INVEN);

	/* Something was done */
	return true;
}

bool effect_handler_ACQUIRE(effect_handler_context_t *context)
{
	int num = effect_calculate_value(context, false);
	acquirement(player->grid, player->depth, num, true);
	context->ident = true;
	return true;
}

bool effect_handler_LOCAL_ACQUIRE(effect_handler_context_t *context)
{
	struct object *best = NULL;
	struct object *obj;
	int bestprice = -1;
	 
	/* All objects on the ground */
	for (int y = 1; y < cave->height; y++) {
		for (int x = 1; x < cave->width; x++) {
			struct loc grid = loc(x, y);
			for (obj = square_object(cave, grid); obj; obj = obj->next) {
				int val = object_value_real(obj, obj->number);
				if (val > bestprice) {
					best = obj;
					bestprice = val;
				}
			}
		}
	}

	if (!best) {
		/* Almost impossible, but could happen if there were no objects on the level */
		return effect_handler_ACQUIRE(context);
	}

	/* Remove it */
	bool dummy;
	struct object *removed = floor_object_for_use(player, best, best->number, false, &dummy);

	/* And replace it */
	drop_near(cave, &removed, 0, player->grid, true, true);

	context->ident = true;
	return true;
}

/**
 * Wake up all monsters in line of sight
 */
bool effect_handler_WAKE(effect_handler_context_t *context)
{
	int i;
	bool woken = false;

	struct loc origin = origin_get_loc(context->origin);

	/* Wake everyone nearby */
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);
		if (mon->race) {
			int radius = z_info->max_sight * 2;
			int dist = distance(origin, mon->grid);

			/* Skip monsters too far away */
			if ((dist < radius) && mon->m_timed[MON_TMD_SLEEP]) {
				/* Monster wakes, closer means likelier to become aware */
				monster_wake(mon, false, 100 - 2 * dist);
				woken = true;
			}
		}
	}

	/* Messages */
	if (woken) {
		msg("You hear a sudden stirring in the distance!");
	}

	context->ident = true;

	return true;
}

/**
 * Summon context->value monsters of context->subtype type.
 */
bool effect_handler_SUMMON(effect_handler_context_t *context)
{
	int summon_max = effect_calculate_value(context, false);
	int summon_type = context->subtype;
	int level_boost = context->other;
	int message_type = summon_message_type(summon_type);
	int fallback_type = summon_fallback_type(summon_type);
	int count = 0, val = 0, attempts = 0;
	char *grow = NULL;

	sound(message_type);

	/* No summoning in arena levels */
	if (player->upkeep->arena_level) return true;

	/* Monster summon */
	if (context->origin.what == SRC_MONSTER) {
		struct monster *mon = cave_monster(cave, context->origin.which.monster);
		int rlev;

		assert(mon);

		/* Set the kin_base if necessary */
		if (summon_type == summon_name_to_idx("KIN")) {
			kin_base = mon->race->base;
		} else if (summon_type == summon_name_to_idx("GROW")) {
			grow = mon->race->grow;
		}

		if (grow) {
			count = summon_named_near(mon->grid, grow);
		} else {
			/* Continue summoning until we reach the current dungeon level */
			rlev = mon->race->level;
			while ((val < player->depth * rlev) && (attempts < summon_max)) {
				int temp;

				/* Get a monster */
				temp = summon_specific(mon->grid, mon->race, rlev + level_boost, summon_type,
									   false, false);

				val += temp * temp;

				/* Increase the attempt in case no monsters were available. */
				attempts++;

				/* Increase count of summoned monsters */
				if (val > 0)
					count++;
			}

			/* If the summon failed and there's a fallback type, use that */
			if ((count == 0) && (fallback_type >= 0)) {
				attempts = 0;
				while ((val < player->depth * rlev) && (attempts < summon_max)) {
					int temp;

					/* Get a monster */
					temp = summon_specific(mon->grid, mon->race, rlev + level_boost,
										   fallback_type, false, false);

					val += temp * temp;

					/* Increase the attempt in case no monsters were available. */
					attempts++;

					/* Increase count of summoned monsters */
					if (val > 0)
						count++;
				}
			}
		}

		/* Summoner failed */
		if (!count) {
			if (grow)
				msg("But nothing happens.");
			else
				msg("But nothing comes.");
		}
	} else {
		/* If not a monster summon, it's simple */
		while (summon_max) {
			count += summon_specific(player->grid, NULL, player->depth + level_boost,
									 summon_type, true, one_in_(4));
			summon_max--;
		}
	}

	/* Identify */
	context->ident = true;

	/* Message for the blind */
	if (count && player->timed[TMD_BLIND])
		msgt(message_type, "You hear %s appear nearby.",
			 (count > 1 ? "many things" : "something"));

	return true;
}

/* Is a monster banish-proof?
 * This is done to avoid quests being too easy or unwinnable
 */
static bool monster_is_banproof(struct monster *mon)
{
	/* If you are on a quest, at least prevent making the quest unwinnable (immediately,
	 * without waiting for wandering monsters to show up)
	 **/
	if (player->active_quest >= 0) {
		struct quest *q = &player->quests[player->active_quest];
		if (q->flags & QF_HOME) {
			/* Home quest = kill all monsters. So don't take the last one here */
			if (cave->mon_cnt == 1)
				return true;
		}
	}

	/* Similar in town quests */
	if (in_town_quest()) {
		for(int i=0;i<z_info->quest_max;i++) {
			struct quest *q = &player->quests[i];
			/* Matching this town, active and a town quest */
			if (q->town == player->town - t_info) {
				if ((q->flags & (QF_TOWN | QF_ACTIVE)) == (QF_TOWN | QF_ACTIVE)) {
					bool wanted = false;
					/* Is it a race wanted by the quest? */
					for(int i=0; i<q->races; i++) {
						if (mon->race == q->race[i]) {
							wanted = true;
							break;
						}
					}
					if (wanted) {
						/* The last one */
						if (q->cur_num >= q->max_num-1) {
							return true;
						}
						/* Not in line of sight */
						if (!los(cave, player->grid, mon->grid))
							return true;
					}
				}
			}
		}
	}

	/* Level guardians, wintrack monsters and most other quest targets are
	 * unique, will come back and the quest won't be failed if you zap it.
	 * But you'll have to take it down from full health again, so you
	 * haven't made it any easier either. So they don't need protection.
	 *
	 * There are also some quests (such as the wumpus quest) which can't be
	 * repeated, but in which banishment is just another way to bail out and
	 * fail the quest. There's no reason not to allow this either.
	 **/
	return false;
}

/* Banish a monster, unless it is banish-proof in which case teleport it away. */
static void banish_monster(effect_handler_context_t *context, struct monster *mon, int i)
{
	if (monster_is_banproof(mon)) {
		effect_simple(EF_TELEPORT, context->origin, "100", 0, 0, 0,
					  mon->grid.y, mon->grid.x, NULL);

		/* Wake the monster up, don't notice the player */
		monster_wake(mon, false, 0);
	} else
		delete_monster_idx(i);
}

/**
 * Delete all non-unique monsters of a given "type" from the level
 * -------
 * Warning - this function assumes that the entered monster symbol is an ASCII
 *		   character, which may not be true in the future - NRM
 * -------
 */
bool effect_handler_BANISH(effect_handler_context_t *context)
{
	int i;
	unsigned dam = 0;

	char typ;

	context->ident = true;

	/* Don't allow in an arena. */
	if (player->upkeep->arena_level) {
		msg("Nothing happens.");
		return true;
	}

	if (!get_com("Choose a monster race (by symbol) to exterminate: ", &typ))
		return false;

	/* Delete the monsters of that "type" */
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);

		/* Paranoia -- Skip dead monsters */
		if (!mon->race) continue;

		/* Hack -- Skip Unique Monsters */
		if (monster_is_unique(mon)) continue;

		/* Skip "wrong" monsters (see warning above) */
		if ((char) mon->race->d_char != typ) continue;

		/* Delete the monster */
		banish_monster(context, mon, i);

		/* Take some damage */
		dam += randint1(4);
	}

	/* Hurt the player */
	take_hit(player, dam, "the strain of extermination");

	/* Update monster list window */
	player->upkeep->redraw |= PR_MONLIST;

	/* Success */
	return true;
}

/**
 * Delete all nearby (non-unique) monsters.  The radius of effect is
 * context->radius if passed, otherwise the player view radius.
 */
bool effect_handler_MASS_BANISH(effect_handler_context_t *context)
{
	int i;
	int radius = context->radius ? context->radius : z_info->max_sight;
	unsigned dam = 0;

	context->ident = true;

	/* Don't allow in an arena. */
	if (player->upkeep->arena_level) {
		msg("Nothing happens.");
		return true;
	}

	/* Delete the (nearby) monsters */
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);

		/* Paranoia -- Skip dead monsters */
		if (!mon->race) continue;

		/* Hack -- Skip unique monsters */
		if (monster_is_unique(mon)) continue;

		/* Skip distant monsters */
		if (mon->cdis > radius) continue;

		banish_monster(context, mon, i);

		/* Take some damage */
		dam += randint1(3);
	}

	/* Hurt the player */
	take_hit(player, dam, "the strain of mass extermination");

	/* Update monster list window */
	player->upkeep->redraw |= PR_MONLIST;

	return true;
}

/**
 * Probe nearby monsters
 */
bool effect_handler_PROBE(effect_handler_context_t *context)
{
	int i;

	bool probe = false;

	/* Probe all (nearby) monsters */
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);

		/* Paranoia -- Skip dead monsters */
		if (!mon->race) continue;

		/* Require line of sight */
		if (!square_isview(cave, mon->grid)) continue;

		/* Probe visible monsters */
		if (monster_is_visible(mon)) {
			char m_name[80];

			/* Start the message */
			if (!probe) msg("Probing...");

			/* Get "the monster" or "something" */
			monster_desc(m_name, sizeof(m_name), mon,
					MDESC_IND_HID | MDESC_CAPITAL);

			/* Describe the monster */
			msg("%s has %d hit point%s.", m_name, mon->hp, (mon->hp == 1) ? "" : "s");

			/* Learn all of the non-spell, non-treasure flags */
			lore_do_probe(mon);

			/* Probe worked */
			probe = true;
		}
	}

	/* Done */
	if (probe) {
		msg("That's all.");
		context->ident = true;
	}

	return true;
}

/** Teleport player to the nearest portal in the level, if
 * there is one. If there isn't behaves as TELEPORT.
 */
bool effect_handler_PORTAL(effect_handler_context_t *context)
{
	struct loc start = loc(context->x, context->y);
	struct monster *t_mon = monster_target_monster(context);
	bool is_player = (context->origin.what != SRC_MONSTER || context->subtype);
	int dis = context->value.base;

	/* No teleporting in arena levels */
	if (player->upkeep->arena_level) return true;

	/* Establish the coordinates to teleport from, if we don't know already */
	if (!loc_is_zero(start)) {
		/* We're good */
	} else if (t_mon) {
		/* Monster targeting another monster */
		start = t_mon->grid;
	} else if (is_player) {
		/* Decoys get destroyed */
		struct loc decoy = cave_find_decoy(cave);
		if (!loc_is_zero(decoy) && context->subtype) {
			square_destroy_decoy(cave, decoy);
			return true;
		}

		start = player->grid;

		/* Check for a no teleport grid */
		if (square_isno_teleport(cave, start) &&
			((dis > 10) || (dis == 0))) {
			msg("Teleportation forbidden!");
			return true;
		}

		/* Check for a no teleport fault */
		if (player_of_has(player, OF_NO_TELEPORT)) {
			equip_learn_flag(player, OF_NO_TELEPORT);
			msg("Teleportation forbidden!");
			return true;
		}
	} else {
		assert(context->origin.what == SRC_MONSTER);
		struct monster *mon = cave_monster(cave, context->origin.which.monster);
		start = mon->grid;
	}

	/* Find the closest portal */
	struct loc target;
	struct loc grid;
	int best = -1;
	struct trap_kind *portal = lookup_trap("portal");
	bool found = false;
	for (grid.y = 1; grid.y < cave->height - 1; grid.y++) {
		for (grid.x = 1; grid.x < cave->width - 1; grid.x++) {
			/* Not this portal. That includes being one step away, as you would be if
			 * setting off a trap as you enter the grid.
			 **/
			if ((ABS(grid.x - start.x) <= 1) && (ABS(grid.y - start.y) <= 1))
				continue;
			/* Not a portal */
			if (!square_trap_specific(cave, grid, portal->tidx))
				continue;
			int d = distance(grid, start);
			if (!found) {
				found = true;
				best = d;
				target = grid;
			} else {
				if (d < best) {
					best = d;
					target = grid;
				}
			}
		}
	}

	/* If there is none, teleport instead */
	if (!found) {
		return effect_handler_TELEPORT(context);
	} else {
		/* Sound */
		sound(is_player ? MSG_TELEPORT : MSG_TPOTHER);

		/* Move player */
		monster_swap(start, target);

		/* Clear any projection marker to prevent double processing */
		sqinfo_off(square(cave, target)->info, SQUARE_PROJECT);

		/* Clear monster target if it's no longer visible */
		if (!target_able(target_get_monster())) {
			target_set_monster(NULL);
		}

		/* Lots of updates after monster_swap */
		handle_stuff(player);
	}
	return true;
}

/** Find a teleportable-to grid, returned in result.
 * Returns true if one can be found.
 * The grid is as close to distance 'dis' from 'start' as possible, but can be anywhere on the level
 * if this isn't available.
 */
static bool find_teleportable(struct loc start, int dis, struct loc *result, bool is_player, bool is_trap, bool fixed_dis, bool in_los)
{
	struct loc grid;
	bool only_vault_grids_possible = true;
	int pick;
	int num_spots = 0;
	int current_score = 2 * MAX(z_info->dungeon_wid, z_info->dungeon_hgt);

	struct jumps {
		struct loc grid;
		struct jumps *next;
	} *spots = NULL;

	/* Make a list of the best grids, scoring by how good an approximation
	 * the distance from the start is to the distance we want */
	for (grid.y = 1; grid.y < cave->height - 1; grid.y++) {
		for (grid.x = 1; grid.x < cave->width - 1; grid.x++) {
			int d = distance(grid, start);
			int score = ABS(d - dis);
			struct jumps *new;

			/* If "fixed_dis" then only exact distances are acceptable */
			if ((d != dis) && (fixed_dis)) continue;

			/* Must move */
			if (d == 0) continue;

			/* Require "naked" floor space */
			if (!square_isempty(cave, grid)) continue;

			/* Require acceptable place for a trap */
			if ((is_trap) && (!square_player_trap_allowed(cave, grid))) continue;

			/* No monster teleport onto glyph of warding */
			if (!is_player && square_iswarded(cave, grid)) continue;

			/* If "in_los" then there must be nothing in the way */
			if (in_los && (!los(cave, start, grid))) continue;

			/* No teleporting into vaults and such, unless there's no choice */
			if (square_isvault(cave, grid)) {
				if (!only_vault_grids_possible) {
					continue;
				}
			} else {
				/* Just starting to consider non-vault grids, so reset score */
				if (only_vault_grids_possible) {
					current_score = 2 * MAX(z_info->dungeon_wid,
											z_info->dungeon_hgt);
				}
				only_vault_grids_possible = false;
			}

			/* Do we have better spots already? */
			if (score > current_score) continue;

			/* Make a new spot */
			new = mem_zalloc(sizeof(struct jumps));
			new->grid = grid;

			/* If improving start a new list, otherwise extend the old one */
			if (score < current_score) {
				current_score = score;
				while (spots) {
					struct jumps *next = spots->next;
					mem_free(spots);
					spots = next;
				}
				spots = new;
				num_spots = 1;
			} else {
				new->next = spots;
				spots = new;
				num_spots++;
			}
		}
	}

	/* Report failure (very unlikely) */
	if (!num_spots) {
		msg("Failed to find teleport destination!");
		return false;
	}

	/* Pick a spot */
	pick = randint0(num_spots);
	while (pick) {
		struct jumps *next = spots->next;
		mem_free(spots);
		spots = next;
		pick--;
	}

	*result = spots->grid;

	while (spots) {
		struct jumps *next = spots->next;
		mem_free(spots);
		spots = next;
	}

	return true;
}

/** Create a portal pair. Doesn't jump through. */
bool effect_handler_CREATE_PORTAL(effect_handler_context_t *context)
{
	/* Find a place to teleport to */
	struct loc start = loc(context->x, context->y);
	int dis = context->value.base;
	dis += randint0(dis / 2) - (dis / 4);
	struct loc result;

	/* Get start position */
	if (loc_is_zero(start))
		start = player->grid;

	/* Find a target position */
	if (!find_teleportable(start, dis, &result, true, true, false, false)) {
		msg("Failed to find portal destination!");
		return true;
	}

	struct trap_kind *trap_kind = lookup_trap("portal");
	assert(trap_kind);

	/* Make them visible and friendly */
	struct trap *trap = place_trap(cave, start, trap_kind->tidx, 0);
	if (trap) {
		trf_on(trap->flags, TRF_HOMEMADE);
		trf_on(trap->flags, TRF_VISIBLE);
		square_reveal_trap(cave, start, true, false);
	}
	struct trap *trap_far = place_trap(cave, result, trap_kind->tidx, 0);
	if (trap_far) {
		trf_on(trap_far->flags, TRF_HOMEMADE);
		trf_on(trap_far->flags, TRF_VISIBLE);
		square_reveal_trap(cave, result, true, false);
	}

	if ((!trap) || (!trap_far)) {
		const char *end = "";
		if (trap)
			end = " at the far end";
		else if (trap_far)
			end = " under you";
		msg("Something prevents the portal from taking hold%s.", end);
	}

	return true;
}

/** Teleport player or monster exactly context->value.base grids away.
 * There must be a clear line of movement to that point - it can't cross walls,
 * although it can cross traps, hostile terrain and monsters.
 * 
 * This is not considered to be a form of teleportation - just rapid movement.
 * So it's not blocked by arenas, etc.
 */
bool effect_handler_HOP(effect_handler_context_t *context)
{
	struct loc start = loc(context->x, context->y);
	int dis = context->value.base;
	bool is_player = (context->origin.what != SRC_MONSTER || context->subtype);
	struct monster *t_mon = monster_target_monster(context);

	/* Establish the coordinates to teleport from, if we don't know already */
	if (!loc_is_zero(start)) {
		/* We're good */
	} else if (t_mon) {
		/* Monster targeting another monster */
		start = t_mon->grid;
	} else if (is_player) {
		start = player->grid;
	} else {
		assert(context->origin.what == SRC_MONSTER);
		struct monster *mon = cave_monster(cave, context->origin.which.monster);
		start = mon->grid;
	}

	struct loc result;
	if (!find_teleportable(start, dis, &result, is_player, false, true, true)) {
		/* Hop failed - this is common, no message */
		return true;
	}

	/* Sound */
	sound(is_player ? MSG_TELEPORT : MSG_TPOTHER);

	/* Move player/monster */
	monster_swap(start, result);

	/* Clear any projection marker to prevent double processing */
	sqinfo_off(square(cave, result)->info, SQUARE_PROJECT);

	/* Clear monster target if it's no longer visible */
	if (!target_able(target_get_monster())) {
		target_set_monster(NULL);
	}

	/* Lots of updates after monster_swap */
	handle_stuff(player);

	return true;
}


/**
 * Teleport player or monster up to context->value.base grids away.
 *
 * If no spaces are readily available, the distance may increase.
 * Try very hard to move the player/monster at least a quarter that distance.
 * Setting context->subtype allows monsters to teleport the player away.
 * Setting context->y and context->x treats them as y and x coordinates
 * and teleports the monster from that grid.
 */
bool effect_handler_TELEPORT(effect_handler_context_t *context)
{
	struct loc start = loc(context->x, context->y);
	int dis = context->value.base;
	int perc = context->value.m_bonus;

	bool is_player = (context->origin.what != SRC_MONSTER || context->subtype);
	struct monster *t_mon = monster_target_monster(context);

	context->ident = true;

	/* No teleporting in arena levels */
	if (player->upkeep->arena_level) return true;

	/* Establish the coordinates to teleport from, if we don't know already */
	if (!loc_is_zero(start)) {
		/* We're good */
	} else if (t_mon) {
		/* Monster targeting another monster */
		start = t_mon->grid;
	} else if (is_player) {
		/* Decoys get destroyed */
		struct loc decoy = cave_find_decoy(cave);
		if (!loc_is_zero(decoy) && context->subtype) {
			square_destroy_decoy(cave, decoy);
			return true;
		}

		start = player->grid;

		/* Check for a no teleport grid */
		if (square_isno_teleport(cave, start) &&
			((dis > 10) || (dis == 0))) {
			msg("Teleportation forbidden!");
			return true;
		}

		/* Check for a no teleport fault */
		if (player_of_has(player, OF_NO_TELEPORT)) {
			equip_learn_flag(player, OF_NO_TELEPORT);
			msg("Teleportation forbidden!");
			return true;
		}
	} else {
		assert(context->origin.what == SRC_MONSTER);
		struct monster *mon = cave_monster(cave, context->origin.which.monster);
		start = mon->grid;
	}

	/* Percentage of the largest cardinal distance to an edge */
	if (perc) {
		int vertical = MAX(start.y, cave->height - start.y);
		int horizontal = MAX(start.x, cave->width - start.x);
		dis = (MAX(vertical, horizontal) * perc) / 100;
	}

	/* Randomise the distance a little */
	if (one_in_(2)) {
		dis -= randint0(dis / 4);
	} else {
		dis += randint0(dis / 4);
	}

	struct loc result;
	if (!find_teleportable(start, dis, &result, is_player, false, false, false)) {
		msg("Failed to find teleport destination!");
		return true;
	}

	/* Sound */
	sound(is_player ? MSG_TELEPORT : MSG_TPOTHER);

	/* Move player */
	monster_swap(start, result);

	/* Clear any projection marker to prevent double processing */
	sqinfo_off(square(cave, result)->info, SQUARE_PROJECT);

	/* Clear monster target if it's no longer visible */
	if (!target_able(target_get_monster())) {
		target_set_monster(NULL);
	}

	/* Lots of updates after monster_swap */
	handle_stuff(player);

	return true;
}


/**
 * Teleport player or target monster to a grid near the given location
 * Setting context->y and context->x treats them as y and x coordinates
 * Setting context->subtype allows monsters to teleport toward the player.
 *
 * This function is slightly obsessive about correctness.
 * This function allows teleporting into vaults (!)
 */
bool effect_handler_TELEPORT_TO(effect_handler_context_t *context)
{
	struct monster *mon = NULL;
	struct loc start, aim, land;
	int dis = 0, ctr = 0, dir = DIR_TARGET;
	struct monster *t_mon = monster_target_monster(context);
	bool dim_door = false;

	context->ident = true;

	/* No teleporting in arena levels */
	if (player->upkeep->arena_level) return true;

	if (context->origin.what == SRC_MONSTER) {
		mon = cave_monster(cave, context->origin.which.monster);
		assert(mon);
	}

	/* Where are we coming from? */
	if (t_mon) {
		/* Monster being teleported */
		start = t_mon->grid;
	} else if (context->subtype) {
		/* Monster teleporting to the player */
		start = mon->grid;
	} else {
		/* Targeted decoys get destroyed */
		if (mon && monster_is_decoyed(mon)) {
			square_destroy_decoy(cave, cave_find_decoy(cave));
			return true;
		}

		/* Player being teleported */
		start = player->grid;

		/* Check for a no teleport grid */
		if (square_isno_teleport(cave, start)) {
			msg("Teleportation forbidden!");
			return true;
		}

		/* Check for a no teleport fault */
		if (player_of_has(player, OF_NO_TELEPORT)) {
			equip_learn_flag(player, OF_NO_TELEPORT);
			msg("Teleportation forbidden!");
			return true;
		}
	}

	/* Where are we going? */
	if (context->y && context->x) {
		/* Effect was given co-ordinates */
		aim = loc(context->x, context->y);
	} else if (mon) {
		/* Spell cast by monster */
		if (context->subtype) {
			/* Monster teleporting to player */
			aim = player->grid;
			dis = 2;
		} else {
			/* Player being teleported to monster */
			aim = mon->grid;
		}
	} else {
		/* Player choice */
		do {
			if (!get_aim_dir(&dir)) return false;
		} while (dir == DIR_TARGET && !target_okay());

		if (dir == DIR_TARGET)
			target_get(&aim);
		else
			aim = loc_offset(start, ddx[dir], ddy[dir]);

		/* Randomise the landing a bit if it's a vault */
		if (square_isvault(cave, aim)) dis = 10;
		dim_door = true;
	}

	/* Find a usable location */
	while (1) {
		/* Pick a nearby legal location */
		while (1) {
			land = rand_loc(aim, dis, dis);
			if (square_in_bounds_fully(cave, land)) break;
		}

		/* Accept "naked" floor grids */
		if (square_isempty(cave, land)) break;

		/* Occasionally advance the distance */
		if (++ctr > (4 * dis * dis + 4 * dis + 1)) {
			ctr = 0;
			dis++;
		}
	}

	/* Sound */
	sound(MSG_TELEPORT);

	/* Move player or monster */
	monster_swap(start, land);

	/* Cancel target if necessary */
	if (dim_door) {
		target_set_location(0, 0);
	}

	/* Clear any projection marker to prevent double processing */
	sqinfo_off(square(cave, land)->info, SQUARE_PROJECT);

	/* Lots of updates after monster_swap */
	handle_stuff(player);

	return true;
}

static bool effect_change_level(effect_handler_context_t *context, const char *rise, const char *sink, bool up, bool down, bool teleport)
{
	int target_depth = dungeon_get_next_level(player, player->max_depth, 1);
	struct monster *t_mon = monster_target_monster(context);
	struct loc decoy = cave_find_decoy(cave);

	context->ident = true;

	/* No changing level in arena levels */
	if (player->upkeep->arena_level) return true;

	/* Check for monster targeting another monster */
	if (t_mon) {
		/* Monster is just gone */
		add_monster_message(t_mon, MON_MSG_DISAPPEAR, false);
		delete_monster_idx(t_mon->midx);
		return true;
	}

	/* Targeted decoys get destroyed */
	if (decoy.y && decoy.x) {
		square_destroy_decoy(cave, decoy);
		return true;
	}

	if (teleport) {
		/* Check for a no teleport grid */
		if (square_isno_teleport(cave, player->grid)) {
			msg("Teleportation forbidden!");
			return true;
		}

		/* Check for a no teleport fault */
		if (player_of_has(player, OF_NO_TELEPORT)) {
			equip_learn_flag(player, OF_NO_TELEPORT);
			msg("Teleportation forbidden!");
			return true;
		}

		/* Resist hostile teleport */
		if (context->origin.what == SRC_MONSTER &&
				player_resists(player, ELEM_NEXUS)) {
			msg("You resist the effect!");
			return true;
		}
	}

	/* No going up with force_descend, in the endgame or in the town */
	if (OPT(player, birth_force_descend) || !player->depth || (!player->town && !player->total_winner))
		up = false;

	/* No forcing player down to quest levels if they can't leave */
	if (!up && is_blocking_quest(player, target_depth))
		down = false;

	/* Can't leave blocking quest levels or go down deeper than the dungeon */
	if (is_blocking_quest(player, player->depth) || (player->depth >= z_info->max_depth - 1))
		down = false;

	/* Determine up/down if not already done */
	if (up && down) {
		if (randint0(100) < 50)
			up = false;
		else
			down = false;
	}

	/* Now actually do the level change */
	if (up) {
		msgt(MSG_TPLEVEL, rise ? rise : "You rise up through the ceiling.");
		target_depth = dungeon_get_next_level(player,
			player->depth, -1);
		dungeon_change_level(player, target_depth);
	} else if (down) {
		msgt(MSG_TPLEVEL, sink ? sink : "You sink through the floor.");

		if (OPT(player, birth_force_descend)) {
			target_depth = dungeon_get_next_level(player,
				player->max_depth, 1);
			dungeon_change_level(player, target_depth);
		} else {
			target_depth = dungeon_get_next_level(player,
				player->depth, 1);
			dungeon_change_level(player, target_depth);
		}
	} else {
		msg("Nothing happens.");
	}

	return true;
}

/**
 * Teleport the player one level up or down (random when legal)
 */
bool effect_handler_TELEPORT_LEVEL(effect_handler_context_t *context)
{
	return effect_change_level(context, NULL, NULL, true, true, true);
}

/**
 * Melt through the floor. Similar to teleport-level, but you can only go one way and it's not considered to be teleportation.
 */
bool effect_handler_MELT_DOWN(effect_handler_context_t *context)
{
	return effect_change_level(context, NULL, "You melt through the floor.", false, true, false);
}

/**
 * The rubble effect
 *
 * This causes rubble to fall into empty squares.
 */
bool effect_handler_RUBBLE(effect_handler_context_t *context)
{
	/*
	 * First we work out how many grids we want to fill with rubble.  Then we
	 * check that we can actually do this, by counting the number of grids
	 * available, limiting the number of rubble grids to this number if
	 * necessary.
	 */
	int rubble_grids = randint1(3);
	int open_grids = count_feats(NULL, square_isempty, false);

	if (rubble_grids > open_grids) {
		rubble_grids = open_grids;
	}

	/* Avoid infinite loops */
	int iterations = 0;

	while (rubble_grids > 0 && iterations < 10) {
		/* Look around the player */
		for (int d = 0; d < 8; d++) {
			/* Extract adjacent (legal) location */
			struct loc grid = loc_sum(player->grid, ddgrid_ddd[d]);
			if (!square_in_bounds_fully(cave, grid)) continue;
			if (!square_isempty(cave, grid)) continue;

			if (one_in_(3)) {
				if (one_in_(2))
					square_set_feat(cave, grid, FEAT_PASS_RUBBLE);
				else
					square_set_feat(cave, grid, FEAT_RUBBLE);
				if (cave->depth == 0)
					expose_to_sun(cave, grid, is_daytime());
				rubble_grids--;
			}
		}

		iterations++;
	}

	context->ident = true;

	/* Fully update the visuals */
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);

	/* Redraw monster list */
	player->upkeep->redraw |= (PR_MONLIST | PR_ITEMLIST);

	return true;
}

bool effect_handler_GRANITE(effect_handler_context_t *context)
{
	struct trap *trap = context->origin.which.trap;
	square_set_feat(cave, trap->grid, FEAT_GRANITE);
	if (cave->depth == 0) expose_to_sun(cave, trap->grid, is_daytime());

	player->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);
	player->upkeep->redraw |= (PR_MONLIST | PR_ITEMLIST);

	return true;
}

bool effect_handler_LIGHT_LEVEL(effect_handler_context_t *context)
{
	bool full = context->value.base ? true : false;
	if (full)
		msg("An image of your surroundings forms in your mind...");
	wiz_light(cave, player, full);
	context->ident = true;
	return true;
}

bool effect_handler_DARKEN_LEVEL(effect_handler_context_t *context)
{
	bool full = context->value.base ? true : false;
	if (full)
		msg("A great blackness rolls through the level...");
	wiz_dark(cave, player, full);
	context->ident = true;
	return true;
}

/**
 * Call light around the player
 */
bool effect_handler_LIGHT_AREA(effect_handler_context_t *context)
{
	/* Message */
	if (!player->timed[TMD_BLIND])
		msg("You are surrounded by a white light.");

	/* Light up the room */
	light_room(player->grid, true);

	/* Assume seen */
	context->ident = true;
	return (true);
}

/**
 * Call darkness around the player or target monster
 */
bool effect_handler_DARKEN_AREA(effect_handler_context_t *context)
{
	struct loc target = player->grid;
	bool message = player->timed[TMD_BLIND] ? false : true;
	struct monster *mon = NULL;
	struct monster *t_mon = monster_target_monster(context);
	struct loc decoy = cave_find_decoy(cave);
	bool decoy_unseen = false;

	if (context->origin.what == SRC_MONSTER) {
		mon = cave_monster(cave, context->origin.which.monster);
	}

	/* Check for monster targeting another monster */
	if (t_mon) {
		char m_name[80];
		target = t_mon->grid;
		monster_desc(m_name, sizeof(m_name), t_mon, MDESC_TARG);
		if (message) {
			msg("Darkness surrounds %s.", m_name);
			message = false;
		}
	}

	/* Check for decoy */
	if (mon && monster_is_decoyed(mon)) {
		target = decoy;
		if (!los(cave, player->grid, decoy) ||
			player->timed[TMD_BLIND]) {
			decoy_unseen = true;
		}
		if (message && !decoy_unseen) {
			msg("Darkness surrounds the decoy.");
			message = false;
		}
	}

	if (message) {
		msg("Darkness surrounds you.");
	}

	/* Darken the room */
	light_room(target, false);

	/* Hack - blind the player directly if player-cast */
	if (context->origin.what == SRC_PLAYER &&
		!player_resists(player, ELEM_DARK)) {
		(void)player_inc_timed(player, TMD_BLIND, 3 + randint1(5), true, true);
	}

	/* Assume seen */
	context->ident = !decoy_unseen;
	return (true);
}

/**
 * Damage the player's armor
 */
bool effect_handler_BLAST_ARMOR(effect_handler_context_t *context)
{
	struct object *obj;

	char o_name[80];

	/* Blast the body armor */
	obj = equipped_item_by_slot_name(player, "body");

	/* Nothing to blast */
	if (!obj) return (true);

	/* Describe */
	object_desc(o_name, sizeof(o_name), obj, ODESC_FULL, player);

	/* Attempt a saving throw for artifacts */
	if (obj->artifact && (randint0(100) < 50)) {
		msg("A fountain of sparks fly from your %s, but it is unaffected!", o_name);
	} else {
		int num = randint1(3);
		int max_tries = 20;
		msg("A fountain of sparks flies from your %s!", o_name);

		/* Take down bonus a wee bit */
		obj->to_a -= randint1(3);

		/* Try to find enough appropriate faults */
		while (num && max_tries) {
			int pick = randint1(z_info->fault_max - 1);
			int power = 10 * m_bonus(9, player->depth);
			if (!faults[pick].poss[obj->tval]) {
				max_tries--;
				continue;
			}
			append_object_fault(obj, pick, power);
			num--;
		}

		/* Recalculate bonuses */
		player->upkeep->update |= (PU_BONUS);

		/* Window stuff */
		player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
	}

	context->ident = true;

	return (true);
}



/**
 * Curse the player's weapon
 */
bool effect_handler_BLAST_WEAPON(effect_handler_context_t *context)
{
	struct object *obj;

	char o_name[80];

	/* Blast the weapon */
	obj = equipped_item_by_slot_name(player, "weapon");

	/* Nothing to blast */
	if (!obj) return (true);

	/* Describe */
	object_desc(o_name, sizeof(o_name), obj, ODESC_FULL, player);

	/* Attempt a saving throw */
	if (obj->artifact && (randint0(100) < 50)) {
		msg("A fountain of sparks fly from your %s, but it is unaffected!", o_name);
	} else {
		int num = randint1(3);
		int max_tries = 20;
		msg("A fountain of sparks flies from your %s!", o_name);

		/* Hurt it a bit */
		obj->to_h = 0 - randint1(3);
		obj->to_d = 0 - randint1(3);

		/* Damage it */
		while (num) {
			int pick = randint1(z_info->fault_max - 1);
			int power = 10 * m_bonus(9, player->depth);
			if (!faults[pick].poss[obj->tval]) {
				max_tries--;
				continue;
			}
			append_object_fault(obj, pick, power);
			num--;
		}

		/* Recalculate bonuses */
		player->upkeep->update |= (PU_BONUS);

		/* Window stuff */
		player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
	}

	context->ident = true;

	/* Notice */
	return (true);
}

/**
 * Mundanify any item which can take an ego
 */
bool effect_handler_UNBRAND_ITEM(effect_handler_context_t *context)
{
	enchant_spell(0, 0, 0, 0, 1, context->cmd);
	context->ident = true;
	return true;
}

/**
 * Brand any item which can take an ego
 */
bool effect_handler_BRAND_ITEM(effect_handler_context_t *context)
{
	enchant_spell(0, 0, 0, 1, 0, context->cmd);
	context->ident = true;
	return true;
}

/**
 * Brand the current melee weapon
 */
bool effect_handler_BRAND_WEAPON(effect_handler_context_t *context)
{
	struct object *obj = equipped_item_by_slot_name(player, "weapon");

	/* Brand the weapon */
	brand_object(obj, NULL);

	context->ident = true;
	return true;
}


/**
 * Brand some (non-ego/artifact) ammo
 */
bool effect_handler_BRAND_AMMO(effect_handler_context_t *context)
{
	struct object *obj;
	const char *q, *s;
	int itemmode = (USE_INVEN | USE_QUIVER | USE_FLOOR);
	bool used = false;

	context->ident = true;

	/* Get an item */
	q = "Brand which kind of ammunition? ";
	s = "You have nothing to brand.";
	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, q, s,
				tval_is_ammo, itemmode)) {
			return used;
		}
	} else if (!get_item(&obj, q, s, 0, tval_is_ammo, itemmode))
		return used;

	/* Brand the ammo */
	brand_object(obj, NULL);

	/* Done */
	return (true);
}

/**
 * Turn a staff into arrows
 */
bool effect_handler_CREATE_ARROWS(effect_handler_context_t *context)
{
	int lev;
	struct object *obj, *device, *arrows;
	const char *q, *s;
	int itemmode = (USE_INVEN | USE_FLOOR);
	bool good = false, great = false;
	bool none_left = false;

	/* Get an item */
	q = "Make arrows from which device? ";
	s = "You have no device to use.";
	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, q, s,
				tval_is_device, itemmode)) {
			return false;
		}
	} else if (!get_item(&obj, q, s, 0, tval_is_device,
				  itemmode)) {
		return false;
	}

	/* Extract the object "level" */
	lev = obj->kind->level;

	/* Roll for good */
	if (randint1(lev) > 25) {
		good = true;
		/* Roll for great */
		if (randint1(lev) > 50) {
			great = true;
		}
	}

	/* Destroy the device */
	if (object_is_carried(player, obj)) {
		device = gear_object_for_use(player, obj, 1, true, &none_left);
	} else {
		device = floor_object_for_use(player, obj, 1, true, &none_left);
	}

	if (device->known) {
		object_delete(player->cave, NULL, &device->known);
	}
	object_delete(cave, player->cave, &device);

	/* Make some arrows */
	arrows = make_object(cave, player->lev, good, great, false, NULL, TV_AMMO_9);
	drop_near(cave, &arrows, 0, player->grid, true, true);

	return true;
}

/* Change player shape */
static void shapechange(const char *shapename, bool verbose)

{
	/* Change shape */
	player->shape = lookup_player_shape(shapename);
	if (verbose) {
		msg("You assume the shape of a %s!", shapename);
		msg("Your gear merges into your body.");
	}

	/* Update */
	shape_learn_on_assume(player, shapename);
	player->upkeep->update |= (PU_BONUS);
	player->upkeep->redraw |= (PR_TITLE | PR_MISC);
	handle_stuff(player);
}

/**
 * Perform a player shapechange
 */
bool effect_handler_SHAPECHANGE(effect_handler_context_t *context)
{
	bool ident = false;
	struct player_shape *shape = player_shape_by_idx(context->subtype);
	assert(shape);
	shapechange(shape->name, true);

	/* Do effect */
	if (shape->effect) {
		(void) effect_do(shape->effect, source_player(), NULL, &ident, true,
						 0, 0, 0, NULL, context->alternate);
	}

	return true;
}

/**
 * Take control of a monster
 */
bool effect_handler_COMMAND(effect_handler_context_t *context)
{
	int amount = effect_calculate_value(context, false);
	struct monster *mon = target_get_monster();

	context->ident = true;

	/* Need to choose a monster, not just point */
	if (!mon) {
		msg("No monster selected!");
		return false;
	}

	/* Wake up, become aware */
	monster_wake(mon, false, 100);

	/* Explicit saving throw */
	if (randint1(player->lev) < randint1(mon->race->level)) {
		char m_name[80];
		monster_desc(m_name, sizeof(m_name), mon, MDESC_STANDARD);
		msg("%s resists your command!", m_name);
		return false;
	}

	/* Player is commanding */
	player_set_timed(player, TMD_COMMAND, MAX(amount, 0), false);

	/* Monster is commanded */
	mon_inc_timed(mon, MON_TMD_COMMAND, MAX(amount, 0), 0);

	return true;
}

/**
 * One Ring activation
 */
bool effect_handler_BIZARRE(effect_handler_context_t *context)
{
	context->ident = true;

	/* Pick a random effect */
	switch (randint1(10))
	{
		case 1:
		case 2:
		{
			/* Message */
			msg("You are surrounded by a malignant aura.");

			/* Decrease all stats (permanently) */
			for(int i=0;i<STAT_MAX;i++)
				player_stat_dec(player, i, true);

			/* Lose some experience (permanently) */
			player_exp_lose(player, player->exp / 4, true);

			return true;
		}

		case 3:
		{
			/* Message */
			msg("You are surrounded by a powerful aura.");

			/* Dispel monsters */
			effect_simple(EF_PROJECT_LOS, context->origin, "1000", PROJ_DISP_ALL, 0, 0, 0, 0, NULL);

			return true;
		}

		case 4:
		case 5:
		case 6:
		{
			/* Radiation Ball */
			int flg = PROJECT_THRU | PROJECT_STOP | PROJECT_GRID | PROJECT_ITEM | PROJECT_KILL;
			struct loc target = loc_sum(player->grid, ddgrid[context->dir]);

			/* Ask for a target if no direction given */
			if ((context->dir == DIR_TARGET) && target_okay()) {
				flg &= ~(PROJECT_STOP | PROJECT_THRU);

				target_get(&target);
			}

			/* Aim at the target, explode */
			return (project(source_player(), 3, target, 300, PROJ_RADIATION, flg, 0,
							0, context->obj));
		}

		case 7:
		case 8:
		case 9:
		case 10:
		{
			/* Radiation Bolt */
			int flg = PROJECT_STOP | PROJECT_KILL | PROJECT_THRU;
			struct loc target = loc_sum(player->grid, ddgrid[context->dir]);

			/* Use an actual target */
			if ((context->dir == DIR_TARGET) && target_okay())
				target_get(&target);

			/* Aim at the target, do NOT explode */
			return project(source_player(), 0, target, 250, PROJ_RADIATION, flg, 0,
						   0, context->obj);
		}
	}

	return false;
}

struct printkind {
	const struct object_kind *kind;
	int difficulty;
	int colour;
	int chunks;
	int chunk_idx;
	char name[80];
};

static int printkind_compar(const void *a, const void *b)
{
	return ((struct printkind *)a)->difficulty - ((struct printkind *)b)->difficulty;
}

/** Convert a difficulty (per 10K) to a colour index: red -> green */
static int difficulty_colour(int diff)
{
	if (diff < 1000)
		return COLOUR_GREEN;
	else if (diff < 2000)
		return COLOUR_L_GREEN;
	else if (diff < 3500)
		return COLOUR_YELLOW;
	else if (diff < 5500)
		return COLOUR_ORANGE;
	else if (diff < 7500)
		return COLOUR_RED;
	return COLOUR_PURPLE;
}

/** Return true if it is possible (in any circumstances) to print an item.
 * (This does not have to check there is enough material)
 * Unprintable items include blocks, chests, special artifacts, etc.
 * There is also an UNPRINTABLE flag for more granular control.
 */ 
static bool kind_is_printable(const struct object_kind *k)
{
	if (k->alloc_prob == 0)
		return false;
	if (kf_has(k->kind_flags, KF_QUEST_ART))
		return false;
	if (kf_has(k->kind_flags, KF_INSTA_ART))
		return false;
	if (kf_has(k->kind_flags, KF_SPECIAL_GEN))
		return false;
	if (kf_has(k->kind_flags, KF_UNPRINTABLE))
		return false;
	if (!(material + k->material)->printable)
		return false;
	return true;
}

static int recyclable_blocks(const struct object *obj)
{
	const struct object_kind *k = obj->kind;
	if (!kind_is_printable(k))
		return 0;
	int lev = levels_in_class(get_class_by_name("Engineer")->cidx);
	int cap = 5 + (lev / 2);
	int rate = lev * 19;
	return MIN(cap, (k->weight * obj->number) / rate);
}

/** The effect of a printer
 * Requires INT and device skill for success
 * Higher level printers help, higher level items hurt.
 * If you want extras, that hurts.
 * 
 * If it fails, it may use all or some of the chunks.
 * 
 * It takes two paramters - the first is the max number of chunks needed.
 * The second is the class:
 * 	0 = plastic, and possibly wax, wood. 
 * 	1 = " + light alloy: aluminium
 *  2 = ", hard metals: steel, titanium
 *  3 = ", unobtainium, exotics
 * 
 * (May want to allow 1 step up, but at risk to the printer)
 * 
 * Use item knowledge screen?
 * First step is to select an item - limited by it having a material that the printer can use & having chunks for it
 */
bool effect_handler_PRINT(effect_handler_context_t *context)
{
	int skill = player->state.skills[SKILL_DEVICE];
	struct object *printer = (struct object *)context->obj;
	int maxchunks = context->subtype;
	int maxmetal = context->radius;
	struct object **chunk = NULL;
	struct printkind *item = NULL;
	int nchunks = 0;
	/* This is a divisor modifying the weight needed to make an item: perfect efficiency would be 1000 */
	int efficiency = context->obj->pval;
	const char *nogo = NULL;
	int longestname = 0;
	const char **chunkname = NULL;

	/* First clear the screen and print the help */
	screen_save();
	Term_clear();
	struct textblock *tb = textblock_new();
	textblock_append_c(tb, COLOUR_WHITE, "Select an item to attempt to create one from blocks.");
	textblock_append_c(tb, COLOUR_SLATE, " Higher level materials (especially when it's more than the printer is really meant for), higher level items, items requiring more blocks, rare or expensive items are all likely to push the difficulty up. Large format or high level printers will help, as will your level and device skill. The items displayed are sorted from easiest to most difficult and coloured based on your chance of success, which is also displayed numerically for the currently selected item. Use the ");
	textblock_append_c(tb, COLOUR_WHITE, "cursor keys");
	textblock_append_c(tb, COLOUR_SLATE, " to select an item, ");
	textblock_append_c(tb, COLOUR_WHITE, "Return ");
	textblock_append_c(tb, COLOUR_SLATE, "to print it, ");
	textblock_append_c(tb, COLOUR_WHITE, "Page Up/Down ");
	textblock_append_c(tb, COLOUR_SLATE, "to move between pages or ");
	textblock_append_c(tb, COLOUR_WHITE, "Space ");
	textblock_append_c(tb, COLOUR_SLATE, "to exit.");
	size_t *line_starts = NULL;
	size_t *line_lengths = NULL;
	int w, h;
	Term_get_size(&w, &h);
	int top = textblock_calculate_lines(tb, &line_starts, &line_lengths, w) + 1;
	textui_textblock_place(tb, SCREEN_REGION, NULL);
	textblock_free(tb);

	/* Redraw */
	Term_redraw();

	/* Chunks' pval is the printer class. Count up the available chunks */
	struct object *obj;
	for(obj=player->gear; obj; obj=obj->next)
		if (obj->tval == TV_BLOCK)
			nchunks++;
	chunk = mem_alloc(sizeof(*chunk) * nchunks);
	chunkname = mem_alloc(sizeof(*chunkname) * nchunks);
	int i = 0;
	for(obj=player->gear; obj; obj=obj->next)
		if (obj->tval == TV_BLOCK)
			chunk[i++] = obj;

	/* Come up with a short name for each chunk */
	for(int i=0;i<nchunks;i++) {
		const char *name = "?????";
		const char *cname = chunk[i]->kind->name;
		if (my_stristr(cname, "alum"))
			name = "Alumi";
		else if (my_stristr(cname, "plas"))
			name = "Plast";
		else if (my_stristr(cname, "steel"))
			name = "Steel";
		else if (my_stristr(cname, "titan"))
			name = "Titan";
		else if (my_stristr(cname, "gold"))
			name = "Gold ";
		else if (my_stristr(cname, "unob"))
			name = "Unobt";
		chunkname[i] = name;
	}

	/* There is now an array of all chunks carried in chunk, length nchunks.
	 * Get out early if you have none (but distinguish from 'none usable')
	 */
	if (nchunks == 0)
		nogo = "You can't print anything as you have no raw materials (blocks).";
	else {
		/* Skip high level chunks. */
		for(i=0; i<nchunks; i++) {
			if ((chunk[i]->pval - maxmetal) > 1) {
				nchunks--;
				chunk[i] = chunk[nchunks];
				i--;
				break;
			}
		}
		if (nchunks == 0)
			nogo = "You can't print anything as you only have blocks that your printer can't use.";
	}

	/* Easymod or Maximod printers are easier to use */
	bool easymode = ((obj_has_ego(printer, "Easy")) || (obj_has_ego(printer, "Maxi")));

	/* Scan item kinds.
	 * Get a list of printable items.
	 **/
	item = mem_zalloc(z_info->k_max * sizeof(*item));
	int nprintable = 0;
	if (nchunks) {
		for(i=0;i<z_info->k_max; i++) {
			const struct object_kind *k = k_info + i;
			bool ok = false;
			int material = 0;

			/* Check if it's a printable item */
			if (kind_is_printable(k)) {
				/* Check if it's a usable material */
				for(int j=0;j<nchunks;j++) {
					if (chunk[j]->kind->material == k->material) {
						ok = true;
						material = j;
						break;
					}
				}
			}

			/* If so, check the weight is not more than you have, or more than
			 * the printer can do.
			 **/
			int chunks = 0;
			if (ok) {
				if (k->weight < 2000000) {
					chunks = (k->weight + 1000) / efficiency;
					if (chunks <= 0)
						chunks = 1;
					if (chunks > chunk[material]->number)
						ok = false;
					if (chunks > maxchunks)
						ok = false;
				}
			}

			/* The printer and materials can do it. You may not be able to, though.
			 * Compute a difficulty (chance of failure), and if it's near 100%
			 * don't even list it.
			 */
			int difficulty = -1;
			if (ok) {
				/* Higher level materials = more difficult */
				difficulty = chunk[material]->pval * 20;

				/* Higher level item = more difficult */
				difficulty += k->level;
	
				/* Higher cost item = more difficult */
				difficulty += sqrt(k->cost);

				/* Higher rarity item = more difficult */
				difficulty += 100 / (k->alloc_prob + 1);

				/* Larger prints are more difficult */
				difficulty += chunks;

				/* Printer is pushed to beyond its normal use */
				if (chunk[material]->pval > maxmetal)
					difficulty += 10 + (difficulty / 2);

				/* Higher level printer = less difficult */
				difficulty -= maxmetal * 5;
				if (maxchunks <= 5)
					difficulty += 2;

				/* Higher level characters are better */
				difficulty -= player->lev * 2;

				/* Device skill (scaled 0 to ~140) helps */
				difficulty -= (skill * 2) / 3;

				/* Easymod/Maximod printers help */
				if (easymode)
					difficulty -= 20;

				/* 
				 * There should be no perfect chance (but at -100 you will be 5% or so,
				 * maybe 10% at -50, 20% at -25, 50% at 0, 80% at 25, 90% at 50 and cut off
				 * about 50-70.
				 * 
				 * The table below maps difficulty to chance-per-10K.
				 */
				 static const u16b difftab[] = {
					 /* -100 */
					 500,	505,	510,	515,	520,	525,	530,	535,	540,	545,
					 /* -90 */
					 550,	555,	561,	567,	573,	580,	587,	594,	602,	610,
					 /* -80 */
					 618,	627,	637,	647,	658,	668,	680,	692,	705,	718,
					 /* -70 */
					 721,	735,	750,	785,	800,	815,	830,	845,	860,	875,	
					 /* -60 */
					 890,	905,	921,	937,	956,	974,	983,	992,	1011,	1030,
					 /* -50 */
					 1050,	1070,	1090,	1110,	1130,	1150,	1175,	1200,	1225,	1250,
					 /* -40 */
					 1275,	1300,	1330,	1360,	1390,	1420,	1455,	1490,	1550,	1610,
					 /* -30 */
					 1680,	1720,	1790,	1860,	1930,	2010,	2090,	2190,	2280,	2380,
					 /* -20 */
					 2480,	2580,	2690,	2800,	2830,	2970,	3000,	3130,	3260,	3400,
					 /* -10 */
					 3550,	3700,	3850,	4000,	4150,	4300,	4450,	4600,	4800,	5000,
					 /* 0 */
					 5200,	5400,	5600,	5800,	6000,	6200,	6400,	6550,	6700,	6900,
					 /* 10 */
					 7050,	7200,	7325,	7450,	7575,	7800,	7900,	8000,	8100,	8200,
					 /* 20 */
					 8280,	8360,	8430,	8530,	8590,	8640,	8690,	8730,	8770,	8800,
					 /* 30 */
					 8825,	8850,	8875,	8900,	8920,	8940,	8960,	8980,	9000,	9020,
					 /* 40 */
					 9040,	9060,	9080,	9100,	9120,	9140,	9160,	9180,	9200,	9220,
					 /* 50 */
					 9240,	9260,	9280,	9300,	9320,	9340,	9360,	9380,	9400,	9420,
					 /* 60 */
					 9440,	9460,	9480,	9500
				 };

				/* Scale to the table */
				difficulty /= 2;
				difficulty += 100;

				/* Easy end: difficulty easier than -100 is all the same */
				if (difficulty < 0)
					difficulty = 0;

				/* Difficult end: off the end = no chance, stop */
				if (difficulty > (int)(sizeof(difftab)/sizeof(*difftab))) {
					ok = false;
					difficulty = -1;
				} else {
					difficulty = difftab[difficulty];
				}
			}
			if (ok) {
				/* List an item: store kind and difficulty, and produce a name.
				 * Track the longest name.
				 * Add a colour, and the chunks required
				 **/
				item[nprintable].difficulty = difficulty;
				item[nprintable].colour = difficulty_colour(difficulty);
				item[nprintable].chunks = chunks;
				item[nprintable].chunk_idx = material;
				item[nprintable].kind = k;

				obj_desc_name_format(item[nprintable].name, sizeof item[nprintable].name, 0, k->name, 0, false);
				if (k->tval == TV_CARD)
					strncat(item[nprintable].name, " card", sizeof item[nprintable].name);

				int length = strlen(item[nprintable].name);
				if (length > longestname)
					longestname = length;

				nprintable++;
			}
		}

		if (nprintable == 0) {
			nogo = "You can't print anything as you don't have the skill yet.";
		} else {

			/* There are now one or more printable items, total 'nprintable',
			 * indexed as kinds in item[] and difficulties in itemdiff[].
			 * Sort by difficulty
			 */
			qsort(item, nprintable, sizeof(*item), printkind_compar);
		}
	}

	/* Display - a how-to across the top, followed by either a no-go message or
	 * a grid of items to select. There may be a lot of items, so it must be pageable.
	 * While item names may be long, to make best use of a large terminal it should
	 * still allow multicolumn layouts, adapting to the size of the names and the
	 * display. To help this, there should be a minimum of tourist information - the
	 * items are listed by difficulty and coloured by difficulty, so precise difficulty
	 * (and the # of chunks) can be moved out, displayed only for the item at the
	 * cursor. The selection should also be done without adding columns - e.g. display
	 * the selected item in white (or pastels, or inverse?) using the colour for the
	 * current-item info).
	 */
	bool leaving = false;
	int selected = 0;
	int columns = w / (longestname + 1);
	int rows = h - (top + 2);
	int toprow = 0;
	int visible = MIN(nprintable, (rows * columns));
	int screens = ((nprintable - 1) / (rows * columns)) + 1;
	do 
	{
		/* Print a no-go line or the selected item (Move to two lines?) */
		if (nogo)
			c_prt(COLOUR_ORANGE, nogo, top, 0);
		else {
			char buf[80];
			strnfmt(buf, sizeof(buf), "%d%% fail: %d/%d %s (%d%% efficient): %s", (item[selected].difficulty + 50) / 100, item[selected].chunks, chunk[item[selected].chunk_idx]->number, chunkname[item[selected].chunk_idx], (efficiency + 5) / 10, item[selected].name);
			c_prt(item[selected].colour, buf, top, 0);
		}

		/* Print a grid of items - left to right, top to bottom */
		if (!nogo) {
			for(int y=0;y<rows;y++) {
				for(int x=0;x<columns;x++) {
					int idx = x + (y * columns) + (toprow * rows * columns);
					c_prt(idx == selected ? COLOUR_L_WHITE : item[idx].colour, item[idx].name, top + 2 + y, x * (longestname + 1));
				}
			}
		}
		Term_redraw();

		/* Key loop : read a key */
		struct keypress ch = inkey();
		switch(ch.code) {
			/* Navigate around the grid */
			case '2':
			case ARROW_DOWN:
			selected += columns;
			if (selected >= visible + (toprow * rows * columns))
				selected = (selected % columns) + (toprow * rows * columns);
			break;
			case '4':
			case ARROW_LEFT:
			selected--;
			if (selected < (toprow * rows * columns))
				selected = (toprow * rows * columns) + visible - 1;
			break;
			case '6':
			case ARROW_RIGHT:
			selected++;
			if (selected >= visible + (toprow * rows * columns))
				selected = (toprow * rows * columns);
			break;
			case '8':
			case ARROW_UP:
			selected -= columns;
			if (selected < (toprow * rows * columns))
				selected += visible;
			break;
			case KC_PGDOWN:
			if (toprow < screens - 1) {
				toprow += 1;
				selected += columns * rows;
			}
			if (toprow == screens - 1)
				visible = nprintable - (toprow * rows * columns);
			else
				visible = MIN(nprintable, (rows * columns));
			if (selected >= visible + (toprow * rows * columns))
				selected = visible + (toprow * rows * columns) - 1;
			break;
			case KC_PGUP:
			if (toprow) {
				toprow -= 1;
				selected -= columns * rows;
			}
			visible = MIN(nprintable, (rows * columns));
			break;

			/* Select */
			case KC_ENTER:
			/* Prompt to confirm. If not confirmed continue, otherwise exit this loop with a selected item.
			 */
			c_prt(COLOUR_ORANGE, format("Really build %s? [yn]", item[selected].name), 0, 0);
			ch = inkey();
			if (strchr("Yy", ch.code)) {
				// Accept
				leaving = true;
			}
			break;

			/* Leave */
			case ESCAPE:
			case 'Q':
			case ' ':
			selected = -1;	/* do not create an item */
			leaving = true;
			break;
		}
	} while (!leaving);

	/* Do we have an item? If so, try to build one */
	if (selected >= 0) {
		struct printkind *pk = &item[selected];
		int rmblocks = pk->chunks;
		bool quiet = false;

		/* Difficulty check */
		if (randint0(10000) > pk->difficulty) {
			msg("The printer whirs, blurs and something bounces out...");
			struct object_kind *kind = (struct object_kind *)pk->kind;

			/* Create an object - select level, good/great? */
			struct object *result = object_new();

			/* Level is taken from skill and player level (in roughly equal proportion, scaled 0 to ~100).
			 * Items are 'good' or 'great' if this level exceeds the item's level.
			 * Artifacts can't be created, and the "extra_roll" flag only affects the chance of creating
			 * an artifact so this is always false.
			 */
			int lev = ((skill * 3) + player->lev) / 2;
			int itemlev = kind->level;
			bool good = false;
			bool great = false;
			if (lev < itemlev) {
				/* 'Difficult' - sometimes good */
				if (randint0(itemlev) < lev)
					good = true;
			} else {
				/* 'Easy' - always good, sometimes great */
				good = true;
				if (randint0(lev) > itemlev)
					great = true;
			}
			/* ... but low skill / level players will not always see that bonus */
			if (randint0(20) < lev)
				good = great = false;
			if (randint0(40) < lev)
				great = false;

			/* Create */
			object_prep(result, kind, lev, RANDOMISE);
			apply_magic(result, lev, false, good, great, false);

			result->origin = ORIGIN_PRINTER;
			result->origin_depth = player->depth;
		
			drop_near(cave, &result, 0, player->grid, true, false);
			quiet = true;
		} else {
			/* Failed - no item.
			 * Test for a critical failure.
			 */
			int diff = pk->difficulty;
			if (chunk[pk->chunk_idx]->pval <= maxmetal) {
				/* Usually be kind */
				diff -= 400;
				diff /= 10;
			}
			if (randint0(10000) < diff) {
				/* Printer tries to destroy itself.
				 * If it has the "duramod" then it will survive at least one such event (setting the FRAGILE flag) and possibly
				 * more, depending on a high-difficulty roll.
				 */
				bool save = false;
				if (!of_has(printer->flags, OF_FRAGILE)) {
					if ((obj_has_ego(printer, "Dura")) || (obj_has_ego(printer, "Maxi")))
						save = true;
				}
				if (save) {
					msg("The printer smokes, chokes and nearly tears itself apart!");
					if (randint0(10000) < 3000 + diff) {
						msg("It barely remains intact, but looks much more fragile now.");
						of_on(printer->flags, OF_FRAGILE);
						player_learn_flag(player, OF_FRAGILE);
					} else {
						msg("Luckily it remains undamaged, although the workpiece is ruined.");
					}
					quiet = true;
				} else {

					/* Oops */
					msg("The printer smokes, chokes and tears itself apart!");
					rmblocks = 0;

					/* Destroy printer! */
					struct object *destroyed;
					bool none_left = false;
					destroyed = gear_object_for_use(player, printer, 1, false, &none_left);
					if (destroyed->known)
						object_delete(player->cave, NULL, &destroyed->known);
					object_delete(cave, player->cave, &destroyed);
				}
			} else {
				/* Sometimes use less, but no credit for using the right material */
				if (randint0(10000) < pk->difficulty) {
					rmblocks = randint1(rmblocks);
				}
			}
		}

		/* Destroy blocks */
		if (rmblocks) {
			char o_name[80];
			struct object *destroyed;
			bool none_left = false;

			/* Display the number destroyed (but not if an item was created) */
			if (!quiet) {
				int number = chunk[pk->chunk_idx]->number;
				chunk[pk->chunk_idx]->number = rmblocks;
				object_desc(o_name, sizeof(o_name), chunk[pk->chunk_idx], ODESC_BASE | ODESC_PREFIX, player);
				chunk[pk->chunk_idx]->number = number;
				msg("The printer shakes, quakes and turns %s into useless swarf.", o_name);
			}

			if (rmblocks == chunk[pk->chunk_idx]->number) {
				/* Destroy the whole stack */
				destroyed = gear_object_for_use(player, chunk[pk->chunk_idx], rmblocks, false, &none_left);
				if (destroyed->known)
					object_delete(player->cave, NULL, &destroyed->known);
				object_delete(cave, player->cave, &destroyed);
			} else {
				/* Reduce the number */
				destroyed = gear_object_for_use(player, chunk[pk->chunk_idx], rmblocks, false, &none_left);
			}
		}
	}

	/* Clean up */
	mem_free(item);
	mem_free(chunk);
	mem_free(chunkname);

	/* Weight, etc. */
	player->upkeep->update |= (PU_BONUS | PU_PANEL | PU_TORCH | PU_HP);
	screen_load();
	return true;
}


/**
 * Time Loop
 * Hold off danger level increases, needs a regen
 */
bool effect_handler_TIME_LOOP(effect_handler_context_t *context)
{
	s32b allowed;
	s32b used;
	bool timelord = get_regens(&allowed, &used);

	/* Not possible */
	if ((!timelord) || (used >= allowed)) {
		msg("You don't have the ability to regenerate, so can't commit one to a time loop.");
		return(true);
	}

	/* Make sure you know what you are geting into */
	if (get_check("Sure you want to create a time loop? (losing a regeneration!) ")) {
		/* Use a regen */
		timelord_change_regenerations(1);
		/* Return a fortnight */
		player->danger_reduction += 56; // 14 days
	}
	/* Done */
	return (true);
}

/**
 * Mutate
 */
bool effect_handler_MUTATE(effect_handler_context_t *context)
{
	if (mutate()) {
		/* Something noticeable happened - so ID it */
		context->ident = true;
	} else {
		/* Nothing happened, print a message */
		msg("Nothing obvious happens.");
	}
	/* Done */
	return (true);
}

/**
 * How much danger would you be in if nearby monsters were aggravated?
 */
int stealth_danger(void)
{
	int monsters = 0;
	int losmonsters = 0;
	int level = 0;

	/* Check everyone nearby */
	for (int i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);
		if (mon->race) {
			int radius = z_info->max_sight * 2;
			int dist = distance(player->grid, mon->grid);

			/* Skip monsters too far away */
			if ((dist < radius) && mon->m_timed[MON_TMD_SLEEP]) {
				// Count, get the highest level
				monsters++;
				int mlevel = mon->race->level;
				// Much less threatening if not in LOS
				if (!los(cave, player->grid, mon->grid)) {
					mlevel -= dist;
					mlevel /= 2;
				} else {
					mlevel -= dist / 2;
					losmonsters++;
				}

				if (mlevel > level)
					level = mlevel;
			}
		}
	}

	/* Danger factor = monster 'total level' (highest level, + a bit for more monsters)
	 * 				vs player 'safe level' derived from HP (not actual level).
	 */

	int monlevel = MIN(200, MAX(1, level + MIN(losmonsters, level / 2) + MIN(monsters / 2, level / 4)));
	int ulevel = MAX(1, player->chp);

	int danger = (monlevel * monlevel * 10000) / (ulevel * ulevel);
	return MIN(danger, MIN(1000, player->depth * 20));
}

/**
 * Hoooonk
 */
bool effect_handler_HORNS(effect_handler_context_t *context)
{
	int danger = stealth_danger();
	if (randint0(danger + 50) < danger) {
		/* Honk message */
		static const char *honk[] =		{ "honk", "blare", "blast", "hoot", "call", "sing", "trumpet", "bray" };
		static const char *music[] =	{ "musically", "tunefully", "mournfully", "a fanfare",
										"a loud trill", "two notes", "a long note", "three notes",
										"a challenge", "loudly", "unexpectedly", " a flourish", "shrilly", "reveille", "piercingly" };
		msg("Your horns %s out %s!", honk[randint0(sizeof(honk)/sizeof(*honk))], music[randint0(sizeof(music)/sizeof(*music))]);

		/* Aggro */
		effect_handler_WAKE(context);
	}
	/* Done */
	return (true);
}

bool effect_handler_MESSAGE(effect_handler_context_t *context)
{
	msg("%s",context->msg);
	return (true);
}

static void personality_display(struct menu *m, int oid, bool cursor,
		int row, int col, int width)
{
	struct player_race *p = personalities->next->next;
	for(int i=0; i<oid; i++)
		p = p->next;
	c_prt(curs_attrs[CURS_KNOWN][0 != cursor], p->name, row, col);
}

static bool personality_action(struct menu *m, const ui_event *e, int oid, bool *exit)
{
	if (e->type != EVT_SELECT)
		return true;

	struct player_race *p = personalities->next->next;
	for(int i=0; i<oid; i++)
		p = p->next;
	player->personality = p;

	return false;
}

static const region personality_area = { 0, 0, 0, 0 };

static menu_iter personality_menu =
{
	NULL,
	NULL,
	personality_display,
	personality_action,
	NULL
};

/**
 * Change personality
 */
bool effect_handler_PERSONALITY(effect_handler_context_t *context)
{
	int per[z_info->r_max];
	int nper = 0;
	struct menu *menu = menu_new(MN_SKIN_COLUMNS, &personality_menu);

	/* List personalities */
	struct player_race *p = personalities->next->next;
	for(; p; p = p->next) {
		per[nper] = nper;
		nper++;
	}

	/* Save the screen */
	screen_save();
	clear_from(0);

	/* Lay out the menu */
	menu_setpriv(menu, nper, kb_info);
	menu_set_filter(menu, per, nper);
	menu_layout(menu, &personality_area);
	menu_select(menu, 0, false);

	/* Clean up */
	screen_load();
	mem_free(menu);
	
	/* Update and redraw map */
	player->upkeep->update |= (PU_BONUS | PU_HP | PU_PANEL);
	player->upkeep->redraw |= (PR_MAP | PR_ITEMLIST | PR_HP | PR_STATS);
	handle_stuff(player);

	/* Done */
	return (true);
}

bool effect_handler_BANANA(effect_handler_context_t *context)
{
	/* May have a spider in it */
	if (one_in_(20)) {
		int mon = summon_named_near(player->grid, "spider");
		if (mon) {
			msg("It begins to move of its own accord, and bursts open! There's a SPIDER in it!");
			return (true);
		}
	}

	/* If not (or if the monster summoning couldn't find one), it's just food */
	msg("That tastes good.");
	player_inc_timed(player, TMD_FOOD, 10 * z_info->food_value, false, false);

	return (true);
}

/* It shouldn't be possible to land or takeoff in the wrong state */
bool effect_handler_LAND(effect_handler_context_t *context)
{
	/* Thes messages will needs to be modified if jetpacks are used */
	if (levels_in_class(get_class_by_name("Pilot")->cidx))
		msg("You settle to the ground and climb out.");
	else
		msg("You are back on your feet again.");
	player->flying = false;
	return (true);
}

bool effect_handler_TAKEOFF(effect_handler_context_t *context)
{
	if (levels_in_class(get_class_by_name("Pilot")->cidx))
		msg("You get in and take off.");
	else
		msg("You jump into the air.");
	player->flying = true;
	return (true);
}

/**
 * Time Lord regeneration
 **/
bool effect_handler_FORCE_REGEN(effect_handler_context_t *context)
{
	timelord_force_regen();
	return (true);
}

bool effect_handler_IF_SUCCESSFUL(effect_handler_context_t *context)
{
	context->next = context->completed;
	return (true);
}

/**
 * Rumor. 
 * This is true X% of the time (taken from the value) 
 */
bool effect_handler_RUMOR(effect_handler_context_t *context)
{
	int die = effect_calculate_value(context, false);
	msg(random_rumor(die));
	return (true);
}

/**
 * Climbing
 */
bool effect_handler_CLIMBING(effect_handler_context_t *context)
{
	/* Is there a wall next to you? */
	if (!square_num_walls_adjacent(cave, player->grid)) {
		msg("You need an adjacent rock face to climb up.");
	} else {
		bool fail = (!stat_check(STAT_DEX, 12));
		if (fail) {
			/* You tried and fell off.
			 * FF will always save you, otherwise make a DEX check.
			 **/
			if (player_of_has(player, OF_FEATHER)) {
				msg("You lose your grip, and float gently to the ground.");
				player_learn_icon(player, OF_FEATHER, true);
			} else if (!stat_check(STAT_DEX, 18)) {
				msg("You lose your grip and fall. Ouch!");
				take_hit(player, randint1(6)+randint1(20), "falling");
			} else {
				msg("You lose your grip, but are unhurt.");
			}
		} else {
			/* The climb attempt was successful but the level change may not be.
			 * You should always get some message from this, so long as it's only used by the player.
			 * Although the "Nothing happens" message might be best replaced with something more descriptive.
			 **/
			return effect_change_level(context, "You climb through a crack above you.", NULL, true, false, false);
		}
	}
	return (true);
}

/**
 * Next effect (do nothing here)
 */
bool effect_handler_NEXT(effect_handler_context_t *context)
{
	return (true);
}

/* Artifact creation */
bool effect_handler_ARTIFACT_CREATION(effect_handler_context_t *context)
{
	struct object *obj;

	/* Get an item. Avoid the floor because BOOM */
	const char *q = "Build an artifact from which item? ";
	const char *s = "You have no suitable base item.";
	if (context->cmd) {
		if (cmd_get_item(context->cmd, "tgtitem", &obj, q, s, item_tester_artifact_creation,
				(USE_EQUIP | USE_INVEN | USE_QUIVER))) {
			return false;
		}
	} else if (!get_item(&obj, q, s, 0, item_tester_artifact_creation, (USE_EQUIP | USE_INVEN | USE_QUIVER)))
		return false;

	/* Randomize that artifact */
	struct artifact *art = (struct artifact *)lookup_artifact_name(player->artifact);
	assert(art);
	if (!new_random_artifact(obj, art, player->lev * 10)) {
		msg("It fails to take hold on such an item.");
		return false;
	}

	/* The previous item is no longer an artifact */
	struct location loc;
	struct object *previous = locate_object(object_is_artifact, (void *)art, &loc);
	if (previous) {
		char p_name[80];
		const char *fall = NULL;
		/* If you notice it, give a message */
		if (loc.type == LOCATION_PLAYER) {
			if (object_is_equipped(player->body, obj))
				fall = "you are using";
			else
				fall = "you are carrying";
		} else if (loc.type == LOCATION_GROUND) {
			if (loc_eq(loc.loc, player->grid))
				fall = "at your feet";
			else if (square_isview(cave, loc.loc))
				fall = "on the floor";
		}
		if (fall) {
			object_desc(p_name, sizeof(p_name), previous, ODESC_BASE, player);
			msg("The %s %s shudders and writhes awfully.", p_name, fall);
		}
		mundane_object(previous, true);
	}

	/* Make it into an artifact */
	mundane_object(obj, true);
	obj->artifact = art;
	copy_artifact_data(obj, art);
	obj->el_info[ELEM_FIRE].flags |= EL_INFO_IGNORE;
	obj->el_info[ELEM_COLD].flags |= EL_INFO_IGNORE;
	obj->el_info[ELEM_ELEC].flags |= EL_INFO_IGNORE;
	obj->el_info[ELEM_ACID].flags |= EL_INFO_IGNORE;

	/* Describe the KABOOM */
	char o_name[80];
	object_desc(o_name, sizeof(o_name), obj, ODESC_BASE, player);
	msg("%s %s radiates a searing blast of white light!", (object_is_carried(player, obj) ? "Your" : "The"), o_name);

	/* Identify it */
	object_know_all(obj);
	obj->known->artifact = obj->artifact;

	/* Prompt for a name */
	char name[80];
	do {
		*name = 0;
		get_string_hook("What do you want to call it? ", name, sizeof(name));
	} while (strlen(name) == 0);
	char quoted[83];
	strnfmt(quoted, sizeof(quoted), "'%s'", name);

	if (art->name)
		mem_free(art->name);
	((struct artifact *)(art))->name = string_make(isupper(name[0]) ? quoted : name);
	player->artifact = string_make(art->name);

	/* Something happened */
	return (true);
}

/**
 * Prismatic Lightsaber
 */
bool effect_handler_PRISMATIC(effect_handler_context_t *context)
{
	assert(context->obj);
	struct object *obj = (struct object *)context->obj;
	if (!(obj->brands))
		return (true);

	#define n_brands 5

	static const char *flip[] = {
		"flip",
		"switch",
		"convert",
		"dial",
		"transform",
	};

	/* The brands to switch between */
	static const char *message[n_brands*2] = {
		"a brilliant dual lightsaber.",
		"a dimsaber's twin chaotic maelstroms.",
		"an ominous red dual darksaber.",
		"a pair of shimmering phantom blades.",
		"invisible dual X-ray beams.",
		"a brilliant lightsaber.",
		"a dimsaber's chaotic maelstrom.",
		"an ominous red darksaber.",
		"an uncannily shimmering phantom blade.",
		"an invisible X-ray beam.",
	};

	static const char *brand_name[n_brands*2] =	{	"LIGHT_2", "CHAOS_2", "DARK_2", "NEXUS_2", "RADIATION_2",
										"LIGHT_3", "CHAOS_3", "DARK_3", "NEXUS_3", "RADIATION_3" };
	int resist_idx[n_brands] = { ELEM_LIGHT, ELEM_CHAOS, ELEM_DARK, ELEM_NEXUS, ELEM_RADIATION };
	int brand_idx[n_brands*2];
	int brand = 0;

	/* Find the brand indexes */
	for(int i=0;i<n_brands*2;i++) {
		brand_idx[i] = get_brand_by_name(brand_name[i]);
		if (brand_idx[i] < 0) {
			msg("Whoops. What's %s?", brand_name[i]);
			return (true);
		}
		if (obj->brands[brand_idx[i]])
			brand = i;
	}

	/* Turn off the other brands and resistances */
	for(int i=0;i<n_brands*2;i++) {
		obj->brands[brand_idx[i]] = false;
		if (obj->known->brands)
			obj->known->brands[brand_idx[i]] = false;
		obj->el_info[resist_idx[i % n_brands]].res_level = 0;
	}
	obj->modifiers[OBJ_MOD_LIGHT] = 0;

	/* The new one */
	brand++;
	if ((brand % n_brands) == 0)
		brand -= n_brands;

	/* Turn on this one */
	obj->brands[brand_idx[brand]] = true;
	obj->el_info[resist_idx[brand % n_brands]].res_level = 1;

	/* A lightsaber glows, the others don't */
	if ((brand % n_brands) == 0)
		obj->modifiers[OBJ_MOD_LIGHT] = 1;

	/* X-sabers are hard to use.
	 * But a reversible penalty (of say -20) would allow you to enhance it to +15 and then
	 * switch it back to a +35 lightsaber! So just assume that the X-mode has a visible guide
	 * beam.
	 **/

	/* Print a message */
	msg("You %s your weapon into %s", flip[randint0(sizeof(flip)/sizeof(*flip))], message[brand]);

	/* Learn, as if wielded */
	object_learn_on_wield(player, obj);
	player_know_object(player, obj);
	update_player_object_knowledge(player);

	/* Update */
	player->upkeep->update |= (PU_TORCH);
	update_stuff(player);

	#undef n_brands

	return (true);
}

/**
 * Dummy effect, to tell the effect code to pick one of the next
 * context->value.base effects at the player's selection or, if the effect
 * wasn't initiated by the player, at random.
 */
bool effect_handler_SELECT(effect_handler_context_t *context)
{
	return true;
}

/**
 * Dummy effect, to tell the effect code to set a value for a string of
 * following effects to use, rather than setting their own value.
 * The value will not use the device boost, which should not be a problem
 * as it is unlikely to be used for damage (the main use case is to
 * synchronise the end of timed effects).
 */
bool effect_handler_SET_VALUE(effect_handler_context_t *context)
{
	set_value = effect_calculate_value(context, false);
	return true;
}

/**
 * Dummy effect, to tell the effect code to clear a value set by the
 * SET_VALUE effect.
 */
bool effect_handler_CLEAR_VALUE(effect_handler_context_t *context)
{
	set_value = 0;
	return true;
}
