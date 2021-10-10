/**
 * \file player-attack.c
 * \brief Attacks (both throwing and melee) by the player
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
#include "cave.h"
#include "cmds.h"
#include "effects.h"
#include "game-event.h"
#include "game-input.h"
#include "generate.h"
#include "init.h"
#include "mon-attack.h"
#include "mon-desc.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "mon-msg.h"
#include "mon-predicate.h"
#include "mon-timed.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-pile.h"
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-ability.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "target.h"

/**
 * ------------------------------------------------------------------------
 * Hit and breakage calculations
 * ------------------------------------------------------------------------ */
/**
 * Returns percent chance of an object breaking after throwing or shooting.
 *
 * Artifacts will never break.
 *
 * Beyond that, each item kind has a percent chance to break (0-100). When the
 * object hits its target this chance is used.
 *
 * When an object misses it also has a chance to break. This is determined by
 * squaring the normal breakage probability. So an item that breaks 100% of
 * the time on hit will also break 100% of the time on a miss, whereas a 50%
 * hit-breakage chance gives a 25% miss-breakage chance, and a 10% hit breakage
 * chance gives a 1% miss-breakage chance.
 */
int breakage_chance(const struct object *obj, bool hit_target) {
	int perc = obj->kind->base->break_perc;
	if (obj->artifact) return 0;
	if (of_has(obj->flags, OF_THROWING) &&
		!of_has(obj->flags, OF_EXPLODE) &&
		!tval_is_ammo(obj)) {
		perc = 1;
	}
	if (!hit_target) return (perc * perc) / 100;
	return perc;
}

/**
 * Calculate the player's base melee to-hit value without regard to a specific
 * monster.
 * See also: chance_of_missile_hit_base
 *
 * \param p The player
 * \param weapon The player's weapon
 */
int chance_of_melee_hit_base(const struct player *p,
		const struct object *weapon)
{
	int bonus = p->state.to_h + (weapon ? weapon->to_h : 0);
	int chance;
	if (weapon) {
		bonus += weapon->to_h;
		chance = p->state.skills[SKILL_TO_HIT_MELEE];
	} else {
		bonus += p->state.skills[SKILL_TO_HIT_MARTIAL] / 4;
		chance = p->state.skills[SKILL_TO_HIT_MARTIAL];
	}
	chance += bonus * BTH_PLUS_ADJ;
	return chance;
}

/**
 * Calculate the player's melee to-hit value against a specific monster.
 * See also: chance_of_missile_hit
 *
 * \param p The player
 * \param weapon The player's weapon
 * \param mon The monster
 */
static int chance_of_melee_hit(const struct player *p,
		const struct object *weapon, const struct monster *mon)
{
	int chance = chance_of_melee_hit_base(p, weapon);
	/* Non-visible targets have a to-hit penalty of 50% */
	return (mon ? monster_is_visible(mon) : true) ? chance : chance / 2;
}

/**
 * Calculate the player's base missile to-hit value without regard to a specific
 * monster.
 * See also: chance_of_melee_hit_base
 *
 * \param p The player
 * \param missile The missile to launch
 * \param launcher The launcher to use (optional)
 */
static int chance_of_missile_hit_base(const struct player *p,
								 const struct object *missile,
								 const struct object *launcher)
{
	int bonus = missile->to_h;
	int chance;

	if (!launcher) {
		/* Other thrown objects are easier to use, but only throwing weapons 
		 * take advantage of bonuses to Skill and Deadliness from other 
		 * equipped items. */
		if (of_has(missile->flags, OF_THROWING)) {
			bonus += p->state.to_h;
			chance = p->state.skills[SKILL_TO_HIT_THROW] + bonus * BTH_PLUS_ADJ;
		} else {
			chance = 3 * p->state.skills[SKILL_TO_HIT_THROW] / 2
				+ bonus * BTH_PLUS_ADJ;
		}
	} else {
		bonus += p->state.to_h + launcher->to_h;
		chance = p->state.skills[SKILL_TO_HIT_GUN] + bonus * BTH_PLUS_ADJ;
	}

	return chance;
}

/**
 * Calculate the player's missile to-hit value against a specific monster.
 * See also: chance_of_melee_hit
 *
 * \param p The player
 * \param missile The missile to launch
 * \param launcher Optional launcher to use (thrown weapons use no launcher)
 * \param mon The monster
 */
static int chance_of_missile_hit(const struct player *p,
	const struct object *missile, const struct object *launcher,
	const struct monster *mon)
{
	int chance = chance_of_missile_hit_base(p, missile, launcher);
	/* Penalize for distance */
	chance -= distance(p->grid, mon->grid);
	/* Non-visible targets have a to-hit penalty of 50% */
	return (mon ? monster_is_obvious(mon) : true) ? chance : chance / 2;
}

/**
 * Determine if a hit roll is successful against the target AC.
 * See also: hit_chance
 *
 * \param to_hit To total to-hit value to use
 * \param ac The AC to roll against
 */
bool test_hit(int to_hit, int ac)
{
	random_chance c;
	hit_chance(&c, to_hit, ac);
	return random_chance_check(c);
}

/**
 * Return a random_chance by reference, which represents the likelihood of a
 * hit roll succeeding for the given to_hit and ac values. The hit calculation
 * will:
 *
 * Always hit 12% of the time
 * Always miss 5% of the time
 * Put a floor of 9 on the to-hit value
 * Roll between 0 and the to-hit value
 * The outcome must be >= AC*2/3 to be considered a hit
 *
 * \param chance The random_chance to return-by-reference
 * \param to_hit The to-hit value to use
 * \param ac The AC to roll against
 */
void hit_chance(random_chance *chance, int to_hit, int ac)
{
	/* Percentages scaled to 10,000 to avoid rounding error */
	const int HUNDRED_PCT = 10000;
	const int ALWAYS_HIT = 1200;
	const int ALWAYS_MISS = 500;

	/* Put a floor on the to_hit */
	to_hit = MAX(9, to_hit);

	/* Calculate the hit percentage */
	chance->numerator = MAX(0, to_hit - ac * 2 / 3);
	chance->denominator = to_hit;

	/* Convert the ratio to a scaled percentage */
	chance->numerator = HUNDRED_PCT * chance->numerator / chance->denominator;
	chance->denominator = HUNDRED_PCT;

	/* The calculated rate only applies when the guaranteed hit/miss don't */
	chance->numerator = chance->numerator *
			(HUNDRED_PCT - ALWAYS_MISS - ALWAYS_HIT) / HUNDRED_PCT;

	/* Add in the guaranteed hit */
	chance->numerator += ALWAYS_HIT;
}

/**
 * ------------------------------------------------------------------------
 * Damage calculations
 * ------------------------------------------------------------------------ */
/**
 * Conversion of plusses to Deadliness to a percentage added to damage.
 * Much of this table is not intended ever to be used, and is included
 * only to handle possible inflation elsewhere. -LM-
 */
byte *deadliness_conversion;
int n_deadliness_conversion;

/**
 * Deadliness multiplies the damage done by a percentage, which varies 
 * from 0% (no damage done at all) to at most 355% (damage is multiplied 
 * by more than three and a half times!).
 *
 * We use the table "deadliness_conversion" to translate internal plusses 
 * to deadliness to percentage values.
 *
 * This function multiplies damage by 100.
 */
void apply_deadliness(int *die_average, int deadliness)
{
	int i;

	/* Paranoia - ensure legal table access. */
	if (deadliness >= n_deadliness_conversion)
		deadliness = n_deadliness_conversion-1;
	if (deadliness <= -n_deadliness_conversion)
		deadliness = (-n_deadliness_conversion) + 1;

	/* Deadliness is positive - damage is increased */
	if (deadliness >= 0) {
		i = deadliness_conversion[deadliness];

		*die_average *= (100 + i);
	}

	/* Deadliness is negative - damage is decreased */
	else {
		i = deadliness_conversion[ABS(deadliness)];

		if (i >= 100)
			*die_average = 0;
		else
			*die_average *= (100 - i);
	}
}

/**
 * Check if a monster is debuffed in such a way as to make a critical
 * hit more likely.
 */
static bool is_debuffed(const struct monster *monster)
{
	return monster->m_timed[MON_TMD_CONF] > 0 ||
			monster->m_timed[MON_TMD_HOLD] > 0 ||
			monster->m_timed[MON_TMD_FEAR] > 0 ||
			monster->m_timed[MON_TMD_STUN] > 0;
}

/**
 * Determine damage for critical hits from shooting.
 *
 * Factor in item weight, total plusses, and player level.
 */
static int critical_shot(const struct player *p,
		const struct monster *monster,
		int weight, int plus,
		int dam, u32b *msg_type)
{
	int debuff_to_hit = 0;
	if ((monster) && (is_debuffed(monster)))
		debuff_to_hit = DEBUFF_CRITICAL_HIT;
	int chance = (weight / 20) + (p->state.to_h + plus + debuff_to_hit) * 4 + p->lev * 2;
	int power = (weight / 20) + randint1(500);
	int new_dam = dam;

	if (randint1(5000) > chance) {
		*msg_type = MSG_SHOOT_HIT;
	} else if (power < 500) {
		*msg_type = MSG_HIT_GOOD;
		new_dam = 2 * dam + 5;
	} else if (power < 1000) {
		*msg_type = MSG_HIT_GREAT;
		new_dam = 2 * dam + 10;
	} else {
		*msg_type = MSG_HIT_SUPERB;
		new_dam = 3 * dam + 15;
	}

	return new_dam;
}

/**
 * Determine O-combat damage for critical hits from shooting.
 *
 * Factor in item weight, total plusses, and player level.
 */
static int o_critical_shot(const struct player *p,
						   const struct monster *monster,
						   const struct object *missile,
						   const struct object *launcher,
						   u32b *msg_type)
{
	int debuff_to_hit = is_debuffed(monster) ? DEBUFF_CRITICAL_HIT : 0;
	int power = chance_of_missile_hit_base(p, missile, launcher)
		+ debuff_to_hit;
	int add_dice = 0;

	/* Thrown weapons get lots of critical hits. */
	if (!launcher) {
		power = power * 3 / 2;
	}

	/* Test for critical hit - chance power / (power + 360) */
	if (randint1(power + 360) <= power) {
		/* Determine level of critical hit. */
		if (one_in_(50)) {
			*msg_type = MSG_HIT_SUPERB;
			add_dice = 3;
		} else if (one_in_(10)) {
			*msg_type = MSG_HIT_GREAT;
			add_dice = 2;
		} else {
			*msg_type = MSG_HIT_GOOD;
			add_dice = 1;
		}
	} else {
		*msg_type = MSG_SHOOT_HIT;
	}

	return add_dice;
}

/**
 * Determine damage for critical hits from melee.
 *
 * Factor in weapon weight, total plusses, player level.
 */
static int critical_melee(const struct player *p,
		struct monster *monster,
		const struct object *obj,
		int weight, int plus,
		int dam, u32b *msg_type)
{
	int debuff_to_hit = is_debuffed(monster) ? DEBUFF_CRITICAL_HIT : 0;
	int power = (weight / 30) + randint1(650);
	int chance = (weight / 30) + (p->state.to_h + plus + debuff_to_hit) * 5;
	if (obj)
		chance += p->state.skills[SKILL_TO_HIT_MELEE];
	else {
		/* Note that currently this path won't be used as this fn is only called with an object */
		chance += p->state.skills[SKILL_TO_HIT_MARTIAL];
	}
	chance -= 60;
	int new_dam = dam;

	if (randint1(5000) > chance) {
		*msg_type = MSG_HIT;
	} else if (power < 400) {
		*msg_type = MSG_HIT_GOOD;
		new_dam = 2 * dam + 5;
	} else if (power < 700) {
		*msg_type = MSG_HIT_GREAT;
		new_dam = 2 * dam + 10;
	} else if (power < 900) {
		*msg_type = MSG_HIT_SUPERB;
		new_dam = 3 * dam + 15;
	} else if (power < 1300) {
		*msg_type = MSG_HIT_HI_GREAT;
		new_dam = 3 * dam + 20;
	} else {
		*msg_type = MSG_HIT_HI_SUPERB;
		new_dam = 4 * dam + 20;
	}

	return new_dam;
}

/**
 * Determine O-combat damage for critical hits from melee.
 *
 * Factor in weapon weight, total plusses, player level.
 */
static int o_critical_melee(const struct player *p,
							const struct monster *monster,
							const struct object *obj, u32b *msg_type)
{
	int debuff_to_hit = is_debuffed(monster) ? DEBUFF_CRITICAL_HIT : 0;
	int power = (chance_of_melee_hit_base(p, obj) + debuff_to_hit) / 3;
	int add_dice = 0;

	/* Test for critical hit - chance power / (power + 240) */
	if (randint1(power + 240) <= power) {
		/* Determine level of critical hit. */
		if (one_in_(40)) {
			*msg_type = MSG_HIT_HI_SUPERB;
			add_dice = 5;
		} else if (one_in_(12)) {
			*msg_type = MSG_HIT_HI_GREAT;
			add_dice = 4;
		} else if (one_in_(3)) {
			*msg_type = MSG_HIT_SUPERB;
			add_dice = 3;
		} else if (one_in_(2)) {
			*msg_type = MSG_HIT_GREAT;
			add_dice = 2;
		} else {
			*msg_type = MSG_HIT_GOOD;
			add_dice = 1;
		}
	} else {
		*msg_type = MSG_HIT;
	}

	return add_dice;
}

/**
 * Determine standard melee damage.
 *
 * Factor in damage dice, to-dam and any brand or slay (whichever is higher).
 * If deflate is >1, divide the base damage only (not the additional damage from slays/brands) by that factor.
 */
static int melee_damage(const struct monster *mon, struct object *obj, int b, int s, int deflate)
{
	int dmg = (obj) ? damroll(obj->dd, obj->ds) : 1;
	int base_dmg = dmg;

	int slaymul = 1;
	if (s)
		slaymul = slays[s].multiplier;
	int brandmul = 1;
	if (b)
		brandmul = get_monster_brand_multiplier(mon, &brands[b], false);
	dmg *= MAX(slaymul, brandmul);

	dmg -= base_dmg;
	if (obj) base_dmg += obj->to_d;
	if (deflate > 1) {
		base_dmg = (base_dmg + deflate - 1) / deflate;
	}

	return dmg + base_dmg;
}

/**
 * Determine O-combat average melee damage.
 * This does not handle the case of both slay and brand being present.
 *
 * Deadliness and any brand or slay add extra sides to the damage dice,
 * criticals add extra dice.
 */
static int o_melee_average_damage(struct player *p, const struct monster *mon,
		struct object *obj, int b, int s, int deflate)
{
	int dice = (obj) ? obj->dd : 1;
	int sides, dmg, add = 0;

	/* Get the average value of a single damage die. (x10) */
	int die_average = (10 * (((obj) ? obj->ds : 1) + 1)) / 2;

	/* Adjust the average for slays and brands. (10x inflation) */
	if (s) {
		die_average *= slays[s].o_multiplier;
		add = slays[s].o_multiplier - 10;
	} else if (b) {
		die_average *= get_monster_brand_multiplier(mon, &brands[b], false);
	}

	/* Apply deadliness to average. (100x inflation) */
	apply_deadliness(&die_average,
			MIN(((obj) ? obj->to_d : 0) + p->state.to_d, 150));

	/* Calculate the actual number of sides to each die (x10000). */
	sides = (2 * die_average) - 10000;

	/* Get number of critical dice */
	double extra_dice = 0.0;
	if ((obj) && (deflate <= 1))
		extra_dice = o_calculate_melee_crits(p->state, obj) * 0.01;

	/* Average damage. */
	dmg = ((dice + extra_dice) * (sides + 1.0)) / 2.0;

	/* Reduce if deflating */
	if (deflate > 1)
		dmg = (dmg + deflate + 1) / deflate;

	/* Apply any special additions to damage. */
	dmg += add;

	return dmg;
}


/**
 * Determine O-combat melee damage.
 *
 * Deadliness and any brand or slay add extra sides to the damage dice,
 * criticals add extra dice.
 */
static int o_melee_damage(struct player *p, const struct monster *mon,
		struct object *obj, int b, int s, u32b *msg_type, int deflate)
{
	int dice = obj->dd;
	int sides, dmg, add = 0;
	bool extra;

	/* Get the average value of a single damage die. (x10) */
	int die_average = (10 * (obj->ds + 1)) / 2;

	/* Determine whether to use slay or brand, if both are present */
	if (s && b) {
		int slay = o_melee_average_damage(p, mon, obj, 0, s, deflate);
		int brand = o_melee_average_damage(p, mon, obj, b, 0, deflate);
		if (slay > brand)
			b = 0;
		else
			s = 0;
	}

	/* Adjust the average for slays and brands. (10x inflation) */
	if (s) {
		die_average *= slays[s].o_multiplier;
		add = slays[s].o_multiplier - 10;
	} else if (b) {
		int bmult = get_monster_brand_multiplier(mon, &brands[b], true);

		die_average *= bmult;
		add = bmult - 10;
	} else {
		die_average *= 10;
	}

	/* Apply deadliness to average. (100x inflation) */
	apply_deadliness(&die_average, MIN(obj->to_d + p->state.to_d, 150));

	/* Calculate the actual number of sides to each die. */
	sides = (2 * die_average) - 10000;
	extra = randint0(10000) < (sides % 10000);
	sides /= 10000;
	sides += (extra ? 1 : 0);

	/* Get number of critical dice */
	if (deflate <= 1)
		dice += o_critical_melee(p, mon, obj, msg_type);

	/* Roll out the damage. */
	dmg = damroll(dice, sides);

	/* Reduce if deflating */
	if (deflate > 1)
		dmg = (dmg + deflate + 1) / deflate;

	/* Apply any special additions to damage. */
	dmg += add;

	return dmg;
}

/**
 * Determine standard ranged damage.
 *
 * Factor in damage dice, to-dam, multiplier and any brand or slay.
 */
static int ranged_damage(struct player *p, const struct monster *mon,
						 struct object *missile, struct object *launcher,
						 int b, int s)
{
	int dmg;
	int mult = (launcher ? p->state.ammo_mult : 1);

	/* If we have a slay or brand, modify the multiplier appropriately */
	if (b && mon) {
		mult += get_monster_brand_multiplier(mon, &brands[b], false);
	} else if (s) {
		mult += slays[s].multiplier;
	}

	/* Apply damage: multiplier, slays, bonuses */
	dmg = damroll(missile->dd, missile->ds);
	dmg += missile->to_d;
	if (launcher) {
		dmg += launcher->to_d;
	} else if (of_has(missile->flags, OF_THROWING)) {
		/* Adjust damage for throwing weapons.
		 * This is not the prettiest equation, but it does at least try to
		 * keep throwing weapons competitive. */
		dmg *= 2 + missile->weight / 540;
	}
	dmg *= mult;

	return dmg;
}

/**
 * Determine O-combat ranged damage.
 *
 * Deadliness, launcher multiplier and any brand or slay add extra sides to the
 * damage dice, criticals add extra dice.
 */
static int o_ranged_damage(struct player *p, const struct monster *mon,
						   struct object *missile, struct object *launcher,
						   int b, int s, u32b *msg_type)
{
	int mult = (launcher ? p->state.ammo_mult : 1);
	int dice = missile->dd;
	int sides, deadliness, dmg, add = 0;
	bool extra;

	/* Get the average value of a single damage die. (x10) */
	int die_average = (10 * (missile->ds + 1)) / 2;

	/* Apply the launcher multiplier. */
	die_average *= mult;

	/* Adjust the average for slays and brands. (10x inflation) */
	if (b) {
		int bmult = get_monster_brand_multiplier(mon, &brands[b], true);

		die_average *= bmult;
		add = bmult - 10;
	} else if (s) {
		die_average *= slays[s].o_multiplier;
		add = slays[s].o_multiplier - 10;
	} else {
		die_average *= 10;
	}

	/* Apply deadliness to average. (100x inflation) */
	if (launcher) {
		deadliness = missile->to_d + launcher->to_d + p->state.to_d;
	} else if (of_has(missile->flags, OF_THROWING)) {
		deadliness = missile->to_d + p->state.to_d;
	} else {
		deadliness = missile->to_d;
	}
	apply_deadliness(&die_average, MIN(deadliness, 150));

	/* Calculate the actual number of sides to each die. */
	sides = (2 * die_average) - 10000;
	extra = randint0(10000) < (sides % 10000);
	sides /= 10000;
	sides += (extra ? 1 : 0);

	/* Get number of critical dice - only for suitable objects */
	if (launcher) {
		dice += o_critical_shot(p, mon, missile, launcher, msg_type);
	} else if (of_has(missile->flags, OF_THROWING)) {
		dice += o_critical_shot(p, mon, missile, NULL, msg_type);

		/* Multiply the number of damage dice by the throwing weapon
		 * multiplier.  This is not the prettiest equation,
		 * but it does at least try to keep throwing weapons competitive. */
		dice *= 2 + missile->weight / 540;
	}

	/* Roll out the damage. */
	dmg = damroll(dice, sides);

	/* Apply any special additions to damage. */
	dmg += add;

	return dmg;
}

/**
 * Apply the player damage bonuses
 */
static int player_damage_bonus(struct player_state *state)
{
	return state->to_d;
}

/**
 * ------------------------------------------------------------------------
 * Non-damage melee blow effects
 * ------------------------------------------------------------------------ */
/**
 * Apply blow side effects
 */
static void blow_side_effects(struct player *p, struct monster *mon)
{
	/* Confusion attack */
	if (p->timed[TMD_ATT_CONF]) {
		player_clear_timed(p, TMD_ATT_CONF, true);

		mon_inc_timed(mon, MON_TMD_CONF, (10 + randint0(p->lev) / 10),
					  MON_TMD_FLG_NOTIFY);
	}
}

/**
 * Apply blow after effects
 */
static bool blow_after_effects(struct loc grid, int dmg, int splash,
							   bool *fear, bool quake)
{
	/* Apply earthquake brand */
	if (quake) {
		effect_simple(EF_EARTHQUAKE, source_player(), "0", 0, 10, 0, 0, 0,
					  NULL);

		/* Monster may be dead or moved */
		if (!square_monster(cave, grid))
			return true;
	}

	return false;
}

/**
 * ------------------------------------------------------------------------
 * Melee attack
 * ------------------------------------------------------------------------ */
/* Melee and throwing hit types */
static const struct hit_types melee_hit_types[] = {
	{ MSG_MISS, NULL },
	{ MSG_HIT, NULL },
	{ MSG_HIT_GOOD, "It was a good hit!" },
	{ MSG_HIT_GREAT, "It was a great hit!" },
	{ MSG_HIT_SUPERB, "It was a superb hit!" },
	{ MSG_HIT_HI_GREAT, "It was a *GREAT* hit!" },
	{ MSG_HIT_HI_SUPERB, "It was a *SUPERB* hit!" },
};

static bool py_attack_hit(struct player *p, struct loc grid, struct monster *mon, int dmg, char *verb, int verbsize, const char *after, u32b msg_type, int splash, bool do_quake, bool *fear, struct object *obj)
{
	int drain = 0;
	bool stop = false;
	char m_name[80];

	if (!after)
		after = ".";

	/* Extract monster name (or "it") */
	monster_desc(m_name, sizeof(m_name), mon, MDESC_TARG);

	/* Apply the player damage bonuses */
	if (!OPT(p, birth_percent_damage)) {
		dmg += player_damage_bonus(&p->state);
	}

	/* Substitute shape-specific blows for shapechanged players */
	if (player_is_shapechanged(p)) {
		int choice = randint0(p->shape->num_blows);
		struct player_blow *blow = p->shape->blows;
		while (choice--) {
			blow = blow->next;
		}
		my_strcpy(verb, blow->name, verbsize);
		obj = NULL;
	}

	/* No negative damage; change verb if no damage done */
	if (dmg <= 0) {
		dmg = 0;
		msg_type = MSG_MISS;
		my_strcpy(verb, "fail to harm", verbsize);
	}

	for (int i = 0; i < (int)N_ELEMENTS(melee_hit_types); i++) {
		const char *dmg_text = "";

		if (msg_type != melee_hit_types[i].msg_type)
			continue;

		if (OPT(p, show_damage))
			dmg_text = format(" (%d)", dmg);

		if (melee_hit_types[i].text)
			msgt(msg_type, "You %s %s%s. %s", verb, m_name, dmg_text,
					melee_hit_types[i].text);
		else
			msgt(msg_type, "You %s %s%s.", verb, m_name, dmg_text);
	}

	/* Pre-damage side effects */
	blow_side_effects(p, mon);

	/* Damage, check for hp drain, fear and death */
	drain = MIN(mon->hp, dmg);
	bool funny = false;
	if ((obj) && (kf_has(obj->kind->kind_flags, KF_LOL)))
		funny = true;
	stop = do_mon_take_hit(mon, p, dmg, fear, NULL, funny);

	/* Small chance of bloodlust side-effects */
	if (p->timed[TMD_BLOODLUST] && one_in_(50)) {
		msg("You feel something give way!");
		player_over_exert(p, PY_EXERT_CON, 20, 0);
	}

	if (!stop) {
		if (p->timed[TMD_ATT_VAMP] && monster_is_living(mon)) {
			effect_simple(EF_HEAL_HP, source_player(), format("%d", drain),
						  0, 0, 0, 0, 0, NULL);
		}
	}

	if (stop && fear)
		(*fear) = false;

	/* Post-damage effects */
	if (blow_after_effects(grid, dmg, splash, fear, do_quake))
		stop = true;

	return stop;
}

static double py_unarmed_crit_power(struct player *p, struct monster_race *monrace, int wlev, int random)
{
	/* Determine clumsy armor factor
	 * This is related to effective_ac_of(), though different enough to not want to use it directly.
	 * In particular it is independent of level.
	 **/
	int eff_ac = 0;
	int max_ac = 0;
	for (int i = 0; i < p->body.count; i++) {
		struct object *armor = slot_object(p, i);
		int eff = 0;
		int armweight = armor ? armor->weight : 0;
		int weight = slot_table[i].weight;
		weight -= armweight;
		if (weight > 0) {
			eff = (weight * slot_table[i].ac) / slot_table[i].weight;
		}
		if (armor && armor->to_h < 0)
			eff >>= -armor->to_h;
		eff_ac += eff;
		max_ac += slot_table[i].ac;
	}
	/* Max_ac is now the most AC bonus that it is possible to get, while eff_ac is the amount that you
	 * would have at level 50.
	 * With eff_ac at 0 you will get few criticals (effectively dividing your level by 5), at max_ac the most.
	 * Eff_lev is scaled from 1 to 10000.
	 */
	int eff_lev = MAX((wlev * 40), (wlev * 200 * eff_ac) / (MAX (1, max_ac)));
	/* How many, and how powerful the crits are depends on eff_lev and monster AC / level.
	 * You can get your maximum crit against any monster, though - just not so often.
	 */
	int mon_ac, mon_lev;
	if (monrace) {
		mon_ac = monrace->ac;
		mon_lev = monrace->level;
	} else {
		/* If no monster is gives, estimate a 'typical' monster of your max depth */
		mon_lev = p->max_depth;
		mon_ac = mon_lev;
	}
	int mon_def = MAX (eff_lev + (mon_ac * 100) + (mon_lev * 40) + 2500, eff_lev * 2);
	/* Calculate at random, or find the average */
	if (random == RANDOMISE) {
		int power = 0;
		while (randint0(mon_def) < eff_lev)
			power++;
		return power;
	} else {
		double power = 0.0;
		double prop = (double)eff_lev / (double)mon_def;
		double add = prop;
		do {
			power += add;
			add *= prop;
		} while (add > 1e-6);
		return power;
	}
}

/**
 * Convert power to damage
 */

static double py_power_damage(double power)
{
	double dmg = 1.0 + player->known_state.to_d;
	dmg = ((2.0 + power) * dmg) / 2.0;
	return dmg;
}

/**
 * Determine average damage done unarmed (on a successful hit)
 */
double py_unarmed_damage(struct player *p)
{
	double dmg = 1.0;
	const int wlev = levels_in_class(get_class_by_name("Wrestler")->cidx);
	if (wlev) {
		dmg = py_power_damage(py_unarmed_crit_power(p, NULL, wlev, AVERAGE));
	}
	return dmg;
}

/**
 * Attack the monster at the given location with a single blow.
 */
bool py_attack_real(struct player *p, struct loc grid, bool *fear, bool *hit)
{
	/* Information about the target of the attack */
	struct monster *mon = square_monster(cave, grid);
	struct monster_lore *lore = get_lore(mon->race);
	char m_name[80];

	/* The weapon used */
	struct object *obj = equipped_item_by_slot_name(p, "weapon");

	/* Information about the attack */
	int splash = 0;
	bool do_quake = false;
	bool success = false;
	bool ko = false;

	char verb[32];
	char after[64];
	strcpy(after, ".");
	u32b msg_type = MSG_HIT;
	int j, b, s, weight, dmg;

	/* Default to punching */
	my_strcpy(verb, "punch", sizeof(verb));

	/* Extract monster name (or "it") */
	monster_desc(m_name, sizeof(m_name), mon, MDESC_TARG);

	/* Auto-Recall and track if possible and visible */
	if (monster_is_visible(mon)) {
		monster_race_track(p->upkeep, mon->race);
		health_track(p->upkeep, mon);
	}

	/* Handle player fear (only for invisible monsters) */
	if (player_of_has(p, OF_AFRAID)) {
		equip_learn_flag(p, OF_AFRAID);
		msgt(MSG_AFRAID, "You are too afraid to attack %s!", m_name);
		return false;
	}

	/* Disturb the monster */
	monster_wake(mon, false, 100);

	/* See if the player hit */
	success = test_hit(chance_of_melee_hit(p, obj, mon), mon->race->ac);

	/* Handle EVASIVE monsters.
	 * This is a second chance to avoid the hit. Unlike AC it only affects
	 * melee and isn't affected by your + to hit - only relative speed.
	 * Chance of fail is 35% at equal speed, maximum 90%.
	 */
	if ((success) && (rf_has(mon->race->flags, RF_EVASIVE))) {
		/* Chance of failure */
		int diff_speed = 35 + mon->race->speed - player->state.speed;
		if (diff_speed > 90)
			diff_speed = 90;
		if (randint0(100) < diff_speed) {
			/* Failure */
			monster_desc(m_name, sizeof(m_name), mon, MDESC_CAPITAL | MDESC_HIDE | (monster_is_visible(mon) ? MDESC_PRO_VIS : MDESC_PRO_HID));
			msgt(MSG_MISS, "%s evades your blow.", m_name);
			lore_learn_flag_if_visible(lore, mon, RF_EVASIVE);
			return false;
		}
	}

	/* If a miss, skip this hit */
	else if (!success) {
		msgt(MSG_MISS, "You miss %s.", m_name);

		/* Small chance of bloodlust side-effects */
		if (p->timed[TMD_BLOODLUST] && one_in_(50)) {
			msg("You feel strange...");
			player_over_exert(p, PY_EXERT_SCRAMBLE, 20, 20);
		}

		return false;
	}

	if (hit) *hit = true;

	if (obj) {
		weight = obj->weight;

		/* Handle normal weapon */
		my_strcpy(verb, "hit", sizeof(verb));
	} else {
		weight = 0;
	}

	/* Best attack from all slays or brands on all non-launcher equipment */
	b = 0;
	s = 0;
	for (j = 2; j < p->body.count; j++) {
		struct object *obj_local = slot_object(p, j);
		if (obj_local)
			improve_attack_modifier(p, obj_local, mon,
				&b, &s, verb, false);
	}

	/* Get the best attack from all slays or brands - weapon or temporary */
	improve_attack_modifier(p, obj, mon, &b, &s, verb, false);
	improve_attack_modifier(p, NULL, mon, &b, &s, verb, false);

	int deflate = 0;
	if (obj) {
		/* Handle resistance to edged / blunt weapons.
		 * All objects are either pointy or blunt, and anything that isn't TV_BLUNT
		 * is considered pointy. (This only makes sense because most items can't be
		 * wielded.)
		 * If the monster has IM_EDGED and the weapon is pointy, or if the monster
		 * has IM_BLUNT and the weapon is blunt, deflate damage to 1/4 and prevent
		 * critical hits.
		 * Either way, it applies to base and + to damage, but not to the extra damage
		 * gained from brands and slays or to bonuses from strength, etc.
		 */
		bool is_blunt = tval_is_blunt(obj);
		if (((rf_has(mon->race->flags, RF_IM_EDGED) && !is_blunt)) ||
			((rf_has(mon->race->flags, RF_IM_BLUNT) && is_blunt))) {
			deflate = 4;
			my_strcpy(verb, "weakly hit", sizeof(verb));
			if (rf_has(mon->race->flags, RF_IM_EDGED))
				lore_learn_flag_if_visible(lore, mon, RF_IM_EDGED);
			if (rf_has(mon->race->flags, RF_IM_BLUNT))
				lore_learn_flag_if_visible(lore, mon, RF_IM_BLUNT);
		}
	}

	/* Get the damage */
	if (!OPT(p, birth_percent_damage)) {
		dmg = melee_damage(mon, obj, b, s, deflate);
		if (obj && !deflate)
			dmg = critical_melee(p, mon, obj, weight, obj->to_h, dmg, &msg_type);
		else {
			/* Handle unarmed melee.
			 * Only Wrestlers can get critical hits - the chance depends on armor weight,
			 * level, the monster's AC.
			 * A critical means more damage, a more emphatic message and a chance of
			 * debuffing the monster.
			 **/
			const int wlev = levels_in_class(get_class_by_name("Wrestler")->cidx);
			if (wlev) {
				int power = py_unarmed_crit_power(p, mon->race, wlev, RANDOMISE);

				/* Power is now the type of critical - 0 is not a crit and will stop here, higher levels
				 * are increasingly rare and powerful crits.
				 */
				if (power) {
					strcpy(after, "!");
					/* Extract a verb */
					const char *ouch = "bug";
					switch(power) {
						case 1:
							ouch = one_in_(2) ? "smack" : "whack";
							break;
						case 2:
							ouch = one_in_(2) ? "strike" : "slam";
							break;
						case 3:
							ouch = "slug";
							break;
						case 4:
							ouch = "pound";
							break;
						default:
							ouch = "hammer";
							break;
					}
					/* Produce an increased damage */
					dmg = ((2 + power) * dmg) / 2;
					/* Add statuses: stun, etc. on critical unarmed melee hits */
					if ((wlev) && (!obj)) {
						if (randint0(power + 4) < power) {
							if (mon_inc_timed(mon, MON_TMD_HOLD, 2 + randint0(power + 2), MON_TMD_FLG_NOMESSAGE)) {
								switch(randint0(4)) {
									case 0:
										ouch = "hold down";
										break;
									case 1:
										ouch = "pin down";
										break;
									case 2:
										ouch = "immobilize";
										break;
									case 3:
										ouch = "knock out";
										break;
								}
								ko = true;
							}
						}
						else if (randint0(power + 3) < power) {
							if (mon_inc_timed(mon, MON_TMD_SLOW, 2 + randint0(power + 3), MON_TMD_FLG_NOMESSAGE)) {
								strcpy(after, ". ");
								bool visible = monster_is_visible(mon);
								monster_desc(after + 2, sizeof(after) - 2, mon, MDESC_CAPITAL | MDESC_HIDE | (visible ? MDESC_PRO_VIS : MDESC_PRO_HID));
								strcat(after, " is slowed!");
							}
						}
						else if (randint0(power + 2) < power) {
							mon_inc_timed(mon, MON_TMD_STUN, 2 + randint0(power + 4), MON_TMD_FLG_NOMESSAGE);
							switch(randint0(2)) {
								case 0:
									ouch = "stun";
									break;
								case 1:
									strcpy(after, ". ");
									monster_desc(after + 2, sizeof(after) - 2, mon, MDESC_CAPITAL | MDESC_HIDE);
									strcat(after, " staggers back!");
									break;
							}
						}
					}
					my_strcpy(verb, ouch, sizeof(verb));
				}
			}
		}
	} else {
		dmg = o_melee_damage(p, mon, obj, b, s, &msg_type, deflate);
	}

	/* Splash damage and earthquakes */
	splash = (weight * dmg) / 100;
	if (player_of_has(p, OF_IMPACT) && dmg > 50) {
		do_quake = true;
		equip_learn_flag(p, OF_IMPACT);
	}

	/* Learn by use */
	equip_learn_on_melee_attack(p);
	learn_brand_slay_from_melee(p, obj, mon);

	/* Apply the player damage bonuses */
	if (!OPT(p, birth_percent_damage)) {
		dmg += player_damage_bonus(&p->state);
	}

	/* Substitute shape-specific blows for shapechanged players */
	if (player_is_shapechanged(p)) {
		int choice = randint0(p->shape->num_blows);
		struct player_blow *blow = p->shape->blows;
		while (choice--) {
			blow = blow->next;
		}
		my_strcpy(verb, blow->name, sizeof(verb));
	}

	/* No negative damage; change verb if no damage done */
	if (dmg <= 0) {
		dmg = 0;
		msg_type = MSG_MISS;
		my_strcpy(verb, "fail to harm", sizeof(verb));
	}

	for (unsigned i = 0; i < N_ELEMENTS(melee_hit_types); i++) {
		const char *dmg_text = "";

		if (msg_type != melee_hit_types[i].msg_type)
			continue;

		if (OPT(p, show_damage))
			dmg_text = format(" (%d)", dmg);

		if (melee_hit_types[i].text)
			msgt(msg_type, "You %s %s%s. %s", verb, m_name, dmg_text,
					melee_hit_types[i].text);
		else
			msgt(msg_type, "You %s %s%s.", verb, m_name, dmg_text);
	}

	/* Pre-damage side effects */
	blow_side_effects(p, mon);

	/* Learn by use */
	equip_learn_on_melee_attack(p);

	return py_attack_hit(p, grid, mon, dmg, verb, sizeof(verb), after, msg_type, splash, do_quake, fear, obj) || ko;
}

/* Skill for the current weapon */
int weapon_skill(struct player *p)
{
	struct object *weapon = equipped_item_by_slot_name(p, "weapon");
	return weapon ? p->state.skills[SKILL_TO_HIT_MELEE] : p->state.skills[SKILL_TO_HIT_MARTIAL];
}

/**
 * Attempt a shield bash; return true if the monster dies
 */
static bool attempt_shield_bash(struct player *p, struct monster *mon, bool *fear)
{
	struct object *weapon = equipped_item_by_slot_name(p, "weapon");
	struct object *shield = equipped_item_by_slot_name(p, "arm");
	int nblows = p->state.num_blows / 100;
	int bash_quality, bash_dam, energy_lost;

	/* Bashing chance depends on melee skill, DEX, and a level bonus. */
	int bash_chance = weapon_skill(p) / 8 +
		adj_dex_th[p->state.stat_ind[STAT_DEX]] / 2;

	/* No shield, no bash */
	if (!shield) return false;

	/* Monster is too pathetic, don't bother */
	if (mon->race->level < p->lev / 2) return false;

	/* Players bash more often when they see a real need: */
	if (!equipped_item_by_slot_name(p, "weapon")) {
		/* Unarmed... */
		bash_chance *= 4;
	} else if (weapon->dd * weapon->ds * nblows < shield->dd * shield->ds * 3) {
		/* ... or armed with a puny weapon */
		bash_chance *= 2;
	}

	/* Try to get in a shield bash. */
	if (bash_chance <= randint0(200 + mon->race->level)) {
		return false;
	}

	/* Calculate attack quality, a mix of momentum and accuracy. */
	bash_quality = weapon_skill(p) / 4 + p->wt / 360 +
		p->upkeep->total_weight / 3600 + shield->weight / 90;

	/* Calculate damage.  Big shields are deadly. */
	bash_dam = damroll(shield->dd, shield->ds);

	/* Multiply by quality and experience factors */
	bash_dam *= bash_quality / 40 + p->lev / 14;

	/* Strength bonus. */
	bash_dam += adj_str_td[p->state.stat_ind[STAT_STR]];

	/* Paranoia. */
	bash_dam = MIN(bash_dam, 125);

	if (OPT(p, show_damage)) {
		msgt(MSG_HIT, "You get in a shield bash! (%d)", bash_dam);
	} else {
		msgt(MSG_HIT, "You get in a shield bash!");
	}

	/* Encourage the player to keep wearing that heavy shield. */
	if (randint1(bash_dam) > 30 + randint1(bash_dam / 2)) {
		msgt(MSG_HIT_HI_SUPERB, "WHAMM!");
	}

	/* Damage, check for fear and death. */
	if (mon_take_hit(mon, p, bash_dam, fear, NULL)) return true;

	/* Stunning. */
	if (bash_quality + p->lev > randint1(200 + mon->race->level * 8)) {
		mon_inc_timed(mon, MON_TMD_STUN, randint0(p->lev / 5) + 4, 0);
	}

	/* Confusion. */
	if (bash_quality + p->lev > randint1(300 + mon->race->level * 12)) {
		mon_inc_timed(mon, MON_TMD_CONF, randint0(p->lev / 5) + 4, 0);
	}

	/* The player will sometimes stumble. */
	if (35 + adj_dex_th[p->state.stat_ind[STAT_DEX]] < randint1(60)) {
		energy_lost = randint1(50) + 25;
		/* Lose 26-75% of a turn due to stumbling after shield bash. */
		msgt(MSG_GENERIC, "You stumble!");
		p->upkeep->energy_use += energy_lost * z_info->move_energy / 100;
	}

	return false;
}

/**
 * Returns (in a buffer 'list' passed in by the caller of 'length' entries)
 * a list of all extra attacks available as pointers to struct attacks. The
 * number of entries is returned, and the list is terminated by a NULL pointer.
 */
int get_extra_attacks(struct attack **list, int length)
{
	int attacks = 0;

	for(int i=0; i<PF_MAX; i++) {
		if ((ability[i]) && (player_has(player, i))) {
			for(int j=0; j<ability[i]->nattacks; j++) {
				if (attacks < length-1)
					list[attacks++] = &(ability[i]->attacks[j]);
			}
		}
	}

	list[attacks] = NULL;
	return attacks;
}

/**
 * Attempt a 'extra' attack against the specified monster. Return true if the
 * attack killed the target.
 */
static bool py_attack_extra(struct player *p, struct loc grid, struct monster *mon, struct attack *att, bool *hit)
{
	char m_name[80];

	int chance = chance_of_melee_hit(p, NULL, mon);
	int dmg = randcalc(att->damage, 0, RANDOMISE);

	/* See if the player hit */
	bool success = test_hit(chance, mon->race->ac);

	/* Extract monster name (or "it") */
	monster_desc(m_name, sizeof(m_name), mon, MDESC_TARG);

	/* If a miss, skip this hit */
	if (!success) {
		msgt(MSG_MISS, "You miss %s.", m_name);
		return false;
	}

	if (hit) *hit = true;

	/* You hit.
	 * Extra attacks are not influenced by equipment slays, brands etc.
	 * They do however have an element.
	 **/
	if (att->element >= 0) {
		#define RF_0 0
		static const int immune[] = {
		#define ELEM(a, b, ...)  RF_##b,
		#include "list-elements.h"
		#undef ELEM
			RF_IM_ACID
		};
		static const int vulnerable[] = {
		#define ELEM(a, b, c)  RF_##c,
		#include "list-elements.h"
		#undef ELEM
			RF_IM_ACID
		};
		#undef RF_0
		int imm = immune[att->element];
		if (imm)
			if (rf_has(mon->race->flags, imm))
				dmg /= 6 + randint0(6);
		int vuln = vulnerable[att->element];
		if (vuln)
			if (rf_has(mon->race->flags, vuln))
				dmg = (dmg * rand_range(200, 500)) / 100;
	}
	const char *after = ".";
	if (dmg >= (randcalc(att->damage, 0, MAXIMISE) * 7) / 8)
		after = "!";
	char msg[32];
	strncpy(msg, att->msg, sizeof(msg));
	msg[sizeof(msg)-1] = 0;
	return py_attack_hit(p, grid, mon, dmg, att->msg, sizeof(msg), after, MSG_HIT, 0, false, NULL, NULL);
}

/**
 * Attack the monster at the given location
 *
 * We get blows until energy drops below that required for another blow, or
 * until the target monster dies. Each blow is handled by py_attack_real().
 * We don't allow @ to spend more than 1 turn's worth of energy,
 * to avoid slower monsters getting double moves.
 */
void py_attack(struct player *p, struct loc grid)
{
	int avail_energy = MIN(p->energy, z_info->move_energy);
	int blow_energy = 100 * z_info->move_energy / p->state.num_blows;
	bool slain = false, fear = false, hit = false;
	struct monster *mon = square_monster(cave, grid);

	/* Disturb the player */
	disturb(p);

	/* Initialize the energy used */
	p->upkeep->energy_use = 0;

	/* Player attempts a shield bash if they can, and if monster is visible
	 * and not too pathetic */
	if (player_has(p, PF_SHIELD_BASH) && monster_is_visible(mon)) {
		/* Monster may die */
		if (attempt_shield_bash(p, mon, &fear)) return;
	}

	/* Attack until the next attack would exceed energy available or
	 * a full turn or until the enemy dies. We limit energy use
	 * to avoid giving monsters a possible double move. */
	while (avail_energy - p->upkeep->energy_use >= blow_energy && !slain) {
		slain = py_attack_real(p, grid, &fear, &hit);
		p->upkeep->energy_use += blow_energy;
	}

	/* Extra attacks, from mutations, equipment etc.
	 * These are 'free' (take no energy).
	 * Still should skip if the target's dead.
	 */
	struct attack *att[256];
	int attacks = get_extra_attacks(att, 256);
	for(int i=0; i<attacks; i++) {
		if (slain)
			break;
		slain = py_attack_extra(p, grid, mon, att[i], &hit);
	}

	/* Passive attacks against you */
	if (hit && !slain) {
		struct monster_lore *lore = get_lore(mon->race);
		bool blinked = false;

		if (z_info->mon_passive_max && mon->race->passive[0].method) {
			msg("It retaliates!");
			/* Scan through all blows */
			for (int i = 0; i < z_info->mon_passive_max; i++) {
				/* Extract the passive blow information and make the attack */
				if (!make_attack_blow(mon, p, &mon->race->passive[i], &lore->passive[i], &blinked))
					break;
			}
		}
	}

	/* Hack - delay fear messages */
	if (!slain && fear && monster_is_visible(mon)) {
		add_monster_message(mon, MON_MSG_FLEE_IN_TERROR, true);
	}
}

/**
 * ------------------------------------------------------------------------
 * Ranged attacks
 * ------------------------------------------------------------------------ */
/* Shooting hit types */
static const struct hit_types ranged_hit_types[] = {
	{ MSG_MISS, NULL },
	{ MSG_SHOOT_HIT, NULL },
	{ MSG_HIT_GOOD, "It was a good hit!" },
	{ MSG_HIT_GREAT, "It was a great hit!" },
	{ MSG_HIT_SUPERB, "It was a superb hit!" }
};


/**
 * This is a helper function used by do_cmd_throw and do_cmd_fire.
 *
 * It abstracts out the projectile path, display code, identify and clean up
 * logic, while using the 'attack' parameter to do work particular to each
 * kind of attack.
 */
static void ranged_helper(struct command *cmd, struct player *p,	struct object *obj, int dir,
						  int range, int shots, ranged_attack attack,
						  const struct hit_types *hit_types, int num_types, const char *fire, bool destroy)
{
	int i, j;

	int path_n;
	struct loc path_g[256];

	/* Start at the player */
	struct loc grid = p->grid;

	/* Predict the "target" location */
	struct loc target = loc_sum(grid, loc(99 * ddx[dir], 99 * ddy[dir]));

	bool hit_target = false;
	bool none_left = false;
	bool breaks = false;

	struct object *missile;
	int pierce = 1;

	/* Check for target validity */
	if ((dir == DIR_TARGET) && target_okay()) {
		int taim;
		target_get(&target);
		taim = distance(grid, target);
		if (taim > range) {
			char msg[80];
			strnfmt(msg, sizeof(msg),
					"Target out of range by %d squares. %s anyway? ",
				taim - range, fire);
			if (!get_check(msg)) return;
		}
	}

	/* Sound */
	sound(MSG_SHOOT);

	/* Actually "fire" the object -- Take a partial turn */
	p->upkeep->energy_use = (z_info->move_energy * 10 / shots);

	/* Calculate the path */
	path_n = project_path(cave, path_g, range, grid, target, 0);

	/* With reflection */
	struct monster *mon = square_monster(cave, path_g[path_n-1]);
	if (mon && rf_has(mon->race->flags, RF_REFLECT)) {
		/* Boing!
		 * Return to sender, or deflect randomly.
		 */
		struct loc boing = bounce_target(grid, target);

		/* The additional path after bouncing. Allow it to hit anything. */
		path_n += project_path(cave, path_g + path_n, z_info->max_range, target,
								  boing, PROJECT_KILL | PROJECT_PLAY | PROJECT_SELF);
	}

	/* Calculate potential piercing */
	if (p->timed[TMD_POWERSHOT]) {
		pierce = p->state.ammo_mult;
	}

	/* Will it explode, or otherwise behave in a way that means there should be no "fails to harm" message? */
	bool destroyed = object_destroyed(obj, grid, true);

	/* Hack -- Handle stuff */
	handle_stuff(p);

	/* Project along the path */
	for (i = 0; i < path_n; ++i) {
		struct monster *mon = NULL;
		bool see = square_isseen(cave, path_g[i]);

		/* Stop before hitting walls */
		if (!(square_ispassable(cave, path_g[i])) &&
			!(square_isprojectable(cave, path_g[i])))
			break;

		/* Advance */
		grid = path_g[i];

		/* Tell the UI to display the missile */
		event_signal_missile(EVENT_MISSILE, obj, see, grid.y, grid.x);

		/* Try the attack on the monster at (x, y) if any */
		mon = square_monster(cave, path_g[i]);
		if ((mon) && (!rf_has(mon->race->flags, RF_REFLECT))) {
			char m_name[80];
			monster_desc(m_name, sizeof(m_name), mon, MDESC_OBJE);

			int visible = monster_is_obvious(mon);

			bool fear = false;
			const char *note_dies = monster_is_destroyed(mon) ? 
				" is destroyed." : " dies.";

			struct attack_result result = attack(cmd, p, obj, grid);
			breaks = result.breaks;
			int dmg = result.dmg;
			u32b msg_type = result.msg_type;
			char hit_verb[32];
			my_strcpy(hit_verb, result.hit_verb, sizeof(hit_verb));
			mem_free(result.hit_verb);

			if (result.success) {
				char o_name[80];

				hit_target = true;

				missile_learn_on_ranged_attack(p, obj);

				/* Learn by use for other equipped items */
				equip_learn_on_ranged_attack(p);

				/*
				 * Describe the object (have most up-to-date
				 * knowledge now).
				 */
				object_desc(o_name, sizeof(o_name), obj,
					ODESC_FULL | ODESC_SINGULAR, p);

				/* No negative damage; change verb if no damage done */
				if ((dmg <= 0) && (mon->race)) {
					dmg = 0;
					msg_type = MSG_MISS;
					my_strcpy(hit_verb, destroyed ? "hits the floor by" : "fails to harm", sizeof(hit_verb));
				}

				if (!visible) {
					/* Invisible monster */
					msgt(MSG_SHOOT_HIT, "The %s finds a mark.", o_name);
				} else {
					for (j = 0; j < num_types; j++) {
						const char *dmg_text = "";

						if (msg_type != hit_types[j].msg_type) {
							continue;
						}

						if (OPT(p, show_damage)) {
							dmg_text = format(" (%d)", dmg);
						}

						if (hit_types[j].text) {
							msgt(msg_type, "Your %s %s %s%s. %s", o_name, 
								 hit_verb, m_name, dmg_text, hit_types[j].text);
						} else {
							msgt(msg_type, "Your %s %s %s%s.", o_name, hit_verb,
								 m_name, dmg_text);
						}
					}

					/* Track this monster */
					if (monster_is_obvious(mon)) {
						monster_race_track(p->upkeep, mon->race);
						health_track(p->upkeep, mon);
					}
				}

				/* Hit the monster, check for death */
				if (mon->race) {
					if (!mon_take_hit(mon, p, dmg, &fear, note_dies)) {
						if (!destroyed)
							message_pain(mon, dmg);
						if (fear && monster_is_obvious(mon)) {
							add_monster_message(mon, MON_MSG_FLEE_IN_TERROR, true);
						}
					}
				}
			}
			/* Stop the missile, or reduce its piercing effect */
			pierce--;
			if (pierce) continue;
			else break;
		} else if (mon) {
			char o_name[80];
			/*
			 * Describe the object (have most up-to-date
			 * knowledge now).
			 */
			object_desc(o_name, sizeof(o_name), obj,
				ODESC_FULL | ODESC_SINGULAR, p);

			/* Reflection */
			msgt(MSG_SHOOT_HIT, "The %s bounces!", o_name);
		} else if (loc_eq(grid, player->grid)) {
			char o_name[80];
			/*
			 * Describe the object (have most up-to-date
			 * knowledge now).
			 */
			object_desc(o_name, sizeof(o_name), obj,
				ODESC_FULL | ODESC_SINGULAR, p);

			/* Oops */
			msgt(MSG_SHOOT_HIT, "The %s hits you!", o_name);
			struct attack_result result = attack(cmd, p, obj, grid);
			breaks = result.breaks;
			int dmg = result.dmg;
			take_hit(player, dmg, "reflection");
		}

		/* Stop if non-projectable but passable */
		if (!(square_isprojectable(cave, path_g[i]))) 
			break;
	}

	/* Get the missile */
	if (object_is_carried(p, obj)) {
		missile = gear_object_for_use(p, obj, 1, true, &none_left);
	} else {
		missile = floor_object_for_use(p, obj, 1, true, &none_left);
	}

	/* Terminate piercing */
	if (p->timed[TMD_POWERSHOT]) {
		player_clear_timed(p, TMD_POWERSHOT, true);
	}

	/* Drop (or break) near that location */
	int breakage = breakage_chance(missile, hit_target);
	if (breaks || destroy) breakage = 100;
	drop_near(cave, &missile, breakage, grid, true, false);
}


/**
 * Helper function used with ranged_helper by do_cmd_fire.
 */
static struct attack_result make_ranged_shot(struct command *cmd, struct player *p,
		struct object *ammo, struct loc grid)
{
	char *hit_verb = mem_alloc(20 * sizeof(char));
	struct attack_result result = {false, true, 0, 0, hit_verb};
	struct object *bow = equipped_item_by_slot_name(p, "shooting");
	struct monster *mon = square_monster(cave, grid);
	int chance = chance_of_missile_hit(p, ammo, bow, mon);
	int b = 0, s = 0;

	my_strcpy(hit_verb, "hits", 20);

	/* Did we hit it (penalize distance travelled) */
	if (!test_hit(chance, (mon ? mon->race->ac : player->state.ac)))
		return result;

	result.success = true;

	improve_attack_modifier(p, ammo, mon, &b, &s, result.hit_verb, true);
	improve_attack_modifier(p, bow, mon, &b, &s, result.hit_verb, true);

	if (!OPT(p, birth_percent_damage)) {
		result.dmg = ranged_damage(p, mon, ammo, bow, b, s);
		result.dmg = critical_shot(p, mon, ammo->weight, ammo->to_h,
								   result.dmg, &result.msg_type);
	} else {
		result.dmg = o_ranged_damage(p, mon, ammo, bow, b, s, &result.msg_type);
	}

	missile_learn_on_ranged_attack(p, bow);
	learn_brand_slay_from_launch(p, ammo, bow, mon);

	return result;
}

void thrown_explodes(struct command *cmd, struct object *obj, struct loc grid)
{
	struct effect *effect = object_effect(obj);
	if (effect) {
		target_fix();
		bool ident = false;

		/* Boost damage effects if skill > difficulty */
		int boost = MAX((player->state.skills[SKILL_TO_HIT_THROW] - player->lev) / 2, 0);

		effect_do(effect,
					source_object_at(obj, grid),
					obj,
					&ident,
					object_flavor_is_aware(obj),
					5,
					false,
					boost,
					cmd,
					0);
		target_release();
	}
}

/**
 * Helper function used with ranged_helper by do_cmd_throw.
 */
static struct attack_result make_ranged_throw(struct command *cmd, struct player *p,
	struct object *obj, struct loc grid)
{
	char *hit_verb = mem_alloc(20 * sizeof(char));
	struct attack_result result = {false, false, 0, 0, hit_verb};
	struct monster *mon = square_monster(cave, grid);
	int b = 0, s = 0;

	my_strcpy(hit_verb, "hits", 20);

	/* Direct effect for exploding things (which can't miss)
	 * The actual explosion occurs in drop_near.
	 **/
	if (of_has(obj->flags, OF_EXPLODE)) {
		result.dmg *= 3;
		if (obj->kind->effect) {
			return result;
		}
	}

	/* If we missed then we're done */
	if (!test_hit(chance_of_missile_hit(p, obj, NULL, mon), (mon ? mon->race->ac : p->state.ac)))
		return result;

	result.success = true;

	improve_attack_modifier(p, obj, mon, &b, &s, result.hit_verb, true);

	if (!OPT(p, birth_percent_damage)) {
		result.dmg = ranged_damage(p, mon, obj, NULL, b, s);
		result.dmg = critical_shot(p, mon, obj->weight, obj->to_h,
								   result.dmg, &result.msg_type);
	} else {
		result.dmg = o_ranged_damage(p, mon, obj, NULL, b, s, &result.msg_type);
	}

	learn_brand_slay_from_throw(p, obj, mon);

	return result;
}


/**
 * Help do_cmd_throw():  restrict which equipment can be thrown.
 */
static bool restrict_for_throwing(const struct object *obj)
{
	return !object_is_equipped(player->body, obj) ||
			(tval_is_melee_weapon(obj) && obj_can_takeoff(obj));
}


/**
 * Fire an object from the quiver, pack or floor at a target.
 */
void do_cmd_fire(struct command *cmd) {
	int dir;
	int range = MIN(6 + 2 * player->state.ammo_mult, z_info->max_range);
	int shots = player->state.num_shots;

	ranged_attack attack = make_ranged_shot;

	struct object *gun = equipped_item_by_slot_name(player, "shooting");
	struct object *obj;

	if (!player_get_resume_normal_shape(player, cmd)) {
		return;
	}

	/* Get arguments */
	if (cmd_get_item(cmd, "item", &obj,
			/* Prompt */ "Fire which ammunition?",
			/* Error  */ "You have no ammunition to fire.",
			/* Filter */ obj_can_fire,
			/* Choice */ USE_INVEN | USE_QUIVER | USE_FLOOR | QUIVER_TAGS)
		!= CMD_OK)
		return;

	if (cmd_get_target(cmd, "target", &dir) == CMD_OK)
		player_confuse_dir(player, &dir, false);
	else
		return;

	/* Require a usable launcher */
	if (!gun || !player->state.ammo_tval) {
		msg("You have nothing to fire with.");
		return;
	}

	/* Check the item being fired is usable by the player. */
	if (!item_is_available(obj)) {
		msg("That item is not within your reach.");
		return;
	}

	/* Check the ammo can be used with the launcher */
	if (obj->tval != player->state.ammo_tval) {
		msg("That ammo cannot be fired by your current weapon.");
		return;
	}

	ranged_helper(cmd, player, obj, dir, range, shots, attack, ranged_hit_types,
				  (int) N_ELEMENTS(ranged_hit_types), "Fire", true);
}


/**
 * Throw an object from the quiver, pack, floor, or, in limited circumstances,
 * the equipment.
 */
void do_cmd_throw(struct command *cmd) {
	int dir;
	int shots = 10;
	int str = adj_str_blow[player->state.stat_ind[STAT_STR]];
	ranged_attack attack = make_ranged_throw;

	int weight;
	int range;
	struct object *obj;

	if (!player_get_resume_normal_shape(player, cmd)) {
		return;
	}

	/*
	 * Get arguments.  Never default to showing the equipment as the first
	 * list (since throwing the equipped weapon leaves that slot empty will
	 * have to choose another source anyways).
	 */
	if (player->upkeep->command_wrk == USE_EQUIP)
		player->upkeep->command_wrk = USE_INVEN;
	if (cmd_get_item(cmd, "item", &obj,
			/* Prompt */ "Throw which item?",
			/* Error  */ "You have nothing to throw.",
			/* Filter */ restrict_for_throwing,
			/* Choice */ USE_EQUIP | USE_QUIVER | USE_INVEN | USE_FLOOR | SHOW_THROWING)
		!= CMD_OK)
		return;

	if (cmd_get_target(cmd, "target", &dir) == CMD_OK)
		player_confuse_dir(player, &dir, false);
	else
		return;

	if (object_is_equipped(player->body, obj)) {
		assert(obj_can_takeoff(obj) && tval_is_melee_weapon(obj));
		inven_takeoff(obj);
	}

	weight = MAX(obj->weight, 450);
	range = MIN(((str + 20) * 450) / weight, 10);

	ranged_helper(cmd, player, obj, dir, range, shots, attack, ranged_hit_types,
				  (int) N_ELEMENTS(ranged_hit_types), "Throw it", false);
}

/**
 * Front-end command which fires at the nearest target with default ammo.
 */
void do_cmd_fire_at_nearest(void) {
	int i, dir = DIR_TARGET;
	struct object *ammo = NULL;
	struct object *gun = equipped_item_by_slot_name(player, "shooting");

	/* Require a usable launcher */
	if (!gun || !player->state.ammo_tval) {
		msg("You have nothing to fire with.");
		return;
	}

	/* Find first eligible ammo in the quiver */
	for (i = 0; i < z_info->quiver_size; i++) {
		if (!player->upkeep->quiver[i])
			continue;
		if (player->upkeep->quiver[i]->tval != player->state.ammo_tval)
			continue;
		ammo = player->upkeep->quiver[i];
		break;
	}

	/* Require usable ammo */
	if (!ammo) {
		msg("You have no ammunition ready to fire.");
		return;
	}

	/* Require foe */
	if (!target_set_closest((TARGET_KILL | TARGET_QUIET), NULL)) return;

	/* Fire! */
	cmdq_push(CMD_FIRE);
	cmd_set_arg_item(cmdq_peek(), "item", ammo);
	cmd_set_arg_target(cmdq_peek(), "target", dir);
}
