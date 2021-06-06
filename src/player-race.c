/**
 * \file player-race.c
 * \brief Player races, extensions, personalities
 *
 * Copyright (c) 2011 elly+angband@leptoquark.net. See COPYING.
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

#include "message.h"
#include "player.h"
#include "ui-input.h"
#include "z-util.h"

struct player_race *extensions;

struct player_race *personalities;

struct player_race *player_id2race(guid id)
{
	struct player_race *r;
	for (r = races; !r->extension; r = r->next)
		if (guid_eq(r->ridx, id))
			break;
	return r;
}

struct player_race *player_id2ext(guid id)
{
	struct player_race *r;
	for (r = extensions; !r->personality; r = r->next)
		if (guid_eq(r->ridx, id))
			break;
	return r;
}

struct player_race *player_id2personality(guid id)
{
	struct player_race *r;
	for (r = personalities; r; r = r->next)
		if (guid_eq(r->ridx, id))
			break;
	return r;
}

struct player_race *get_race_by_name(const char *name)
{
	struct player_race *r;
	for (r = races; r; r = r->next)
		if (streq(r->name, name))
			return r;
	return r;
}

/* Gain one or more levels for the first time - including level 1 - with Split Personality.
 * Multiple-level gains ignore intermediate steps if they would involve more than one change, to
 * cut down on the spam. Early levels also have a chance of keeping the same personality silently
 * for the same reason (as they come fast).
 * Level 1 always chooses randomly and is always silent.
 * Level 50 always does nothing - except inform you that you can now change at will.
 * Otherwise, there is a chance increasing with wisdom and level that you will be able to stop the
 * change, and a further chance that you will be told what the new personality would be. 
 **/ 
void personality_split_level(int from, int to)
{
	/* Pick a personality - any except Split (the first) */
	struct player_race *incoming, *r;
	int pers = 0;
	for (r = personalities; r; r = r->next)
		pers++;
	do {
		int rand = randint1(pers-1);
		incoming = personalities;
		for (int i=0; i<rand; i++)
			incoming = incoming->next;
	} while (incoming == player->personality);

	/* Scrub / Munchkin:
	 * Neither should be around for long.
	 * They have the same probability of selection as the others,
	 * but if you have either then early-level skipping won't happen.
	 * Scrub is something you would always want to get rid of, so the
	 * change-check is easier.
	 * Munchkin is something you want to keep, so the change-check is
	 * harder.
	 */
	bool scrub = (streq(player->personality->name, "Scrub"));
	bool munchkin = (streq(player->personality->name, "Munchkin"));

	/* Chance of avoiding a change, /10000.
	 * 10% minimum
	 * 32.5% at wisdom 16 and level 19 
	 * 71.5% at maxed wis and level (48 - gaining level 49 is the last one that cares)
	 **/
	int avoid = 940 + (player->state.stat_ind[STAT_WIS] * 90) + (from * 60);
	if (scrub) {
		/* 77.5% to 92.9% */
		avoid += ((10000 - avoid) * 3) / 4;
	} else if (munchkin) {
		/* 3.3% to 23.8% */
		avoid /= 3;
	}

	/* Chance of skipping */
	static const byte skip[16] = {
		0,   0,  50, 41,   35, 30,  26, 22,
		18, 15,  12, 10,   8,  6,   4,  2,
	};
	bool change = true;

	/* Treat levels 1 and 50 differently */
	switch(to) {
		case 1:
		player->personality = incoming;
		break;

		case PY_MAX_LEVEL:
		msg("You may now change your personality at will.");
		break;

		default:
		if (!scrub && !munchkin) {
			for(int level=from+1; level <= to; level++) {
				/* Skip early levels sometimes */
				if (level < (int)(sizeof(skip)/sizeof(*skip)))
					if (randint0(100) < skip[level])
						change = false;
			}
		}

		if (change) {
			if (randint0(10000) < avoid) {
				char buf[80];
				const char *query = "You feel your personality shifting - allow the change? ";
				strnfmt(buf, sizeof(buf), "You feel your personality shifting - allow changing to %s? ", incoming->name);
				if (randint0(10000) < avoid)
					query = buf;
				if (textui_get_check(query)) {
					player->personality = incoming;
					msg("You let your mind run free. %s!", incoming->name);
				} else {
					msg("You hold on to your personality.");
				}
			} else {
				player->personality = incoming;
				msg("You feel your personality shifting... %s!", incoming->name);
			}
		}
		break;
	}
}
