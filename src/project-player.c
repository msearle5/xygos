/**
 * \file project-player.c
 * \brief projection effects on the player
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
#include "effects.h"
#include "init.h"
#include "mon-desc.h"
#include "mon-predicate.h"
#include "mon-util.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "source.h"
#include "trap.h"

/* Vulnerability: % extra damage from 1 or more levels of vulnerability */
byte *dam_inc_vuln;
int n_dam_inc_vuln;

/* Resistance: % damage reduction from 1 or more levels of resistance */
byte *dam_dec_resist;
int n_dam_dec_resist;

int resist_to_percent(int resist, int type)
{
	if (resist >= IMMUNITY)
		return 100;

	if (resist < 0) {
		resist = -resist;
		if (resist >= n_dam_inc_vuln)
			resist = n_dam_inc_vuln - 1;
		return -dam_inc_vuln[resist];
	}

	if (resist >= n_dam_dec_resist)
		resist = n_dam_dec_resist - 1;
	int percent = dam_dec_resist[resist];

	/* This is correct for an element, i.e. a projection with numerator = 1, denominator = 3.
	 * (Meaning that 2 steps - 66% - should reduce damage to 1/3.)
	 *  => (percent * denominator) / (numerator * 3) => (% * 3) / (1 * 3) => percent.
	 *
	 * Others may be scaled differently:
	 * e.g. sound: numerator:6, denominator:8+1d4
	 * This means that 2 steps should reduce damage to minimum 6/9, maximum 6/12, average 6/10.5.
	 * So sound should return (percent * 10.5) / (6 * 3)
	 * => (percent * (10.5, denominator)) / ((6, numerator) * 3) => (% * 10.5) / (6 * 3) => percent * (10.5 / 18) => 38.5%
	 */

	/* For accuracy, avoid AVERAGE and used the midpoint of minimum and maximum. */
	int min_denom = randcalc(projections[type].denominator, 0, MINIMISE);
	int max_denom = randcalc(projections[type].denominator, 0, MAXIMISE);

	/* Scaled by 100. 50 is because the wanted average is (min_denom+max_denom/2) */
	return (percent * 50 * (min_denom + max_denom)) / (300 * projections[type].numerator);
}

/**
 * Adjust damage according to resistance or vulnerability.
 *
 * \param p is the player
 * \param type is the attack type we are checking.
 * \param dam is the unadjusted damage.
 * \param dam_aspect is the calc we want (min, avg, max, random).
 * \param resist is the degree of resistance (-1 = vuln, 3 = immune).
 */
int adjust_dam(struct player *p, int type, int dam, aspect dam_aspect,
			   int resist, bool actual)
{
	int denom = 0;
	/* If an actual player exists, get their actual resist */
	if (p && p->race) {
		/* Ice is a special case */
		int res_type = (type == PROJ_ICE) ? PROJ_COLD: type;
		resist = res_type < ELEM_MAX ? p->state.el_info[res_type].res_level : 0;

		/* Notice element stuff */
		if (actual) {
			equip_learn_element(p, res_type);
		}
	}

	if (resist == IMMUNITY) /* immune */
		return 0;

	/* If armour is damaged, add one effective level of resistance */
	if (type == PROJ_ACID && p && minus_ac(p))
		resist++;
 
	/* Vulnerable - increase damage by a percentage taken from the dam_inc_vuln
	 * array. This doesn't care about denominator/divisor.
	 **/
	if (resist < 0) {
		resist = -resist;
		if (resist >= n_dam_inc_vuln)
			resist = n_dam_inc_vuln - 1;
		return (dam * dam_inc_vuln[resist]) / 100;
	}

	int percent = 0;
	/* Resistant; convert to a percentage (scaled for a 1/3 element) */
	if (n_dam_dec_resist) {
		if (resist >= n_dam_dec_resist)
			resist = n_dam_dec_resist - 1;
		percent = dam_dec_resist[resist];
	}

	/* Variable resists vary the denominator, so we need to invert the logic
	 * of dam_aspect. (m_bonus is unused) */
	if (resist > 0) {
		switch (dam_aspect) {
			case MINIMISE:
				denom = randcalc(projections[type].denominator, 0, MAXIMISE);
				break;
			case MAXIMISE:
				denom = randcalc(projections[type].denominator, 0, MINIMISE);
				break;
			case AVERAGE:
			case EXTREMIFY:
			case RANDOMISE:
				denom = randcalc(projections[type].denominator, 0, dam_aspect);
				break;
			default:
				assert(0);
		}

		if (denom > 0) {
			/* Scale by percentage and variable resist */
			dam = dam * projections[type].numerator * (100 - percent) * 3;
			dam = dam / (100 * denom);
		}
	}

	return dam;
}


/**
 * ------------------------------------------------------------------------
 * Player handlers
 * ------------------------------------------------------------------------ */

/**
 * Drain stats at random
 *
 * \param num is the number of points to drain
 */
static void project_player_drain_stats(int num)
{
	int i, k = 0;
	const char *act = NULL;

	for (i = 0; i < num; i++) {
		k = randint0(STAT_MAX);
		act = obj_properties[k+1].drain_adj;
		msg("You're not as %s as you used to be...", act);
		player_stat_dec(player, k, false);
	}

	return;
}

typedef struct project_player_handler_context_s {
	/* Input values */
	const struct source origin;
	const int r;
	const struct loc grid;
	int dam; /* May need adjustment */
	const int type;
	const int power;

	/* Return values */
	bool obvious;
} project_player_handler_context_t;

typedef int (*project_player_handler_f)(project_player_handler_context_t *);

static int project_player_handler_ACID(project_player_handler_context_t *context)
{
	if (player_is_immune(player, ELEM_ACID)) return 0;
	inven_damage(player, PROJ_ACID, MIN(context->dam * 5, 300));
	return 0;
}

static int project_player_handler_ELEC(project_player_handler_context_t *context)
{
	if (player_is_immune(player, ELEM_ELEC)) return 0;
	inven_damage(player, PROJ_ELEC, MIN(context->dam * 5, 300));
	return 0;
}

static int project_player_handler_FIRE(project_player_handler_context_t *context)
{
	if (player_is_immune(player, ELEM_FIRE)) return 0;
	inven_damage(player, PROJ_FIRE, MIN(context->dam * 5, 300));

	/* Occasional side-effects for powerful fire attacks */
	if (randint0(context->dam) > 500) {
		msg("The intense heat saps you.");
		effect_simple(EF_DRAIN_STAT, source_none(), "0", STAT_STR, 0, 0, 0,
					  0, &context->obvious);
	}
	else if (randint0(context->dam) > 200) {
		if (player_inc_timed(player, TMD_BLIND,
							 randint1(context->dam / 100), true, true)) {
			msg("Your eyes fill with smoke!");
		}
	}
	else if (randint0(context->dam) > 300) {
		if (player_inc_timed(player, TMD_POISONED,
							 randint1(context->dam / 10), true, true)) {
			msg("You are assailed by poisonous fumes!");
		}
	}
	return 0;
}

static int project_player_handler_COLD(project_player_handler_context_t *context)
{
	if (player_is_immune(player, ELEM_COLD)) return 0;
	inven_damage(player, PROJ_COLD, MIN(context->dam * 5, 300));

	/* Occasional side-effects for powerful cold attacks */
	if (randint0(context->dam) > 500) {
		msg("The cold seeps into your bones.");
		effect_simple(EF_DRAIN_STAT, source_none(), "0", STAT_DEX, 0, 0, 0,
					  0, &context->obvious);
	}
	else if (randint0(context->dam) > 500) {
		msg("You aren't thinking as clearly in this cold!");
		effect_simple(EF_DRAIN_STAT, source_none(), "0", STAT_INT, 0, 0, 0,
					0, &context->obvious);
	}
	return 0;
}

static int project_player_handler_POIS(project_player_handler_context_t *context)
{
	int xtra = 0;

	if (!player_inc_timed(player, TMD_POISONED, 10 + randint1(context->dam),
						  true, true))
		msg("You resist the effect!");

	/* Occasional side-effects for powerful poison attacks */
	if (randint0(context->dam) > 200) {
		if (!player_is_immune(player, ELEM_ACID)) {
			int dam = context->dam / 5;
			msg("The venom stings your skin!");
			inven_damage(player, PROJ_ACID, dam);
			xtra += adjust_dam(player, PROJ_ACID, dam, RANDOMISE,
							 player->state.el_info[PROJ_ACID].res_level,
							 true);
		}
	}
	else if (randint0(context->dam) > 200) {
		msg("The stench sickens you.");
		effect_simple(EF_DRAIN_STAT, source_none(), "0", STAT_CON, 0, 0, 0,
					  0, &context->obvious);
	}
	return xtra;
}

static int project_player_handler_LIGHT(project_player_handler_context_t *context)
{
	if (player_resists(player, ELEM_LIGHT)) {
		msg("You resist the effect!");
		return 0;
	}

	(void)player_inc_timed(player, TMD_BLIND, 2 + randint1(5), true, true);

	/* Confusion for strong unresisted light */
	if (context->dam - 25 > randint0(300)) {
		msg("You are dazzled!");
		(void)player_inc_timed(player, TMD_CONFUSED,
							   2 + randint1(context->dam / 100), true, true);
	}
	return 0;
}

static int project_player_handler_DARK(project_player_handler_context_t *context)
{
	if (player_resists(player, ELEM_DARK)) {
		msg("You resist the effect!");
		return 0;
	}

	(void)player_inc_timed(player, TMD_BLIND, 2 + randint1(5), true, true);

	/* Unresisted dark from powerful monsters is bad news */

	/* Life draining */
	if (randint0(context->dam) > 100) {
		if (player_of_has(player, OF_HOLD_LIFE)) {
			equip_learn_flag(player, OF_HOLD_LIFE);
		} else {
			int drain = context->dam;
			msg("You feel uncertain of yourself in the darkness.");
			player_exp_lose(player, drain, false);
		}
	}

	/* Slowing */
	else if (randint0(context->dam) > 200) {
		msg("You feel unsure of yourself in the darkness.");
		(void)player_inc_timed(player, TMD_SLOW, context->dam / 100, true,
							   false);
	}

	/* Amnesia */
	else if (randint0(context->dam) > 300) {
		msg("You feel unsure of yourself in the darkness.");
		(void)player_inc_timed(player, TMD_AMNESIA, context->dam / 100,
							   true, false);
	}

	return 0;
}

static int project_player_handler_SOUND(project_player_handler_context_t *context)
{
	if (player_resists(player, ELEM_SOUND)) {
		msg("You resist the effect!");
		return 0;
	}

	/* Stun */
	if (!player_of_has(player, OF_PROT_STUN)) {
		int duration = 5 + randint1(context->dam / 3);
		if (duration > 35) duration = 35;
		(void)player_inc_timed(player, TMD_STUN, duration, true, true);
	} else {
		equip_learn_flag(player, OF_PROT_STUN);
	}

	/* Confusion for strong unresisted sound */
	if (context->dam > 50 + randint0(300)) {
		msg("The noise disorients you.");
		(void)player_inc_timed(player, TMD_CONFUSED,
							   2 + randint1(context->dam / 100), true, true);
	}
	return 0;
}

static int project_player_handler_SHARD(project_player_handler_context_t *context)
{
	if (player_resists(player, ELEM_SHARD)) {
		msg("You resist the effect!");
		return 0;
	}

	/* Cuts */
	(void)player_inc_timed(player, TMD_CUT, randint1(context->dam), true,
						   false);
	return 0;
}

static int project_player_handler_NEXUS(project_player_handler_context_t *context)
{
	struct monster *mon = NULL;
	if (context->origin.what == SRC_MONSTER) {
		mon = cave_monster(cave, context->origin.which.monster);
	}

	if (player_resists(player, ELEM_NEXUS)) {
		msg("You resist the effect!");
		return 0;
	}

	/* Stat swap */
	if (randint0(100) < player->state.skills[SKILL_SAVE]) {
		msg("You avoid the effect!");
	} else {
		player_inc_timed(player, TMD_SCRAMBLE, randint0(20) + 20, true, true);
	}

	if (one_in_(3) && mon) { /* Teleport to */
		effect_simple(EF_TELEPORT_TO, context->origin, "0", 0, 0, 0,
					  mon->grid.y, mon->grid.x, NULL);
	} else if (one_in_(4)) { /* Teleport level */
		if (randint0(100) < player->state.skills[SKILL_SAVE]) {
			msg("You avoid the effect!");
			return 0;
		}
		effect_simple(EF_TELEPORT_LEVEL, context->origin, "0", 0, 0, 0, 0, 0,
					  NULL);
	} else { /* Teleport */
		const char *miles = "200";
		effect_simple(EF_TELEPORT, context->origin, miles, 1, 0, 0, 0, 0, NULL);
	}
	return 0;
}

static int project_player_handler_RADIATION(project_player_handler_context_t *context)
{
	int power = context->power;

	/* Radiation is difficult to be resistant to */
	switch (player->state.el_info[ELEM_RADIATION].res_level) {
		case 0:
			break;
		case 1:
			msg("You partially resist the effects.");
			power /= 2;
			break;
		case 2:
			msg("You resist the effects.");
			power /= 4;
			break;
		default:
			msg("You resist the effects very well.");
			power /= 8;
			break;
	}

	if (power < 1)
		power = 1;

	/* Radiation attacks cause a status increase */
	(void)player_inc_timed(player, TMD_RAD, rand_range((power / 4) + 1, power), true, true);

	return 0;
}

static int project_player_handler_CHAOS(project_player_handler_context_t *context)
{
	if (player_resists(player, ELEM_CHAOS)) {
		msg("You resist the effect!");
		return 0;
	}

	/* Hallucination */
	(void)player_inc_timed(player, TMD_IMAGE, randint1(10), true, false);

	/* Confusion */
	(void)player_inc_timed(player, TMD_CONFUSED, 10 + randint0(20), true, true);

	/* Life draining */
	if (!player_of_has(player, OF_HOLD_LIFE)) {
		int drain = ((player->exp * 3)/ (100 * 2)) * z_info->life_drain_percent;
		msg("You feel your memories draining away!");
		player_exp_lose(player, drain, false);
	} else {
		equip_learn_flag(player, OF_HOLD_LIFE);
	}
	return 0;
}

static int project_player_handler_DISEN(project_player_handler_context_t *context)
{
	if (player_resists(player, ELEM_DISEN)) {
		msg("You resist the effect!");
		return 0;
	}

	/* Disenchant gear */
	effect_simple(EF_DISENCHANT, context->origin, "0", 0, 0, 0, 0, 0, NULL);
	return 0;
}

static int project_player_handler_WATER(project_player_handler_context_t *context)
{
	/* Confusion */
	(void)player_inc_timed(player, TMD_CONFUSED, 5 + randint1(5), true, true);

	/* Stun */
	(void)player_inc_timed(player, TMD_STUN, randint1(40), true, true);
	return 0;
}

static int project_player_handler_ICE(project_player_handler_context_t *context)
{
	if (!player_is_immune(player, ELEM_COLD))
		inven_damage(player, PROJ_COLD, MIN(context->dam * 5, 300));

	/* Cuts */
	if (!player_resists(player, ELEM_SHARD))
		(void)player_inc_timed(player, TMD_CUT, damroll(5, 8), true, false);
	else
		msg("You resist the effect!");

	/* Stun */
	(void)player_inc_timed(player, TMD_STUN, randint1(15), true, true);
	return 0;
}

static int project_player_handler_GRAVITY(project_player_handler_context_t *context)
{
	msg("Gravity warps around you.");

	/* Blink */
	if (randint1(127) > player->lev) {
		const char *five = "5";
		effect_simple(EF_TELEPORT, context->origin, five, 1, 0, 0, 0, 0, NULL);
	}

	/* Slow */
	(void)player_inc_timed(player, TMD_SLOW, 4 + randint0(4), true, false);

	/* Stun */
	if (!player_of_has(player, OF_PROT_STUN)) {
		int duration = 5 + randint1(context->dam / 3);
		if (duration > 35) duration = 35;
		(void)player_inc_timed(player, TMD_STUN, duration, true, true);
	} else {
		equip_learn_flag(player, OF_PROT_STUN);
	}
	return 0;
}

static int project_player_handler_INERTIA(project_player_handler_context_t *context)
{
	/* Slow */
	(void)player_inc_timed(player, TMD_SLOW, 4 + randint0(4), true, false);
	return 0;
}

static int project_player_handler_FORCE(project_player_handler_context_t *context)
{
	struct loc centre = origin_get_loc(context->origin);

	/* Player gets pushed in a random direction if on the trap */
	if (context->origin.what == SRC_TRAP &&	loc_eq(player->grid, centre)) {
		int d = randint0(8);
		centre = loc_sum(centre, ddgrid_ddd[d]);
	}

	/* Stun */
	(void)player_inc_timed(player, TMD_STUN, randint1(20), true, true);

	/* Thrust player away. */
	thrust_away(centre, context->grid, 3 + context->dam / 20);
	return 0;
}

static int project_player_handler_TIME(project_player_handler_context_t *context)
{
	if (one_in_(2)) {
		/* Life draining */
		int drain = 100 + (player->exp / 100) * z_info->life_drain_percent;
		msg("You feel your memories draining away!");
		player_exp_lose(player, drain, false);
	} else if (!one_in_(5)) {
		/* Drain some stats */
		project_player_drain_stats(2);
	} else {
		/* Drain all stats */
		int i;
		msg("You're not as powerful as you used to be...");

		for (i = 0; i < STAT_MAX; i++)
			player_stat_dec(player, i, false);
	}
	return 0;
}

static int project_player_handler_PLASMA(project_player_handler_context_t *context)
{
	/* Stun */
	if (!player_of_has(player, OF_PROT_STUN)) {
		int duration = 5 + randint1(context->dam * 3 / 4);
		if (duration > 35) duration = 35;
		(void)player_inc_timed(player, TMD_STUN, duration, true, true);
	} else {
		equip_learn_flag(player, OF_PROT_STUN);
	}
	return 0;
}

static int project_player_handler_HALLU(project_player_handler_context_t *context)
{
	(void)player_inc_timed(player, TMD_IMAGE, 3 + context->dam, true, true);
	return 0;
}

static int project_player_handler_METEOR(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MISSILE(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_ARROW(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_LIGHT_WEAK(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_DARK_WEAK(project_player_handler_context_t *context)
{
	if (player_resists(player, ELEM_DARK)) {
		if (!player_has(player, PF_UNLIGHT)) {
			msg("You resist the effect!");
		}
		return 0;
	}

	(void)player_inc_timed(player, TMD_BLIND, 3 + randint1(5), true, true);
	return 0;
}

static int project_player_handler_KILL_WALL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_KILL_DOOR(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_KILL_TRAP(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MAKE_DOOR(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MAKE_TRAP(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_AWAY_EVIL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_AWAY_SPIRIT(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_AWAY_ALL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_TURN_EVIL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_TURN_LIVING(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_TURN_ALL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_DISP_EVIL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_DISP_ALL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_SLEEP_EVIL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_SLEEP_ALL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_CLONE(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_POLY(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_HEAL(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_SPEED(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_SLOW(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_LAG(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_CONF(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_HOLD(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_STUN(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_DRAIN(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_CRUSH(project_player_handler_context_t *context)
{
	return 0;
}

static int project_player_handler_MON_PAINT(project_player_handler_context_t *context)
{
	return 0;
}

static const project_player_handler_f player_handlers[] = {
	#define ELEM(a, ...) project_player_handler_##a,
	#include "list-elements.h"
	#undef ELEM
	#define PROJ(a) project_player_handler_##a,
	#include "list-projections.h"
	#undef PROJ
	NULL
};

/**
 * Called from project() to affect the player
 *
 * Called for projections with the PROJECT_PLAY flag set, which includes
 * bolt, beam, ball and breath effects.
 *
 * \param src is the origin of the effect
 * \param r is the distance from the centre of the effect
 * \param y the coordinates of the grid being handled
 * \param x the coordinates of the grid being handled
 * \param dam is the "damage" from the effect at distance r from the centre
 * \param typ is the projection (PROJ_) type
 * \return whether the effects were obvious
 *
 * If "r" is non-zero, then the blast was centered elsewhere; the damage
 * is reduced in project() before being passed in here.  This can happen if a
 * monster breathes at the player and hits a wall instead.
 *
 * We assume the player is aware of some effect, and always return "true".
 */
bool project_p(struct source origin, int r, struct loc grid, int dam, int typ,
			   int power, bool self)
{
	bool blind = (player->timed[TMD_BLIND] ? true : false);
	bool seen = !blind;
	bool obvious = true;

	/* Monster or trap name (for damage) */
	char killer[80];

	project_player_handler_f player_handler = player_handlers[typ];
	project_player_handler_context_t context = {
		origin,
		r,
		grid,
		dam,
		typ,
		power,
		obvious
	};
	int res_level = typ < ELEM_MAX ? player->state.el_info[typ].res_level : 0;

	/* Decoy has been hit */
	if (square_isdecoyed(cave, grid) && context.dam) {
		square_destroy_decoy(cave, grid);
	}

	/* No player here */
	if (!square_isplayer(cave, grid)) {
		return false;
	}

	switch (origin.what) {
		case SRC_PLAYER: {
			/* Don't affect projector unless explicitly allowed */
			if (!self) return false;
			strnfmt(killer, sizeof(killer), "yourself");
			break;
		}

		case SRC_MONSTER: {
			struct monster *mon = cave_monster(cave, origin.which.monster);

			/* Check it is visible */
			if (!monster_is_visible(mon))
				seen = false;

			/* Get the monster's real name */
			monster_desc(killer, sizeof(killer), mon, MDESC_DIED_FROM);

			/* Monster sees what is going on */
			update_smart_learn(mon, player, 0, 0, typ);

			break;
		}

		case SRC_TRAP: {
			struct trap *trap = origin.which.trap;

			/* Get the trap name */
			strnfmt(killer, sizeof(killer), "a %s", trap->kind->desc);

			break;
		}

		case SRC_OBJECT_AT:
		case SRC_OBJECT: {
			struct object *obj = origin.which.object;
			object_desc(killer, sizeof(killer), obj, ODESC_PREFIX | ODESC_BASE | ODESC_SINGULAR, player);
			break;
		}

		case SRC_CHEST_TRAP: {
			struct chest_trap *trap = origin.which.chest_trap;

			/* Get the trap name */
			strnfmt(killer, sizeof(killer), "%s", trap->msg_death);

			break;
		}

		case SRC_NONE: {
			/* Assume the caller has set the killer variable */
			break;
		}
	}

	/* Let player know what is going on */
	if (!seen) {
		msg("You are hit by %s!", projections[typ].blind_desc);
	}

	/* Adjust damage for resistance, immunity or vulnerability, and apply it */
	context.dam = adjust_dam(player,
							 typ,
							 context.dam,
							 RANDOMISE,
							 res_level,
							 true);
	if (context.dam) {
		take_hit(player, context.dam, killer);
	}

	/* Handle side effects, possibly including extra damage */
	if (player_handler != NULL && player->is_dead == false) {
		int xtra = player_handler(&context);
		if (xtra) take_hit(player, xtra, killer);
	}

	/* Disturb */
	disturb(player);

	/* Return "Anything seen?" */
	return context.obvious;
}
