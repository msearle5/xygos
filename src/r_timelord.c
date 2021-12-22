#include "effect-handler.h"
#include "game-input.h"
#include "message.h"
#include "player.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "savefile.h"
#include "z-util.h"

/* Persistent state for the Time-Lord race */
struct timelord_state {
	/* Total number of regenerations so far */
	uint32_t regenerations;
};

/* Load/save Time-Lord specific state */
static void timelord_loadsave(bool complete) {
	if (!complete) {
		if (player->race->state == NULL)
			player->race->state = mem_zalloc(sizeof(struct timelord_state));

		struct timelord_state *state = (struct timelord_state *)player->race->state;
		rdwr_u32b(&state->regenerations);
	}
}

/* Start a new character as an Time-Lord */
static void timelord_init(void)
{
	/* Initialise saved state */
	player->race->state = mem_zalloc(sizeof(struct timelord_state));
}

static const uint8_t regens[PY_MAX_LEVEL + 1] = {
	0,	0,	0,	0,	0,
	0,	0,	0,	0,	0,
	// 10
	1,	1,	1,	1,	1,
	2,	2,	2,	3,	3,
	// 20
	4,	4,	5,	5,	6,
	6,	7,	7,	8,	8,
	// 30
	9,	9,	10,	10,	10,
	11, 11, 11, 11, 11,
	// 40
	11, 11, 11, 11, 11,
	11, 11, 11, 11, 11,
	// 50
	12
};

static const char *once[] = {
	"", "only once", "only twice", "three times", "four times",
	"five times", "six times", "seven times", "eight times", "nine times",
	"ten times", "eleven times", "twelve times"
};

static void timelord_regen_status(void)
{
	struct player *p = player;

	player_inc_timed(p, TMD_STUN, damroll(2, 4), false, false);
	player_inc_timed(p, TMD_AMNESIA, damroll(3, 8), false, false);
	player_inc_timed(p, TMD_CONFUSED, damroll(3, 12), false, false);
	player_inc_timed(p, TMD_SCRAMBLE, damroll(3, 16), false, false);
}

bool get_regens(int32_t  *allowed, int32_t *used)
{
	if (player && player->race && player->race->name && player->race->state && streq(player->race->name, "Time-Lord")) {
		*allowed = regens[player->max_lev];
		*used = ((struct timelord_state *)player->race->state)->regenerations;
		return true;
	} else {
		*allowed = *used = 0;
		return false;
	}
}

void timelord_change_regenerations(int change)
{
	((struct timelord_state *)player->race->state)->regenerations += change;
}

/* Regenerate (on death or forced).
 * Cheat death? Return true in *success to avoid death.
 * First, calculate the number of regenerations allowed at your (max) level and determine
 * whether you have one left.
 * If so, use the max level to get a chance of survival. If you pass, re-roll the character
 * (height, weight, HP), scramble the stats, and put you in town in a weakened state (but not
 * so much that the denizens are much more dangerous to you - so remove all mobs?)
 **/
static void timelord_do_regen(bool *success, bool *tried)
{
	struct timelord_state *state = (struct timelord_state *)player->race->state;

	*tried = false;
	uint32_t allowed_regens = regens[player->max_lev];
	if (state->regenerations >= allowed_regens) {
		// explain
		if (allowed_regens == 0)
			msgt(MSG_DEATH, "Only Time Lords of level 10 or above may regenerate into a new form.");
		else
			msgt(MSG_DEATH, "Time Lords of your level may regenerate %s; you have used all your regenerations.", once);
		*success = false;
		return;
	}

	*tried = true;
	if (!*success) {
		/* 10% chance of success at level 10, 85% at level 50.
		 * Even if you used no regens before level 50, this is still only a 14.2% chance of surviving all
		 * 12 regenerations (pow(0.85, 12)).
		 * This is a chance per 10000 of success.
		 */
		int32_t chance = ((player->max_lev * 9375) / PY_MAX_LEVEL) - 875;
		if (randint0(10000) > chance) {
			// explain
			msgt(MSG_DEATH, "You attempt to regenerate into a new form, but are unsuccessful.");
			*success = false;
			return;
		}
	}

	/* Success */
	state->regenerations++;
	msgt(MSG_DEATH, "With no other option remaining, you must regenerate into a new form. You pass out...");

	struct player *p = player;

	/* Restore stats */
	for(int i = 0; i < STAT_MAX; i++)
		player->stat_cur[i] = player->stat_max[i];

	/* Switch some around permanently */
	for(int i = 0; i < damroll(4, 2); i++) {
		int loser = randint0(STAT_MAX);
		int winner = randint0(STAT_MAX);
		/* Only switch if the stats are different, the stat to decrease can be decreased (not
		 * at minimum) and the stat to increase can be (not maxed).
		 * As it is possible (and common) for all stats to be maxed, don't try to turn this
		 * into a while loop!
		 */
		if ((loser != winner) && (p->stat_max[loser] > 3) && (p->stat_max[winner] < 18 + 100)) {
			player_stat_dec(p, loser, true);
			player_stat_inc(p, winner);
		}
	}

	/* Get rid of statuses */
	for(int i = 0; i < TMD_MAX; i++)
		p->timed[i] = 0;

	/* Well fed */
	p->timed[TMD_FOOD] = PY_FOOD_FULL - 1;

	/* But confuse, etc. Not for long - this is more for appearance than to present a real threat */
	timelord_regen_status();

	/* Experience is drained, including a small permanent loss */
	player_exp_lose(p, p->max_exp / (40 + randint0(20)), true); // ~2%
	player_exp_lose(p, (p->exp * 10) / (40 + randint0(20)), false); // ~20%

	/* Reroll HP, and heal almost completely */
	roll_hp();
	p->chp = (p->mhp * (900 + damroll(2, 25))) / 1000; // 90~95%

	/* Change the ancillary information */
	get_height_weight(p);

	/* Move to the town, put on the stair */
	dungeon_change_level(player, 0);

	/* Recalc everything */
	player->upkeep->update |= (PU_BONUS | PU_HP | PU_PANEL);

	/* Saved! */
	*success = true;
	return;
}

/* Regenerate - on death
 **/
static void timelord_death(bool *success)
{
	bool dummy;
	timelord_do_regen(success, &dummy);
}

/* Regeneration - forced
 */
void timelord_force_regen(void)
{
	assert(streq(player->race->name, "Time-Lord"));

	/* First check (as this is risky) */
	if (!get_check("Are you sure you want to force regeneration? " ))
		return;

	/* Try to regen */
	bool success = false;
	bool tried = false;
	timelord_do_regen(&success, &tried);

	/* Failure => statuses.
	 * If you failed because of having no regenerations left, this doesn't happen
	 * (you didn't really start trying to do anything).
	 **/
	if (tried && !success) {
		struct timelord_state *state = (struct timelord_state *)player->race->state;
		timelord_regen_status();
		state->regenerations++;
	}
}

/*
 * Pre-effect hook.
 * Return true if the effect should stop before running the effect.
 */
bool timelord_effect(effect_handler_context_t *e)
{
	struct timelord_state *state = (struct timelord_state *)player->race->state;
	uint32_t allowed_regens = regens[player->max_lev];
	uint32_t remaining_regens = allowed_regens - state->regenerations;

	if ((e->effect == EF_BANISH) || (e->effect == EF_MASS_BANISH)) {
		/* Check that you really want to do this */
		if (!get_check(format("Extermination is anathema to Time-Lords, you risk %s - sure? ",
			(remaining_regens <= 0) ? "death" : "regeneration"))) {
			return true;
		}

		/* Maybe you got lucky */
		int luck = randint0(player->state.stat_ind[STAT_WIS] + player->max_lev); /* 1 to 87 */
		if (randint0((e->effect == EF_BANISH) ? 120 : 180) < luck) {
			msgt(MSG_DEATH, "With a great effort you repel the extermination field washing over you.");
			return false;
		}

		/* Maybe not. This is a guaranteed successful regeneration if you get this far
		 * and have regenerations left.
		 **/
		if (remaining_regens > 0) {
			msgt(MSG_DEATH, "The extermination field tears through you, and you begin to disperse...");
			bool success = true;
			bool tried = false;
			timelord_do_regen(&success, &tried);
		} else {
			msgt(MSG_DEATH, "The extermination field tears through you, and you disintegrate.");
			take_hit(player, FATAL_DAMAGE, "self-extermination");
		}
		return true;
	}

	return false;
}

static void timelord_levelup(int from, int to)
{
	struct timelord_state *state = (struct timelord_state *)player->race->state;
	int regen_old = regens[from];
	int regen_new = regens[to];
	if (regen_new > regen_old) {
		int avail_regens = regen_new - state->regenerations;
		const char *more = " more";
		const char *times = once[avail_regens];
		if (state->regenerations == 0)
			more = "";
		msg("You may now attempt (%s%s) to regenerate into a new form on death.", times, more);
	}
}

/* Install hooks */
void install_race_TIMELORD(void)
{
	struct player_race *r = get_race_by_name("Time-Lord");
	r->init = timelord_init;
	r->death = timelord_death;
	r->levelup = timelord_levelup;
	r->loadsave = timelord_loadsave;
	r->effect = timelord_effect;
}
