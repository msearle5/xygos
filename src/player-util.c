/**
 * \file player-util.c
 * \brief Player utility functions
 *
 * Copyright (c) 2011 The Angband Developers. See COPYING.
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
#include "cmd-core.h"
#include "cmds.h"
#include "effect-handler.h"
#include "effects.h"
#include "game-input.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "obj-chest.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-knowledge.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-quest.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "score.h"
#include "store.h"
#include "target.h"
#include "trap.h"
#include "ui-input.h"
#include "world.h"

/* List of death messages */
struct death_msg *death;

/** Returns the number of levels you have in a given class */
int levels_in_class(int c)
{
	int l = 0;
	for(int i=1; i<=player->lev; i++) {
		if (player->lev_class[i] == c)
			l++;
	}
	return l;
}

static int compar_class(const void *av, const void *bv)
{
	const struct player_class *a = *(const struct player_class **)av;
	const struct player_class *b = *(const struct player_class **)bv;

	int lev_a = levels_in_class(a->cidx);
	int lev_b = levels_in_class(b->cidx);

	if (lev_a != lev_b) return lev_b - lev_a;

	for(int i=1; i<(int)sizeof(player->lev_class); i++) {
		if (player->lev_class[i] == (a->cidx))
			return 1;
		if (player->lev_class[i] == (b->cidx))
			return -1;
	}
	return 0;
}

/** Returns the class with the given cidx, of NULL if there is none.
 */
struct player_class *get_class_by_idx(int cidx)
{
	for (struct player_class *c = classes; c; c = c->next) {
		if ((int)(c->cidx) == cidx)
			return c;
	}
	return NULL;
}

/** Returns a (static) list of classes, from most levelled to least.
 * The sort order is by total number of levels first, and the first level gained in that class second
 * (earlier = first).
 */
struct player_class **ordered_classes(void)
{
	static struct player_class **order = NULL;
	static int n_classes = 0;
	if (!order) {
		for (struct player_class *c = classes; c; c = c->next)
			n_classes++;
		order = mem_zalloc(sizeof(*order) * (n_classes + 1));	// +1 so there is always a terminator
	}

	/* Get all classes */
	int n = 0;
	for (struct player_class *c = classes; c; c = c->next) {
		order[n] = c;
		n++;
	}

	/* Sort */
	qsort(order, n, sizeof(*order), compar_class);

	/* Wipe unused ones */
	for (int i=0; i<n; i++) {
		if (levels_in_class(order[i]->cidx) == 0) {
			order[i] = NULL;
		}
	}

	return order;
}

/** Set the primary class, based on the level-class array */
void set_primary_class(void)
{
	struct player_class **order = ordered_classes();
	player->class = order[0];
}

/** Return an effective depth for difficulty of monster generation, etc.
 * based on given depth and additional difficulty over time.
 */
int danger_depth_at(struct player *p, int depth)
{
	return MIN(z_info->max_depth - 1, depth + p->danger);
}

/** Return an effective depth for difficulty of monster generation, etc.
 * based on physical depth and additional difficulty over time.
 */
int danger_depth(struct player *p)
{
	return danger_depth_at(p, p->depth);
}

/** Return the number of 'feeling squares' needed to get an object feeling
 */
int feeling_need(struct player *p)
{
	if (player_has(p, PF_EMOTIONAL_INTELLIGENCE))
		return 1;
	return z_info->feeling_need;
}

/** Return a random message from the death message list */
static const char *random_death_msg(void)
{
	struct death_msg *v, *r = NULL;
	int n;
	for (v = death, n = 1; v; v = v->next, n++)
		if (one_in_(n))
			r = v;
	return r->death_msg;
}

/**
 * Return the top level of a dungeon, this does not care about whether
 * you can get there (so is not blocked by quests)
*/
int dungeon_top_level(const char *dungeon)
{
	/* Get target level */
	int target_level = 0;

	/* Step 1 level at a time until a valid
	 * level is found, or the limit is hit.
	 */
	do {
		target_level ++;

		/* Don't allow levels below max */
		if (target_level > z_info->max_depth - 1) {
			target_level = z_info->max_depth - 1;
			break;
		}
	} while  (!world_level_exists(dungeon, target_level));

	return target_level;
}

/**
 * Increment to the next or decrement to the preceding level,
   accounting for the stair skip value in constants.
   Keep in mind to check all intermediate level for unskippable
   quests.
   Ensure that the target level exists (moving in the same
   direction until it does) - so that town dungeons can start
   at >1.
*/
int dungeon_get_next_level(struct player *p, int dlev, int added)
{
	int target_level, i;

	/* Don't allow any movement from an active quest */
	if (player->active_quest >= 0)
		return dlev;

	/* Get target level */
	target_level = dlev + added * z_info->stair_skip;

	/* Step 1 level at a time (not the skip) until a valid
	 * level is found, or the limit is hit.
	 */
	target_level -= added;
	do {
		target_level += added;

		/* Don't allow levels below max */
		if (target_level > z_info->max_depth - 1) {
			target_level = z_info->max_depth - 1;
			break;
		}

		/* Don't allow levels above the town */
		if (target_level < 0) {
			target_level = 0;
			break;
		}

	} while  (!world_level_exists(NULL, target_level));

	/* Check intermediate levels for quests */
	for (i = dlev; i <= target_level; i++) {
		if (is_blocking_quest(p, i)) return i;
	}

	return target_level;
}

/**
 * Set recall depth for a player recalling from town
 */
void player_set_recall_depth(struct player *p)
{
	/* Account for forced descent */
	if (OPT(p, birth_force_descend)) {
		/* Force descent to a lower level if allowed */
		if ((p->max_depth < z_info->max_depth - 1) && !is_blocking_quest(p, p->max_depth)) {
			p->town->recall_depth = dungeon_get_next_level(p, p->max_depth, 1);
		}
	}

	/* Players who haven't left town before go to the shallowest level */
	p->town->recall_depth = MAX(p->town->recall_depth, dungeon_get_next_level(p, 0, 1));
	struct quest *quest = quest_guardian();
	if (quest) {
		quest->flags |= QF_ACTIVE;
	}
}

/**
 * Give the player the choice of persistent level to recall to.  Note that if
 * a level greater than the player's maximum depth is chosen, we silently go
 * to the maximum depth.
 */
bool player_get_recall_depth(struct player *p)
{
	bool level_ok = false;
	int new = 0;

	while (!level_ok) {
		const char *prompt =
			"Which level do you wish to return to (0 to cancel)? ";
		int i;

		/* Choose the level */
		new = get_quantity(prompt, p->max_depth);
		if (new == 0) {
			return false;
		}

		/* Is that level valid? */
		for (i = 0; i < chunk_list_max; i++) {
			if (chunk_list[i]->depth == new) {
				level_ok = true;
				break;
			}
		}
		if (!level_ok) {
			msg("You must choose a level you have previously visited.");
		}
	}
	p->town->recall_depth = new;
	return true;
}

/**
 * Change dungeon level - e.g. by going up stairs or with WoR.
 */
void dungeon_change_level(struct player *p, int dlev)
{
	/* New depth */
	p->depth = dlev;

	/* If we're returning to town, update the store contents
	   according to how long we've been away */
	if (!dlev && daycount)
		store_update();

	/* Leaving, make new level */
	p->upkeep->generate_level = true;

	/* Save the game when we arrive on the new level. */
	p->upkeep->autosave = true;

	/* Quest specials - before changing level */
	quest_changing_level();
}

/* You are below 0 HP, either after taking damage or after a turn has passed
 * while below 0.
 * You may die, in which case this function will return true.
 * Otherwise (if the CON check succeeded) you will probably suffer - drained stats, etc.
 */
static bool death_check(struct player *p, const char *kb_str)
{
	/* At HP < x, death is guaranteed
	 * At HP = x/2, there is a 50% chance
	 * Intermediate points follow a normal distribution
	 * x is a function of CON and max HP
	 */
	int x = ((player->state.stat_ind[STAT_CON] + 5) * (player->mhp)) / 100;

	// Generate a random value
	int check = Rand_normal(x, x / 2);

	/* Note cause of death */
	if (kb_str)
		my_strcpy(p->died_from, kb_str, sizeof(p->died_from));

	// Return true if it's at least equal to mid
	if (check <= -p->chp) {
		/* e.g. Time Lords may be able to regenerate */
		bool saved = false;
		player_hook(death, &saved);
		if (saved)
			return false;

		/* too bad, so sad */
		/* Hack -- Note death */
		msgt(MSG_DEATH, "You die.");
		event_signal(EVENT_MESSAGE_FLUSH);
		msgt(MSG_DEATH, random_death_msg());
		event_signal(EVENT_MESSAGE_FLUSH);

		/* No longer a winner */
		p->total_winner = false;

		/* Note death */
		p->is_dead = true;
		return true;
	} else {
		/* Mess up your stats */
		for(int stat=0;stat<STAT_MAX;stat++) {
			/* The first time is temporary, though */
			if (Rand_normal(x, x / 2) <= -player->chp) {
				player_stat_dec(p, stat, (p->stat_cur[stat] != p->stat_max[stat]));
			}
		}
		/* and exp similarly */
		if (Rand_normal(x, x / 2) <= -p->chp) {
			player_exp_lose(p, (p->exp + 19) / 20, (p->exp != p->max_exp));
		}
		msgt(MSG_HITPOINT_WARN, "*** FATAL HITPOINT WARNING! ***");
	}

	return false;
}


/**
 * Decreases players hit points and sets death flag if necessary
 *
 * Hack -- this function allows the user to save (or quit) the game
 * when he dies, since the "You die." message is shown before setting
 * the player to "dead".
 */
void take_hit(struct player *p, int dam, const char *kb_str)
{
	int old_chp = p->chp;

	int warning = (p->mhp * p->opts.hitpoint_warn / 10);

	/* Paranoia */
	if (p->is_dead) return;

	/* Mega-Hack -- Apply "invulnerability" */
	if (p->timed[TMD_INVULN] && (dam < FATAL_DAMAGE)) return;

	/* Apply damage reduction */
	dam -= p->state.dam_red;
	if (p->state.perc_dam_red) {
		dam -= (dam * p->state.perc_dam_red) / 100 ;
	}
	if (dam <= 0) return;

	/* Disturb */
	disturb(p);

	/* Hurt the player */
	p->chp -= dam;

	/* Display the hitpoints */
	p->upkeep->redraw |= (PR_HP);

	/* Dead player */
	if (p->chp < 0) {
		/* From hell's heart I stab at thee */
		if (p->timed[TMD_BLOODLUST]
			&& (p->chp + p->timed[TMD_BLOODLUST] + p->lev >= 0)) {
			msg("Your lust for blood keeps you alive!");
		} else if ((p->wizard || OPT(p, cheat_live)) && !get_check("Die? ")) {
			event_signal(EVENT_CHEAT_DEATH);
		} else {
			if (death_check(p, kb_str))
				/* Dead */
				return;
		}
	}

	/* Hitpoint warning */
	if (p->chp < warning) {
		/* Hack -- bell on first notice */
		if (old_chp > warning)
			bell();

		/* Message */
		if (p->chp >= 0)
			msgt(MSG_HITPOINT_WARN, "*** LOW HITPOINT WARNING! ***");
		event_signal(EVENT_MESSAGE_FLUSH);
	}
}

/**
 * Win or not, know inventory, home items and history upon death, enter score
 */
void death_knowledge(struct player *p)
{
	struct store *home = stores ? &stores[STORE_HOME] : NULL;
	struct object *obj;
	time_t death_time = (time_t)0;

	/* Retire in the town in a good state */
	if (p->total_winner) {
		p->depth = 0;
		my_strcpy(p->died_from, "Saved the galaxy and retired a hero", sizeof(p->died_from));
		p->exp = p->max_exp;
		p->lev = p->max_lev;
		p->au += 10000000L;
	}

	player_learn_all_icons(p);
	for (obj = p->gear; obj; obj = obj->next) {
		object_flavor_aware(p, obj);
		obj->known->effect = obj->effect;
		obj->known->activation = obj->activation;
	}

	if (home) {
		for (obj = home->stock; obj; obj = obj->next) {
			object_flavor_aware(p, obj);
			obj->known->effect = obj->effect;
			obj->known->activation = obj->activation;
		}
	}

	history_unmask_unknown(p);

	/* Get time of death */
	(void)time(&death_time);
	enter_score(p, &death_time);

	/* Hack -- Recalculate bonuses */
	p->upkeep->update |= (PU_BONUS);
	handle_stuff(p);
}

/* 0 to maximum moves */
uint16_t *energy_move_pos;
int n_energy_move_pos;

/* -1 to most negative moves */
uint16_t *energy_move_neg;
int n_energy_move_neg;

/* Return the energy scale (x1000) for the given number of levels of movement speed bonus */
uint32_t energy_move_scale(int moves)
{
	uint32_t scale;
	if (moves >= 0) {
		if (moves >= n_energy_move_pos)
			moves = n_energy_move_pos-1;
		scale = energy_move_pos[moves];
	} else {
		moves = -1-moves;
		if (moves >= n_energy_move_neg)
			moves = n_energy_move_neg-1;
		scale = energy_move_neg[moves];
	}
	return scale;
}

/**
 * Energy per move, taking extra moves into account
 */
int energy_per_move(struct player *p)
{
	int num = p->state.num_moves;
	int energy = z_info->move_energy;
	return (energy * energy_move_scale(num)) / 1000;
}

#ifdef TEST_STAT_CHECK
bool test_stat_check(void)
{
	static int sv[] = {
		3, 8, 13, 18, 18+10, 18+50, 18+100, 18+150, 18+220
	};
	int nsv = sizeof(sv)/sizeof(int);
	for(int stat=0;stat<nsv;stat++) {
		int val = sv[stat];
		player->stat_cur[0] = val;
		for(int check=0;check<nsv;check++) {
			int ch = sv[check];
			int freq = 0;
			for(int i=0;i<10000;i++)
				freq += stat_check(0, ch);
			fprintf(stderr,"Stat %d, check %d, freq %d\n", val, ch, freq);
		}
	}
	return false;
}
#endif

/** Randomly return true or false. When the given stat is at the value give, the chance is 50%, when it is higher the chance gets higher
 * following a normal distribution.
 */
bool stat_check(int stat, int mid)
{
	// Get the 'internal' value (3..18, then 18/10 = 18+10 etc.)
	int val = player->stat_cur[stat] + 3;

	// Translate it to a linear form
	if (val < 18) val = ((val - 18) * 10) + 18;

	// Translate the midpoint
	if (mid < 18) mid = ((mid - 18) * 10) + 18;

	// Keep them +ve
	val += 180;
	mid += 180;

	// Generate a random value
	int check = Rand_normal(val, val / 2);

	// Return true if it's at least equal to mid
	if (check >= mid)
		return true;
	else
		return false;
}

/**
 * Modify a stat value by a "modifier", return new value
 *
 * Stats go up: 3,4,...,17,18,18/10,18/20,...,18/220
 * Or even: 18/13, 18/23, 18/33, ..., 18/220
 *
 * Stats go down: 18/220, 18/210,..., 18/10, 18, 17, ..., 3
 * Or even: 18/13, 18/03, 18, 17, ..., 3
 */
int16_t modify_stat_value(int value, int amount)
{
	int i;

	/* Reward or penalty */
	if (amount > 0) {
		/* Apply each point */
		for (i = 0; i < amount; i++) {
			/* One point at a time */
			if (value < 18) value++;

			/* Ten "points" at a time */
			else value += 10;
		}
	} else if (amount < 0) {
		/* Apply each point */
		for (i = 0; i < (0 - amount); i++) {
			/* Ten points at a time */
			if (value >= 18+10) value -= 10;

			/* Hack -- prevent weirdness */
			else if (value > 18) value = 18;

			/* One point at a time */
			else if (value > 3) value--;
		}
	}

	/* Return new value */
	return (value);
}

/**
 * Regenerate one turn's worth of hit points
 */
void player_regen_hp(struct player *p)
{
	int32_t hp_gain;
	int percent = 0;/* max 32k -> 50% of mhp; more accurately "pertwobytes" */
	int fed_pct, old_chp = p->chp;

	if (player_has(p, PF_FORAGING)) {
		/* Foraging = always regenerate as if full */
		if (p->timed[TMD_FOOD] >= PY_FOOD_STARVE)
			percent = PY_REGEN_NORMAL;

		fed_pct = PY_FOOD_MAX;
	} else {
		/* Default regeneration */
		if (p->timed[TMD_FOOD] >= PY_FOOD_WEAK) {
			percent = PY_REGEN_NORMAL;
		} else if (p->timed[TMD_FOOD] >= PY_FOOD_FAINT) {
			percent = PY_REGEN_WEAK;
		} else if (p->timed[TMD_FOOD] >= PY_FOOD_STARVE) {
			percent = PY_REGEN_FAINT;
		}

		fed_pct = p->timed[TMD_FOOD];
	}

	/* Food bonus - better fed players regenerate up to 1/3 faster */
	fed_pct /= z_info->food_value;
	percent *= 100 + fed_pct / 3;
	percent /= 100;

	/* Various things speed up regeneration */
	if (player_of_has(p, OF_REGEN))
		percent *= 2;
	if (player_resting_can_regenerate(p))
		percent *= 2;

	/* Some things slow it down */
	if (player_of_has(p, OF_IMPAIR_HP))
		percent /= 2;

	/* Various things interfere with physical healing */
	if (p->timed[TMD_PARALYZED]) percent = 0;
	if (p->timed[TMD_HELD]) percent = 0;
	if (p->timed[TMD_POISONED]) percent = 0;
	if (p->timed[TMD_INFECTED]) percent /= 4;
	if (p->timed[TMD_STUN]) percent = 0;
	if (p->timed[TMD_RAD]) percent = 0;
	if (p->timed[TMD_CUT]) percent = 0;

	/* Extract the new hitpoints */
	hp_gain = (int32_t)(p->mhp * percent) + PY_REGEN_HPBASE;
	player_adjust_hp_precise(p, hp_gain);

	/* Notice changes */
	if (old_chp != p->chp) {
		equip_learn_flag(p, OF_REGEN);
		equip_learn_flag(p, OF_IMPAIR_HP);
	}
}


void player_adjust_hp_precise(struct player *p, int32_t hp_gain)
{
	int32_t new_chp;
	int num, old_chp = p->chp;

	/* Load it all into 4 byte format*/
	new_chp = (int32_t)((p->chp << 16) + p->chp_frac) + hp_gain;

	/* Check for overflow */
	/*     {new_chp = LONG_MIN;} DAVIDTODO*/
	if ((new_chp < 0) && (old_chp > 0) && (hp_gain > 0)) {
		new_chp = INT32_MAX;
	} else if ((new_chp > 0) && (old_chp < 0) && (hp_gain < 0)) {
		new_chp = INT32_MIN;
	}

	/* Break it back down*/
	p->chp = (int16_t)(new_chp >> 16);   /* div 65536 */
	p->chp_frac = (uint16_t)(new_chp & 0xFFFF); /* mod 65536 */
	/*DAVIDTODO neg new_chp ok? I think so because eg a slightly negative
	 * new_chp will give -1 for chp and very high chp_frac.*/

	/* Fully healed */
	if (p->chp >= p->mhp) {
		p->chp = p->mhp;
		p->chp_frac = 0;
	}

	num = p->chp - old_chp;
	if (num == 0)
		return;

	p->upkeep->redraw |= (PR_HP);
}

void light_special_activation(struct object *obj)
{
	bool ident = false;
	bool was_aware = object_flavor_is_aware(obj);
	int dir = randint1(8);
	if (obj->kind->effect_msg)
		print_custom_message(obj, obj->kind->effect_msg, MSG_GENERIC, player);
	object_flavor_aware(player, obj);
	if ((!was_aware) && (object_is_carried(player, obj)))
		print_custom_message(obj, "You realize you were carrying a {kind}!", MSG_GENERIC, player);
	struct effect effect;
	memcpy(&effect, obj->effect, sizeof(effect));
	effect.x = obj->grid.x;
	effect.y = obj->grid.y;
	effect_do(&effect, source_object(obj), NULL, &ident, was_aware, dir, 0, 0, NULL, 0);
}

void light_timeout(struct object *obj, bool delete)
{
	/* The light is now out */
	disturb(player);

	/* Special handling for some 'lights' */
	if (object_effect(obj) && (of_has(obj->flags, OF_NO_ACTIVATION))) {
		light_special_activation(obj);
	} else {
		/* Default burning out message */
		if (object_is_carried(player, obj))
			msg("Your light has gone out!");
	}

	/* If it's a torch, now is the time to delete it */
	if (of_has(obj->flags, OF_BURNS_OUT)) {
		bool dummy;
		struct object *burnt;
		if (obj->held_m_idx) {
			pile_excise(&cave_monster(cave, obj->held_m_idx)->held_obj, obj);
			burnt = obj;
		} else {
			if (object_is_carried(player, obj))
				burnt = gear_object_for_use(player, obj, 1, true, &dummy);
			else if (pile_contains(square_object(cave, obj->grid), obj))
				burnt = floor_object_for_use(player, obj, 1, true, &dummy);
			else
				burnt = obj;
		}
		if (delete) {
			if (burnt->known)
				object_delete(cave, player->cave, &burnt->known);
			object_delete(player->cave, NULL, &burnt);
		}
	} else if (obj_has_flag(obj, OF_STICKY) && object_is_carried(player, obj)) {
		/* Remind you that you can take it off now */
		msg("It shifts about, and you realise that it is no longer attached to you.");
	}
}

/**
 * Update the player's light fuel
 */
void player_update_light(struct player *p)
{
	/* Check for light being wielded */
	struct object *obj = equipped_item_by_slot_name(p, "light");

	/* Burn some fuel in the current light */
	if (obj && tval_is_light(obj)) {
		bool burn_fuel = true;

		/* Turn off the wanton burning of light during the day in the town */
		if (!p->depth && is_daytime() && (!of_has(obj->flags, OF_BURNS_OUT)))
			burn_fuel = false;

		/* If the light has the NO_FUEL flag, well... */
		if (of_has(obj->flags, OF_NO_FUEL))
		    burn_fuel = false;
		/* Use some fuel (except on artifacts, or during the day) */
		if (burn_fuel && ((obj->timeout > 0) || (of_has(obj->flags, OF_BURNS_OUT)))) {
			int burnable = obj->modifiers[OBJ_MOD_USE_ENERGY];
			int burnstep = burnable;
			if (burnable > obj->timeout)
				burnable = obj->timeout;

			/* Decrease life-span */
			int prev = obj->timeout;
			obj->timeout -= burnable;

			/* Hack -- notice interesting fuel steps */
			if ((obj->timeout < 100) || ((obj->timeout / 100) != (prev / 100)))
				/* Redraw stuff */
				p->upkeep->redraw |= (PR_EQUIP);

			/* Hack -- Special treatment when blind */
			if (p->timed[TMD_BLIND]) {
				/* Hack -- save some light for later */
				if (obj->timeout == 0) obj->timeout = prev;
			} else if (obj->timeout == 0) {
				light_timeout(obj, true);
			} else if ((obj->timeout < 50 * burnstep) && (!(obj->timeout % (20 * burnstep)))) {
				/* The light is getting dim */
				disturb(p);
				msg("Your light is growing faint.");
			}
		}
	}

	/* Calculate torch radius */
	p->upkeep->update |= (PU_TORCH);
}

/**
 * Find the player's best digging tool.  If forbid_stack is true, ignores
 * stacks of more than one item.
 */
struct object *player_best_digger(struct player *p, bool forbid_stack)
{
	int weapon_slot = slot_by_name(p, "weapon");
	struct object *current_weapon = slot_object(p, weapon_slot);
	struct object *obj, *best = NULL;
	/* Prefer any melee weapon over unarmed digging, i.e. best == NULL. */
	int best_score = -1;
	struct player_state local_state;

	for (obj = p->gear; obj; obj = obj->next) {
		int score, old_number;
		if (!tval_is_melee_weapon(obj)) continue;
		if (obj->number < 1 || (forbid_stack && obj->number > 1)) continue;
		/* Don't use it if it has a sticky curse. */
		if (!obj_can_takeoff(obj)) continue;

		/* Swap temporarily for the calc_bonuses() computation. */
		old_number = obj->number;
		if (obj != current_weapon) {
			obj->number = 1;
			p->body.slots[weapon_slot].obj = obj;
		}

		/*
		 * Avoid side effects from using update set to false
		 * with calc_bonuses().
		 */
		local_state.stat_ind[STAT_STR] = 0;
		local_state.stat_ind[STAT_DEX] = 0;
		calc_bonuses(p, &local_state, true, false);
		score = local_state.skills[SKILL_DIGGING];

		/* Swap back. */
		if (obj != current_weapon) {
			obj->number = old_number;
			p->body.slots[weapon_slot].obj = current_weapon;
		}

		score += obj->modifiers[OBJ_MOD_TUNNEL]
			* p->obj_k->modifiers[OBJ_MOD_TUNNEL];
		/* Convert tunnel modifier to digging skill as in player-calcs.c */
		score *= 20;
		/* Add in weapon weight (does not account for heavy wield) */
		score += obj->weight / 454;

		if (score > best_score) {
			best = obj;
			best_score = score;
		}
	}

	return best;
}

/**
 * Melee a random adjacent monster
 */
bool player_attack_random_monster(struct player *p)
{
	int i, dir = randint0(8);

	/* Confused players get a free pass */
	if (p->timed[TMD_CONFUSED]) return false;

	/* Look for a monster, attack */
	for (i = 0; i < 8; i++, dir++) {
		struct loc grid = loc_sum(p->grid, ddgrid_ddd[dir % 8]);
		const struct monster *mon = square_monster(cave, grid);
		if (mon && !monster_is_camouflaged(mon)) {
			p->upkeep->energy_use = z_info->move_energy;
			msg("You angrily lash out at a nearby foe!");
			py_attack(p, grid);
			return true;
		}
	}
	return false;
}

/**
 * Have random bad stuff happen to the player from over-exertion
 *
 * This function uses the PY_EXERT_* flags
 */
void player_over_exert(struct player *p, int flag, int chance, int amount)
{
	if (chance <= 0) return;

	/* CON damage */
	if (flag & PY_EXERT_CON) {
		if (randint0(100) < chance) {
			/* Hack - only permanent with high chance */
			bool perm = (randint0(100) < chance / 2) && (chance >= 50);
			msg("You have damaged your health!");
			player_stat_dec(p, STAT_CON, perm);
		}
	}

	/* Fainting */
	if (flag & PY_EXERT_FAINT) {
		if (randint0(100) < chance) {
			msg("You faint from the effort!");

			/* Bypass free action */
			(void)player_inc_timed(p, TMD_PARALYZED, randint1(amount),
								   true, false);
		}
	}

	/* Scrambled stats */
	if (flag & PY_EXERT_SCRAMBLE) {
		if (randint0(100) < chance) {
			(void)player_inc_timed(p, TMD_SCRAMBLE, randint1(amount),
								   true, true);
		}
	}

	/* Cut damage */
	if (flag & PY_EXERT_CUT) {
		if (randint0(100) < chance) {
			msg("Wounds appear on your body!");
			(void)player_inc_timed(p, TMD_CUT, randint1(amount),
								   true, false);
		}
	}

	/* Confusion */
	if (flag & PY_EXERT_CONF) {
		if (randint0(100) < chance) {
			(void)player_inc_timed(p, TMD_CONFUSED, randint1(amount),
								   true, true);
		}
	}

	/* Hallucination */
	if (flag & PY_EXERT_HALLU) {
		if (randint0(100) < chance) {
			(void)player_inc_timed(p, TMD_IMAGE, randint1(amount),
								   true, true);
		}
	}

	/* Slowing */
	if (flag & PY_EXERT_SLOW) {
		if (randint0(100) < chance) {
			msg("You feel suddenly lethargic.");
			(void)player_inc_timed(p, TMD_SLOW, randint1(amount),
								   true, false);
		}
	}

	/* HP */
	if (flag & PY_EXERT_HP) {
		if (randint0(100) < chance) {
			msg("You cry out in sudden pain!");
			take_hit(p, randint1(amount), "over-exertion");
		}
	}
}


/**
 * See how much damage the player will take from damaging terrain
 */
int player_check_terrain_damage(struct player *p, struct loc grid)
{
	int dam_taken = 0;

	if (square_isfiery(cave, grid)) {
		int base_dam = 100 + randint1(100);
		int res = p->state.el_info[ELEM_FIRE].res_level;

		/* Fire damage */
		dam_taken = adjust_dam(p, ELEM_FIRE, base_dam, RANDOMISE, res, false);

		/* Feather fall makes one lightfooted. */
		if (player_of_has(p, OF_FEATHER)) {
			dam_taken /= 2;
		}
	} else if (square_isradioactive(cave, grid)) {
		int base_dam = 20 + randint1(40);
		int res = p->state.el_info[ELEM_RADIATION].res_level;

		/* Radiation damage */
		dam_taken = adjust_dam(p, ELEM_RADIATION, base_dam, RANDOMISE, res, false);
	} else if (square_iswater(cave, grid)) {
		int base_dam = 20 + randint1(40);
		int res = p->state.el_info[ELEM_WATER].res_level;

		/* Feather fall = water wings. This even helps if you can't swim. */
		if (player_of_has(p, OF_FEATHER)) {
			dam_taken -= 20;
		}

		if (!player_has(p, PF_NO_SWIMMING)) {
			/* Most players can swim, but are limited by weight.
			 * So obtain the load as a % of the limit (the point at which weight speed
			 * penalties start)...
			 **/
			int load = (p->upkeep->total_weight * 100) / weight_limit(&player->state);

			/* And reduce depending on the load.
			 * So (assuming no feather falling) at 130%+ load you will take full damage,
			 * at 70%- load you will take no damage and at 110%- load, as well as seeing
			 * less max damage you will sometimes take no damage.
			 **/
			if (load < 130) {
				base_dam -= (130 - load);
			}
		}

		/* Bound */
		if (base_dam < 0)
			base_dam = 0;

		/* Water damage */
		dam_taken = adjust_dam(p, ELEM_WATER, base_dam, RANDOMISE, res, false);
	}

	return dam_taken;
}

/**
 * Terrain damages the player
 */
void player_take_terrain_damage(struct player *p, struct loc grid)
{
	int dam_taken = player_check_terrain_damage(p, grid);

	if (!dam_taken) {
		return;
	}

	/* Damage the player and inventory */
	take_hit(p, dam_taken, square_feat(cave, grid)->die_msg);
	if (square_isfiery(cave, grid)) {
		msg(square_feat(cave, grid)->hurt_msg);
		inven_damage(p, PROJ_FIRE, dam_taken);
	} else if (square_isradioactive(cave, grid)) {
		msg(square_feat(cave, grid)->hurt_msg);
		inven_damage(p, PROJ_RADIATION, dam_taken);
		player_inc_timed(p, TMD_RAD, dam_taken, false, false);
	} else if (square_iswater(cave, grid)) {
		msg(square_feat(cave, grid)->hurt_msg);
		inven_damage(p, PROJ_WATER, dam_taken);
	}
}

/**
 * Find a player shape from the name
 */
struct player_shape *lookup_player_shape(const char *name)
{
	struct player_shape *shape = shapes;
	while (shape) {
		if (streq(shape->name, name)) {
			return shape;
		}
		shape = shape->next;
	}
	msg("Could not find %s shape!", name);
	return NULL;
}

/**
 * Find a player shape index from the shape name
 */
int shape_name_to_idx(const char *name)
{
	struct player_shape *shape = lookup_player_shape(name);
	if (shape) {
		return shape->sidx;
	} else {
		return -1;
	}
}

/**
 * Find a player shape from the index
 */
struct player_shape *player_shape_by_idx(int index)
{
	struct player_shape *shape = shapes;
	while (shape) {
		if (shape->sidx == index) {
			return shape;
		}
		shape = shape->next;
	}
	msg("Could not find shape %d!", index);
	return NULL;
}

/* Change player shape */
void shapechange(struct player *p, const char *shapename, bool verbose)
{
	if (player->flying) {
		effect_handler_LAND(NULL);
	}

	/* Change shape */
	int32_t old_mhp = p->mhp;
	p->shape = lookup_player_shape(shapename);
	if (verbose) {
		msg("You assume the shape of a %s!", shapename);
		msg("Your gear merges into your body.");
	}

	/* Update */
	shape_learn_on_assume(p, shapename);
	p->upkeep->update |= (PU_BONUS);
	update_stuff(p);

	/* Scale HP */
	int32_t new_mhp = p->mhp;
	p->chp = (p->chp * new_mhp) / old_mhp;

	/* and redraw */
	p->upkeep->update |= (PU_BONUS);
	p->upkeep->redraw |= (PR_TITLE | PR_MISC);
	handle_stuff(p);
}

/**
 * Give shapechanged players a choice of returning to normal shape and
 * performing a command, just returning to normal shape without acting, or
 * canceling.
 *
 * \param p the player
 * \param cmd the command being performed
 * \return true if the player wants to proceed with their command
 */
bool player_get_resume_normal_shape(struct player *p, struct command *cmd)
{
	if (player_is_shapechanged(p)) {
		msg("You cannot do this while in %s form.", p->shape->name);
		char prompt[100];
		strnfmt(prompt, sizeof(prompt),
		        "Change back and %s (y/n) or (r)eturn to normal? ",
		        cmd_verb(cmd->code));
		char answer = get_char(prompt, "yrn", 3, 'n');

		// Change back to normal shape
		if (answer == 'y' || answer == 'r') {
			player_resume_normal_shape(p);
		}

		// Players may only act if they return to normal shape
		return answer == 'y';
	}

	// Normal shape players can proceed as usual
	return true;
}

/**
 * Revert to normal shape
 */
void player_resume_normal_shape(struct player *p)
{
	/* Kill vampire attack */
	(void) player_clear_timed(p, TMD_ATT_VAMP, true);

	shapechange(p, "normal", false);
	msg("You resume your usual shape.");
}

/**
 * Check if the player is shapechanged
 */
bool player_is_shapechanged(struct player *p)
{
	return streq(p->shape->name, "normal") ? false : true;
}

/**
 * Check if the player is immune from traps
 */
bool player_is_trapsafe(struct player *p)
{
	if (p->timed[TMD_TRAPSAFE]) return true;
	if (player_of_has(p, OF_TRAP_IMMUNE)) return true;
	return false;
}

/**
 * Return true if the player can use an intrinsic ability.
 *
 * \param p is the player
 * \param show_msg should be set to true if a failure message should be
 * displayed.
 */
bool player_can_cast(struct player *p, bool show_msg)
{
	if (p->timed[TMD_CONFUSED]) {
		if (show_msg) {
			msg("You are too confused!");
		}
		return false;
	}

	return true;
}

/**
 * Return true if the player can run cards.
 *
 * \param p is the player
 * \param show_msg should be set to true if a failure message should be
 * displayed.
 */
bool player_can_run(struct player *p, bool show_msg)
{
	if (p->timed[TMD_BLIND]) {
		if (show_msg)
			msg("You can't see anything.");

		return false;
	}

	if (p->timed[TMD_CONFUSED]) {
		if (show_msg)
			msg("You are too confused!");

		return false;
	}

	if (p->timed[TMD_AMNESIA]) {
		if (show_msg)
			msg("You can't remember how!");

		return false;
	}

	return true;
}

/**
 * Return true if the player can fire something with a launcher.
 *
 * \param p is the player
 * \param show_msg should be set to true if a failure message should be
 * displayed.
 */
bool player_can_fire(struct player *p, bool show_msg)
{
	struct object *obj = equipped_item_by_slot_name(p, "shooting");

	/* Require a usable launcher */
	if (!obj || !p->state.ammo_tval) {
		if (show_msg)
			msg("You have nothing to fire with.");
		return false;
	}

	return true;
}

/**
 * Return true if the player can refuel their light source.
 *
 * \param p is the player
 * \param show_msg should be set to true if a failure message should be
 * displayed.
 */
bool player_can_refuel(struct player *p, bool show_msg)
{
	struct object *obj = equipped_item_by_slot_name(p, "light");

	if (obj && of_has(obj->flags, OF_TAKES_FUEL)) {
		return true;
	}

	if (show_msg) {
		msg("Your light cannot be refuelled.");
	}

	return false;
}

/**
 * Prerequisite function for command. See struct cmd_info in ui-input.h and
 * it's use in ui-game.c.
 */
bool player_can_cast_prereq(void)
{
	return player_can_cast(player, true);
}

/**
 * Prerequisite function for command. See struct cmd_info in ui-input.h and
 * it's use in ui-game.c.
 */
bool player_can_run_prereq(void)
{
	/*
	 * Accomodate hacks elsewhere:  'r' is overloaded to mean
	 * release a commanded monster when TMD_COMMAND is active.
	 */
	return (player->timed[TMD_COMMAND]) ?
		true : player_can_run(player, true);
}

/**
 * Prerequisite function for command. See struct cmd_info in ui-input.h and
 * it's use in ui-game.c.
 */
bool player_can_fire_prereq(void)
{
	return player_can_fire(player, true);
}

/**
 * Prerequisite function for command. See struct cmd_info in ui-input.h and
 * it's use in ui-game.c.
 */
bool player_can_refuel_prereq(void)
{
	return player_can_refuel(player, true);
}

/**
 * Prerequisite function for command. See struct cmd_info in ui-input.h and
 * it's use in ui-game.c.
 */
bool player_can_debug_prereq(void)
{
	if (player->noscore & NOSCORE_DEBUG) {
		return true;
	}
	if (confirm_debug()) {
		/* Mark savefile */
		player->noscore |= NOSCORE_DEBUG;
		return true;
	}
	return false;
}

/**
 * Apply confusion, if needed, to a direction
 *
 * Display a message and return true if direction changes.
 */
bool player_confuse_dir(struct player *p, int *dp, bool too)
{
	int dir = *dp;

	if (p->timed[TMD_CONFUSED]) {
		if ((dir == 5) || (randint0(100) < 75)) {
			/* Random direction */
			dir = ddd[randint0(8)];
		}

	/* Running attempts always fail */
	if (too) {
		msg("You are too confused.");
		return true;
	}

	if (*dp != dir) {
		msg("You are confused.");
		*dp = dir;
		return true;
	}
	}

	return false;
}

/**
 * Return true if the provided count is one of the conditional REST_ flags.
 */
bool player_resting_is_special(int16_t count)
{
	switch (count) {
		case REST_COMPLETE:
		case REST_ALL_POINTS:
		case REST_SOME_POINTS:
			return true;
	}

	return false;
}

/**
 * Return true if the player is resting.
 */
bool player_is_resting(struct player *p)
{
	return (p->upkeep->resting > 0 ||
			player_resting_is_special(p->upkeep->resting));
}

/**
 * Return the remaining number of resting turns.
 */
int16_t player_resting_count(struct player *p)
{
	return p->upkeep->resting;
}

/**
 * In order to prevent the regeneration bonus from the first few turns, we have
 * to store the number of turns the player has rested. Otherwise, the first
 * few turns will have the bonus and the last few will not.
 */
static int player_turns_rested = 0;
static bool player_rest_disturb = false;

/**
 * Set the number of resting turns.
 *
 * \param count is the number of turns to rest or one of the REST_ constants.
 */
void player_resting_set_count(struct player *p, int16_t count)
{
	/* Cancel if player is disturbed */
	if (player_rest_disturb) {
		p->upkeep->resting = 0;
		player_rest_disturb = false;
		return;
	}

	/* Ignore if the rest count is negative. */
	if ((count < 0) && !player_resting_is_special(count)) {
		p->upkeep->resting = 0;
		return;
	}

	/* Save the rest code */
	p->upkeep->resting = count;

	/* Truncate overlarge values */
	if (p->upkeep->resting > 9999) p->upkeep->resting = 9999;
}

/**
 * Cancel current rest.
 */
void player_resting_cancel(struct player *p, bool disturb)
{
	player_resting_set_count(p, 0);
	player_turns_rested = 0;
	player_rest_disturb = disturb;
}

/**
 * Return true if the player should get a regeneration bonus for the current
 * rest.
 */
bool player_resting_can_regenerate(struct player *p)
{
	return player_turns_rested >= REST_REQUIRED_FOR_REGEN ||
		player_resting_is_special(p->upkeep->resting);
}

/**
 * Perform one turn of resting. This only handles the bookkeeping of resting
 * itself, and does not calculate any possible other effects of resting (see
 * process_world() for regeneration).
 */
void player_resting_step_turn(struct player *p)
{
	/* Timed rest */
	if (p->upkeep->resting > 0) {
		/* Reduce rest count */
		p->upkeep->resting--;

		/* Redraw the state */
		p->upkeep->redraw |= (PR_STATE);
	}

	/* Take a turn */
	p->upkeep->energy_use = z_info->move_energy;

	/* Increment the resting counters */
	p->resting_turn++;
	player_turns_rested++;
}

/**
 * Handle the conditions for conditional resting (resting with the REST_
 * constants).
 */
void player_resting_complete_special(struct player *p)
{
	/* Complete resting */
	if (!player_resting_is_special(p->upkeep->resting)) return;

	if (p->upkeep->resting == REST_ALL_POINTS) {
		if (p->chp == p->mhp)
			/* Stop resting */
			disturb(p);
	} else if (p->upkeep->resting == REST_COMPLETE) {
		if ((p->chp == p->mhp) &&
			!p->timed[TMD_BLIND] && !p->timed[TMD_CONFUSED] &&
			!p->timed[TMD_POISONED] && !p->timed[TMD_AFRAID] &&
			!p->timed[TMD_TERROR] && !p->timed[TMD_STUN] &&
			!p->timed[TMD_CUT] && !p->timed[TMD_SLOW] &&
			!p->timed[TMD_PARALYZED] && !p->timed[TMD_IMAGE] &&
			!p->timed[TMD_HELD] &&
			!p->word_recall && !p->deep_descent)
			/* Stop resting. Infected is left off intentionally */
			disturb(p);
	} else if (p->upkeep->resting == REST_SOME_POINTS) {
		if (p->chp == p->mhp)
			/* Stop resting */
			disturb(p);
	}
}

/* Record the player's last rest count for repeating */
static int player_resting_repeat_count = 0;

/**
 * Get the number of resting turns to repeat.
 *
 * \param p The current player.
 */
int player_get_resting_repeat_count(struct player *p)
{
	return player_resting_repeat_count;
}

/**
 * Set the number of resting turns to repeat.
 *
 * \param count is the number of turns requested for rest most recently.
 */
void player_set_resting_repeat_count(struct player *p, int16_t count)
{
	player_resting_repeat_count = count;
}

/**
 * Check if the player state has the given OF_ flag.
 */
bool player_of_has(struct player *p, int flag)
{
	assert(p);
	return of_has(p->state.flags, flag);
}

/**
 * Check if the player resists (or better) an element
 */
bool player_resists(struct player *p, int element)
{
	return (p->state.el_info[element].res_level > 0);
}

/**
 * Check if the player resists (or better) an element
 */
bool player_is_immune(struct player *p, int element)
{
	return (p->state.el_info[element].res_level == IMMUNITY);
}

/**
 * Places the player at the given coordinates in the cave.
 */
void player_place(struct chunk *c, struct player *p, struct loc grid)
{
	assert(!square_monster(c, grid));

	/* Save player location */
	p->grid = grid;

	/* Mark cave grid */
	square_set_mon(c, grid, -1);

	/* Clear stair creation */
	p->upkeep->create_down_stair = false;
	p->upkeep->create_up_stair = false;
}

/*
 * Something has happened to disturb the player.
 *
 * The first arg indicates a major disturbance, which affects search.
 *
 * The second arg is currently unused, but could induce output flush.
 *
 * All disturbance cancels repeated commands, resting, and running.
 *
 * XXX-AS: Make callers either pass in a command
 * or call cmd_cancel_repeat inside the function calling this
 */
void disturb(struct player *p)
{
	/* Cancel repeated commands */
	cmd_cancel_repeat();

	/* Cancel Resting */
	if (player_is_resting(p)) {
		player_resting_cancel(p, true);
		p->upkeep->redraw |= PR_STATE;
	}

	/* Cancel running */
	if (p->upkeep->running) {
		p->upkeep->running = 0;

		/* Cancel queued commands */
		cmdq_flush();

		/* Check for new panel if appropriate */
		event_signal(EVENT_PLAYERMOVED);
		p->upkeep->update |= PU_TORCH;

		/* Mark the whole map to be redrawn */
		event_signal_point(EVENT_MAP, -1, -1);
	}

	/* Flush input */
	event_signal(EVENT_INPUT_FLUSH);
}

/**
 * Search for traps or secret doors
 */
void search(struct player *p)
{
	struct loc grid;
	char o_name[80];

	/* Various conditions mean no searching */
	if (p->timed[TMD_BLIND] || no_light(p) ||
		p->timed[TMD_CONFUSED] || p->timed[TMD_IMAGE])
		return;

	/* Search the nearby grids, which are always in bounds */
	for (grid.y = (p->grid.y - 1); grid.y <= (p->grid.y + 1); grid.y++) {
		for (grid.x = (p->grid.x - 1); grid.x <= (p->grid.x + 1); grid.x++) {
			struct object *obj;

			/* Secret doors */
			if (square_issecretdoor(cave, grid)) {
				msg("You have found a secret door.");
				place_closed_door(cave, grid);
				disturb(p);
			}

			/* Traps on chests */
			for (obj = square_object(cave, grid); obj; obj = obj->next) {
				if (!obj->known || ignore_item_ok(p, obj)
						|| !is_trapped_chest(obj)) {
					continue;
				}

				if (obj->known->pval != obj->pval) {
					/* Describe the object */
					object_desc(o_name, sizeof(o_name), obj, ODESC_BASE, p);
					msg("You have discovered a trap on the %s!", &o_name);
					obj->known->pval = obj->pval;
					disturb(p);
				}
			}
		}
	}
}
