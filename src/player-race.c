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
#include "obj-gear.h"
#include "player-ability.h"
#include "player-calcs.h"
#include "player-birth.h"
#include "player-util.h"
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
	static const uint8_t skip[16] = {
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

/* Returns true if the player should keep the given slot.
 * It's used for optional parts of a body, such as Clown's Sleeves.
 */
static bool player_keeps_slot(struct player *p, struct equip_slot *slot)
{
	int clown = levels_in_class(get_class_by_name("Clown")->cidx);
	int sleeves = 0;
	if (clown >= 15) sleeves = 1;
	if (clown >= 25) sleeves = 2;
	if (streq(slot->name, "right sleeve") && (sleeves == 0)) return false;
	if (streq(slot->name, "left sleeve") && (sleeves <= 1)) return false;
	return true;
}

/**
 * Set the player's body.
 * This 
 */
void player_set_body(struct player *p, struct player_body *bod)
{
	char buf[80];
	int i, j;

	memcpy(&p->body, bod, sizeof(p->body));
	my_strcpy(buf, bod->name, sizeof(buf));
	p->body.name = string_make(buf);
	p->body.slots = mem_zalloc(p->body.count * sizeof(struct equip_slot));

	for (i = 0, j = 0; i < bod->count; i++) {
		if (player_keeps_slot(p, &bod->slots[i])) {
			p->body.slots[j].type = bod->slots[i].type;
			my_strcpy(buf, bod->slots[i].name, sizeof(buf));
			p->body.slots[j].name = string_make(buf);
			j++;
			p->body.count = j;
		}
	}

	p->upkeep->equip_cnt = 0;
}

/* Find the body to use for the current race */
struct player_body *player_race_body(struct player *p)
{
	assert(p->race);
	struct player_body *bod = bodies;
	for (int i = 0; i < p->race->body; i++) {
		bod = bod->next;
		assert(bod);
	}
	return bod;
}

/**
 * Change body.
 * This should keep equipped items where possible (when there are enough
 * of the right kind of slots for them) - it removes items from the old
 * body and adds them back to the new one. When this fails items end up
 * in inventory, or dropped if there's not room.
 */
void player_change_body(struct player *p, struct player_body *bod)
{
	/* The previous body */
	struct player_body body;
	memcpy(&body, &p->body, sizeof(p->body));

	/* The new one */
	player_set_body(p, bod ? bod : player_race_body(p));

	/* For each old item, remove it and wear it */
	for (int i = 0; i < body.count; i++) {
		struct object *obj = body.slots[i].obj;
		if (obj) {
			int slot = wield_slot(obj);
			if (slot >= 0) {
				p->body.slots[slot].obj = obj;
				p->upkeep->equip_cnt++;
			} else {
				inven_carry(p, obj, true, true);
				combine_pack(p);
				pack_overflow(NULL);
			}
		}
	}

	changed_abilities();

	p->upkeep->update |= (PU_BONUS | PU_INVEN);
	p->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
	update_stuff(p);

	/* Clean up */
	player_cleanup_body(&body);
}
