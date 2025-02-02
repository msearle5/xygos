/**
 * \file ui-store.c
 * \brief Store UI
 *
 * Copyright (c) 1997 Robert A. Koeneke, James E. Wilson, Ben Harrison
 * Copyright (c) 1998-2014 Angband developers
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
#include "game-event.h"
#include "game-input.h"
#include "game-world.h"
#include "hint.h"
#include "init.h"
#include "mon-make.h"
#include "mon-spell.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-quest.h"
#include "player-timed.h"
#include "player-util.h"
#include "store.h"
#include "target.h"
#include "ui-display.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-options.h"
#include "ui-knowledge.h"
#include "ui-object.h"
#include "ui-player.h"
#include "ui-spell.h"
#include "ui-command.h"
#include "ui-store.h"
#include "world.h"
#include "z-debug.h"
#include "z-rand.h"

#include <math.h>

#define ORBIT_DAYTIME 900
#define ORBIT_DURATION 1350
#define ORBIT_FARE 90000


static int store_quest_status(struct store *store);
static int store_get_stock(struct menu *m, int oid, bool sel);

/**
 * Shopkeeper welcome messages.
 *
 * The shopkeeper's name must come first, then the character's name.
 */
static const char *comment_welcome[] =
{
	"",
	"%s nods to you.",
	"%s says hello.",
	"%s: \"See anything you like, adventurer?\"",
	"%s: \"How may I help you, %s?\"",
	"%s: \"Welcome back, %s.\"",
	"%s: \"A pleasure to see you again, %s.\"",
	"%s: \"How may I be of assistance, good %s?\"",
	"%s: \"You do honour to my humble store, noble %s.\"",
	"%s: \"I and my family are entirely at your service, %s.\""
};

static const char *comment_hint[] =
{
/*	"%s tells you soberly: \"%s\".",
	"(%s) There's a saying round here, \"%s\".",
	"%s offers to tell you a secret next time you're about."*/
	"\"%s\""
};


/* State flags */
#define STORE_GOLD_CHANGE      0x01
#define STORE_FRAME_CHANGE     0x02
#define STORE_SHOW_HELP        0x04
#define STORE_SHOW_STEAL       0x08

/* Compound flag for the initial display of a store */
#define STORE_INIT_CHANGE		(STORE_FRAME_CHANGE | STORE_GOLD_CHANGE)

/* Change faction in a way that treats all stores consistently.
 * So town or BM changes are applied directly.
 * As it's easier to gain faction with the Cyber Salon, it's not simply multiplied by 100 but rounded
 * down when going up and all positive faction is lost when going down.
 */
static void change_faction(struct store *store, int change)
{
	if (store->sidx == STORE_CYBER) {
		if (change > 0) {
			player->cyber_faction += 100 * change;
			player->cyber_faction -= (player->cyber_faction % 100);
		} else if (change < 0) {
			if (player->cyber_faction > 0)
				player->cyber_faction = 0;
			player->cyber_faction -= 100 * change;
		}
	} else {
		if (change < 0)
			player->last_faction_loss = turn;
		if (store->sidx == STORE_B_MARKET)
			player->bm_faction += change;
		else
			player->town_faction += change;
	}
}

/* Return a random hint from the given hints list */
const char *random_line(struct hint *hints)
{
	struct hint *v, *r = NULL;
	int n;
	for (v = hints, n = 1; v; v = v->next, n++)
		if (one_in_(n))
			r = v;
	return r->hint;
}

/* Return a random hint from the given arrays of hints lists */
const char *random_line_multi(struct hint **hints_p)
{
	struct hint *v, *r = NULL;
	int n;
	while (*hints_p) {
		struct hint *hints = *hints_p++;
		for (v = hints, n = 1; v; v = v->next, n++)
			if (one_in_(n))
				r = v;
	}
	return r->hint;
}

/* Return a random hint from the global hints or lies lists
 * <real>% of ths time, it's a hint.
 **/
const char *random_rumor(int32_t  real)
{
	struct hint *h = lies;
	if (randint0(100) < real)
		h = hints;
	return random_line(h);
}

/* Return a random hint from the global hints or lies list,
 * with a minimum and maximum length. It is also checked for being
 * "saying like" - that is, the first character is alphanumeric.
 * (The minimum length is for the same reason, while the max is to
 * ensure it fits on screen.)
 **/
const char *random_saying(int32_t  real, int32_t min, int32_t max)
{
	const char *ret;
	int length;
	do {
		ret = random_rumor(real);
		length = strlen(ret);
	} while (!(isalnum(*ret) && (length >= min) && (length <= max)));
	return ret;
}

/* Build a random shopkeeper name */
bool random_shk_name(char *buf, int len)
{
	struct hint *hints[3] = { 0 };
	hints[0] = firstnames;
	bool male = one_in_(2);
	hints[1] = male ? firstnames_male : firstnames_female;
	strnfmt(buf, len, "%s %s", random_line_multi(hints), random_line(secondnames));
	return male;
}

/**
 * Collect from a store you own 
 */
static void collect_from_store(struct store *store)
{
	if (store->income) {
		msg("You collect $%d in takings.", store->income);
		player->au += store->income;
		store->income = 0;
	}
}

/* Is there anything that can be done for an active quest?
 * Returns 0 if there is not (no msg)
 * 1 "Your task is still in progress" / no msg
 * 2 "Your task is complete. Press 'Q' to discuss it." / no msg?
 * 3 "You can press 'Q' to ask about a task." / "I've got something that needs doing - could you help?" (etc).
 * 
 * Of these, 0 means that no quest has anything (so there can be no conflicts)
 * 1 means that a task is in progress
 * 2 means that a task is complete (ignoring whether any are in progress)
 * 3 means that a task is available.
 * 
 * This is the same priority as implicitly in store_quest(), and the order of quests
 * as there takes priority over 1/2/3. The assumption in both places is that having
 * more than one quest in any of these states at the same time shouldn't happen.
 */
/**
 * The greeting a shopkeeper gives the character says a lot about his
 * general attitude.
 *
 * Taken and modified from Sangband 1.0.
 *
 * Note that each comment_hint should have exactly one %s
 */
static void prt_welcome(struct store *store)
{
	char short_name[20];
	const struct owner *proprietor = store->owner;
	const char *owner_name = proprietor->name;

	int j;

	/* Get the first name of the store owner (stop before the first space) */
	for (j = 0; owner_name[j] && owner_name[j] != ' '; j++)
		short_name[j] = owner_name[j];

	/* Truncate the name */
	short_name[j] = '\0';

	int faction = store_faction(store);
	if (store->sidx == STORE_CYBER) {
		faction = store_cyber_rank();
		if (faction > 0)
			faction = (faction >= 3) ? 2 : 1;
	}

	/* If a quest is available, and the last time you visited it wasn't available
	 * (or this is the first time you visited) print a suitable message.
	 */
	const char *quest_msg = NULL;
	uint32_t quest_status = store_quest_status(store);
	if (quest_status != store->quest_status) {
		store->quest_status = quest_status;
		if (quest_status == 3) {
			if (faction < 0)
				quest_msg = "%s: Hmph. Maybe I could still use you for something.";
			else if (faction == 0)
				quest_msg = "%s: There's something I need doing. Are you interested?";
			else if (faction == 1)
				quest_msg = "%s: %s, could you help me with some work that's come up?";
			else
				quest_msg = "%s: Do you have a moment to spare to discuss a job, %s?";
		}
	}

	if (!quest_msg && one_in_(2))
		return;

	if (quest_msg) {
		prt(format(quest_msg, short_name, player->full_name), 0, 0);
	} else if (one_in_(3)) {
		size_t i = randint0(N_ELEMENTS(comment_hint));

		/* Most shks will give you 100% hints by default, but mix in some lies if they don't like you.
		 * The Black Market is economical with the truth by default, but can become mostly helpful if they like you.
		 */
		int truth = 100;
		if (store->sidx == STORE_B_MARKET) {
			if (faction < -1)
				truth = 0;
			else if (faction == -1)
				truth = 10;
			else if (faction == 0)
				truth = 40;
			else
				truth = 90;
		} else {
			if (faction < -1)
				truth = 10;
			else if (faction == -1)
				truth = 30;
			else
				truth = 100;
		}
		msg(comment_hint[i], random_saying(truth, 20, 75));
	} else if (player->lev > 5) {
		const char *player_name;
		int divi = 10;
		if (faction < -1)
			faction = -1;
		switch (faction) {
			case -1:
				divi = 17;
				break;
			case 0:
				divi = 9;
				break;
			case 1:
				divi = 5;
				break;
			default:
				divi = 4;
				break;
		}

		/* We go from level 1 - 50  */
		size_t i = ((unsigned)player->lev - 1) / divi;
		i = MIN(i, N_ELEMENTS(comment_welcome) - 1);

		/* Get a title for the character */
		if ((i % 2) && randint0(2))
			player_name = player_title();
		else if (randint0(2))
			player_name = player->full_name;
		else
			player_name = "valued customer";

		if (faction < 0) {
			const char *insult[] = {
				"scum", "loser", "waster", "rat", "dirtball", "scumbag", "smeghead", "goober"
			};
			player_name = insult[randint0(sizeof(insult) / sizeof(*insult))];
		}

		/* Balthazar says "Welcome" */
		prt(format(comment_welcome[i], short_name, player_name), 0, 0);
	}
}


/*** Display code ***/


/**
 * This function sets up screen locations based on the current term size.
 *
 * Current screen layout:
 *  line 0: reserved for messages
 *  line 1: shopkeeper and their purse / item buying price
 *  line 2: empty
 *  line 3: table headers
 *
 *  line 4: Start of items
 *
 * If help is turned off, then the rest of the display goes as:
 *
 *  line (height - 4): end of items
 *  line (height - 3): "more" prompt
 *  line (height - 2): empty
 *  line (height - 1): Help prompt and remaining gold
 *
 * If help is turned on, then the rest of the display goes as:
 *
 *  line (height - 7): end of items
 *  line (height - 6): "more" prompt
 *  line (height - 4): gold remaining
 *  line (height - 3): command help 
 */
static void store_display_recalc(struct store_context *ctx)
{
	int wid, hgt;
	region loc;

	struct menu *m = &ctx->menu;
	struct store *store = ctx->store;

	Term_get_size(&wid, &hgt);

	/* Clip the width at a max of 104 (enough room for an 80-char item name) */
	if (wid > 104) wid = 104;

	/* Clip the text_out function at two smaller than the screen width */
	text_out_wrap = wid - 2;


	/* X co-ords first */
	ctx->scr_places_x[LOC_PRICE] = wid - 14;
	ctx->scr_places_x[LOC_AU] = wid - 27;
	ctx->scr_places_x[LOC_OWNER] = wid - 2;
	ctx->scr_places_x[LOC_WEIGHT] = wid - 14;

	/* Add space for for prices */
	if (store->sidx != STORE_HOME)
		ctx->scr_places_x[LOC_WEIGHT] -= 10;

	/* Then Y */
	ctx->scr_places_y[LOC_OWNER] = 1;
	ctx->scr_places_y[LOC_HEADER] = 3;

	/* If we are displaying help, make the height smaller */
	if (ctx->flags & (STORE_SHOW_HELP | STORE_SHOW_STEAL))
		hgt -= 3;

	ctx->scr_places_y[LOC_MORE] = hgt - 3;
	ctx->scr_places_y[LOC_AU] = hgt - 1;

	loc = m->boundary;

	/* If we're displaying the help, then put it with a line of padding */
	if (ctx->flags & (STORE_SHOW_HELP | STORE_SHOW_STEAL)) {
		ctx->scr_places_y[LOC_HELP_CLEAR] = hgt - 11;
		ctx->scr_places_y[LOC_HELP_PROMPT] = hgt - 7;
		loc.page_rows = -11;
	} else {
		ctx->scr_places_y[LOC_HELP_CLEAR] = hgt - 2;
		ctx->scr_places_y[LOC_HELP_PROMPT] = hgt - 1;
		loc.page_rows = -2;
	}

	menu_layout(m, &loc);
}


/**
 * Redisplay a single store entry
 */
static void store_display_entry(struct menu *menu, int oid, bool cursor, int row,
								int col, int width)
{
	struct object *obj;
	int32_t x;
	uint32_t desc = ODESC_PREFIX;

	char o_name[80];
	char out_val[160];
	uint8_t colour;

	struct store_context *ctx = menu_priv(menu);
	struct store *store = ctx->store;
	assert(store);

	if (store->sidx == STORE_AIR) {
		/* Airports display tickets, but these aren't actual items */
		if (oid >= world_connections(player->town)) {
			/* Destination */
			colour = COLOUR_MAGENTA;
			c_put_str(colour, "Orbital Station", row, col);

			/* Airline */
			c_put_str(colour, player->town->stores[STORE_AIR].owner->name, row, 30);

			/* Travel Time */
			c_put_str(colour, format_duration(ORBIT_DURATION), row, 48);

			/* Departure Time */
			c_put_str(colour, format_time(ORBIT_DAYTIME), row, ctx->scr_places_x[LOC_WEIGHT]);

			/* Price */
			c_put_str(colour, format("%d", ORBIT_FARE), row, ctx->scr_places_x[LOC_PRICE] + 4);
		} else {
			/* Destination */
			colour = (oid & 1) ? COLOUR_L_BLUE : COLOUR_DEEP_L_BLUE;
			c_put_str(colour, player->town->connect[oid]->name, row, col);

			/* Airline */
			c_put_str(colour, player->town->connect[oid]->stores[STORE_AIR].owner->name, row, 30);

			/* Travel Time */
			c_put_str(colour, format_duration(world_flight_time(player->town, player->town->connect[oid])), row, 48);

			/* Departure Time */
			c_put_str(colour, format_time(world_departure_time(player->town, player->town->connect[oid])), row, ctx->scr_places_x[LOC_WEIGHT]);

			/* Price */
			c_put_str(colour, format("%d", world_airline_fare(player->town, player->town->connect[oid])), row, ctx->scr_places_x[LOC_PRICE] + 4);
		}
	} else {
		/* Get the object */
		obj = ctx->list[oid];

		/* Describe the object - preserving inscriptions in the home */
		if (store->sidx == STORE_HOME) {
			desc |= ODESC_FULL;
		} else {
			desc |= ODESC_FULL | ODESC_STORE;
		}
		object_desc(o_name, sizeof(o_name), obj, desc, player);

		/* Display the object */
		c_put_str(obj->kind->base->attr, o_name, row, col);

		/* Show weights */
		colour = curs_attrs[CURS_KNOWN][(int)cursor];
		strnfmt(out_val, sizeof out_val, fmt_weight(obj->weight, NULL));
		c_put_str(colour, out_val, row, ctx->scr_places_x[LOC_WEIGHT]);

		/* Describe an object (fully) in a store */
		if (store->sidx != STORE_HOME) {
			/* Extract the "minimum" price */
			x = price_item(store, obj, false, 1);

			/* Make sure the player can afford it */
			if ((int) player->au < (int) x)
				colour = curs_attrs[CURS_UNKNOWN][(int)cursor];

			/* Actually draw the price */
			if (tval_can_have_charges(obj) && (obj->number > 1))
				strnfmt(out_val, sizeof out_val, "%9d avg", x);
			else
				strnfmt(out_val, sizeof out_val, "%9d    ", x);

			c_put_str(colour, out_val, row, ctx->scr_places_x[LOC_PRICE]);
		}
	}
}



/**
 * Display store (after clearing screen)
 */
static void store_display_frame(struct store_context *ctx)
{
	char buf[80];
	struct store *store = ctx->store;
	struct owner *proprietor = store->owner;

	/* Clear screen */
	Term_clear();

	/* The "Home" is special */
	if (store->sidx == STORE_HOME) {
		/* Put the owner name */
		put_str("Your Home", ctx->scr_places_y[LOC_OWNER], 1);

		/* Label the object descriptions */
		put_str("Home Inventory", ctx->scr_places_y[LOC_HEADER], 1);

		/* Show weight header */
		put_str("Weight", ctx->scr_places_y[LOC_HEADER],
				ctx->scr_places_x[LOC_WEIGHT]);
	} else {
		/* Normal stores */
		const char *store_name = store->name;
		const char *owner_name = proprietor->name;

		/* Put the owner name */
		put_str(owner_name, ctx->scr_places_y[LOC_OWNER], 1);

		/* Airport */
		if (store->sidx == STORE_AIR) {
			/* Label the ticket destinations */
			put_str("Destination", ctx->scr_places_y[LOC_HEADER], 4);

			/* Just decoration */
			put_str("Airline", ctx->scr_places_y[LOC_HEADER], 30);

			/* Showing time label (length of flight) */
			put_str("Length", ctx->scr_places_y[LOC_HEADER], 48);

			/* Showing time label (departure time) */
			put_str("Departs", ctx->scr_places_y[LOC_HEADER], ctx->scr_places_x[LOC_WEIGHT]);

			/* Show the time and this location */
			prt(player->town->name, ctx->scr_places_y[LOC_OWNER], 36 - (strlen(player->town->name) + 1));
			prt("Airport (", ctx->scr_places_y[LOC_OWNER], 36);

			c_prt(COLOUR_ORANGE, format_time(turn), ctx->scr_places_y[LOC_OWNER], 45);
			prt(")", ctx->scr_places_y[LOC_OWNER], 50);

			strcpy(buf, "Departures");
		} else {
			/* Label the object descriptions */
			put_str("Store Inventory", ctx->scr_places_y[LOC_HEADER], 1);

			/* Showing weight label */
			put_str("Weight", ctx->scr_places_y[LOC_HEADER],
					ctx->scr_places_x[LOC_WEIGHT]);

			/* Show the max price in the store (above prices) */
			strnfmt(buf, sizeof(buf), proprietor->max_cost ? "%s (%d)" : "%s", store_name,
					proprietor->max_cost);
		}

		prt(buf, ctx->scr_places_y[LOC_OWNER],
			ctx->scr_places_x[LOC_OWNER] - strlen(buf));

		/* Label the asking price (in stores) */
		put_str("Price", ctx->scr_places_y[LOC_HEADER], ctx->scr_places_x[LOC_PRICE] + 4);
	}
}

/**
 * Display steal item help.
 */
static void store_display_steal_help(struct store_context *ctx)
{
	int help_loc = ctx->scr_places_y[LOC_HELP_PROMPT];

	/* Clear */
	clear_from(ctx->scr_places_y[LOC_HELP_CLEAR]);

	/* Prepare help hooks */
	text_out_hook = text_out_to_screen;
	text_out_indent = 1;
	Term_gotoxy(1, help_loc);

	/* Describe fields */
	text_out_c(COLOUR_L_GREEN, "Fail");
	text_out(" is the chance that you will fail to make a clean getaway with the item, without the shop owner noticing. ");

	text_out_c(COLOUR_L_GREEN, "Item");
	text_out(" is the chance that you will steal the item (possibly being seen and banned in the process). ");

	text_out_c(COLOUR_L_GREEN, "Ban");
	text_out(" is the chance that you will be banned from the store. This also implies loss of faction, which will make further attempts riskier. ");

	text_out_c(COLOUR_L_GREEN, "Fight");
	text_out(" is the chance that you will have to fight the store owner. This also means losing the item, being banned and losing faction.");

	text_out_indent = 0;
}

/**
 * Display help.
 */
static void store_display_help(struct store_context *ctx)
{
	struct store *store = ctx->store;
	int help_loc = ctx->scr_places_y[LOC_HELP_PROMPT];
	bool is_home = (store->sidx == STORE_HOME) ? true : false;
	bool is_hq = (store->sidx == STORE_HQ) ? true : false;
	bool is_air = (store->sidx == STORE_AIR) ? true : false;

	/* Clear */
	clear_from(ctx->scr_places_y[LOC_HELP_CLEAR]);

	/* Prepare help hooks */
	text_out_hook = text_out_to_screen;
	text_out_indent = 1;
	Term_gotoxy(1, help_loc);

	if (OPT(player, rogue_like_commands))
		text_out_c(COLOUR_L_GREEN, "x");
	else
		text_out_c(COLOUR_L_GREEN, "l");

	text_out(" examines");
	if (!ctx->inspect_only) {
		text_out(" and ");
		text_out_c(COLOUR_L_GREEN, "p");

		if (is_home) text_out(" picks up");
		else text_out(" purchases");
	}
	if (is_air)
		text_out(" the selected ticket. ");
	else
		text_out(" the selected item. ");
	text_out_c(COLOUR_L_GREEN, "Space");
	text_out(" pages through off-screen tickets. ");

	if ((!is_hq) && (!is_air)) {
		if (!ctx->inspect_only) {
			if (OPT(player, birth_no_selling)) {
				text_out_c(COLOUR_L_GREEN, "d");
				text_out(" gives an item to the store in return for its identification. Some items will also be recharged. ");
			} else {
				text_out_c(COLOUR_L_GREEN, "d");
				if (is_home) text_out(" drops");
				else text_out(" sells");
				text_out(" an item from your inventory. ");
			}
		} else {
			text_out_c(COLOUR_L_GREEN, "I");
			text_out(" inspects an item from your inventory. ");
		}
	}

	if ((!is_home) && (!is_hq) && (!is_air)) {
		text_out_c(COLOUR_L_GREEN, "H");
		text_out(" holds an item (you pay 10%% now to keep it in stock. If you buy it the same day, you'll only pay the remaining 90%% - but each extra day will add 10%% more.) ");
		text_out_c(COLOUR_L_GREEN, "S");
		text_out(" attempts to steal an item (this is risky!), while ");
		text_out_c(COLOUR_L_GREEN, "F");
		text_out(" picks a fight with the owner (even more risky!). ");
		text_out_c(COLOUR_L_GREEN, "N");
		text_out(" asks the owner to obtain new stock (this is expensive), while ");
		text_out_c(COLOUR_L_GREEN, "O");
		text_out(" asks the owner to leave, to be replaced by a different shopkeeper (this is very expensive). ");
		text_out_c(COLOUR_L_GREEN, "B");
		text_out(" buys the store for yourself (this is extremely expensive). ");
		text_out_c(COLOUR_L_GREEN, "Q");
		text_out(" asks the owner if there is anything they want doing for them. ");
	}

	text_out_c(COLOUR_L_GREEN, "ESC");
	if (!ctx->inspect_only)
		text_out(" exits the building.");
	else
		text_out(" exits this screen.");

	text_out_indent = 0;
}

/**
 * Decides what parts of the store display to redraw.  Called on terminal
 * resizings and the redraw command.
 */
static void store_redraw(struct store_context *ctx)
{
	if (ctx->flags & (STORE_FRAME_CHANGE)) {
		store_display_frame(ctx);

		if (ctx->flags & STORE_SHOW_HELP)
			store_display_help(ctx);
		else if (ctx->flags & STORE_SHOW_STEAL)
			store_display_steal_help(ctx);
		else {
			const char *help = "Press '?' for help.";
			int helpx = 1;
			prt(help, ctx->scr_places_y[LOC_HELP_PROMPT], helpx);
			int quest_status = store_quest_status(ctx->store);
			const char *quest_msg = NULL; 
			switch(quest_status) {
				case 2:
					quest_msg = "Press 'Q' to finish your task.";
					break;
				case 3:
					quest_msg = "Press 'Q' to ask about a task.";
					break;
			}
			if (quest_msg) {
				int minx = helpx + strlen(help);
				int maxx = ctx->scr_places_x[LOC_AU] - 1;
				int posx = minx + (((maxx - minx) - (strlen(quest_msg))) / 2);
				c_put_str(COLOUR_ORANGE, quest_msg, ctx->scr_places_y[LOC_HELP_PROMPT], posx);
			} 
		}
		ctx->flags &= ~(STORE_FRAME_CHANGE);
	}

	if (ctx->flags & (STORE_GOLD_CHANGE)) {
		if (!(ctx->flags & (STORE_SHOW_HELP | STORE_SHOW_STEAL)))
			prt(format("Cash Remaining: $%9d", player->au),
				ctx->scr_places_y[LOC_AU], ctx->scr_places_x[LOC_AU]);
		ctx->flags &= ~(STORE_GOLD_CHANGE);
	}
}

static bool store_do_check(void)
{
	struct keypress ch;

	/* Get an answer */
	ch = inkey();

	/* Erase the prompt */
	prt("", 0, 0);

	if (ch.code == ESCAPE) return (false);
	if (strchr("Nn", ch.code)) return (false);

	/* Success */
	return (true);
}

static bool store_get_check(const char *prompt)
{
	/* Prompt for it */
	prt(prompt, 0, 0);

	return store_do_check();
}

/* Print a long (multi line, formatted) message */
void store_long_text(struct store_context *ctx, const char *text)
{
	/* Where the help text goes */
	ctx->flags |= (STORE_SHOW_HELP);
	store_display_recalc(ctx);
	int help_loc = ctx->scr_places_y[LOC_HELP_PROMPT];

	/* Clear */
	clear_from(ctx->scr_places_y[LOC_HELP_CLEAR]);

	/* Prepare hooks */
	text_out_hook = text_out_to_screen;
	text_out_indent = 1;
	Term_gotoxy(1, help_loc);

	/* Print it, wait */
	text_out(text);
	inkey_ex();

	/* Clean up */
	store_display_recalc(ctx);
	return;
}

bool store_get_long_check(struct store_context *ctx, const char *prompt)
{
	/* Print a long (multi line, formatted) prompt */
	unsigned long flags = ctx->flags;

	/* Where the help text goes */
	ctx->flags |= (STORE_SHOW_HELP);
	store_display_recalc(ctx);
	int help_loc = ctx->scr_places_y[LOC_HELP_PROMPT];

	/* Clear */
	clear_from(ctx->scr_places_y[LOC_HELP_CLEAR]);

	/* Prepare hooks */
	text_out_hook = text_out_to_screen;
	text_out_indent = 1;
	Term_gotoxy(1, help_loc);

	/* Print it */
	text_out(prompt);
	text_out_c(COLOUR_L_BLUE, " [yn]");
	text_out_indent = 0;

	/* Clean up and return your response */
	ctx->flags = flags;
	store_display_recalc(ctx);
	return store_do_check();
}

/*
 * Sell an object, or drop if it we're in the home.
 */
static bool store_sell(struct store_context *ctx)
{
	int amt;
	int get_mode = USE_EQUIP | USE_INVEN | USE_FLOOR | USE_QUIVER;

	struct store *store = ctx->store;
	assert(store);
	bool cyber = (store->sidx == STORE_CYBER);

	struct object *obj;
	struct object object_type_body = OBJECT_NULL;
	struct object *temp_obj = &object_type_body;

	char o_name[120];

	item_tester tester = NULL;

	const char *reject = "You have nothing that I want. ";
	const char *prompt = OPT(player, birth_no_selling) ? "Give which item? " : "Sell which item? ";
	if (cyber)
		prompt = OPT(player, birth_no_selling) ? "Give, install or uninstall which item? " : "Sell, install or uninstall which item? ";

	if ((store->sidx == STORE_HQ) || (store->sidx == STORE_AIR)) {
		msg("We don't buy used goods.");
		return false;
	}

	/* Clear all current messages */
	msg_flag = false;
	prt("", 0, 0);

	if (store->sidx == STORE_HOME) {
		prompt = "Drop which item? ";
	} else {
		tester = store_will_buy_tester;
		get_mode |= SHOW_PRICES;
	}

	/* Get an item */
	player->upkeep->command_wrk = USE_INVEN;

	if (!get_item(&obj, prompt, reject, CMD_DROP, tester, get_mode))
		return false;

	/* Cannot remove stickied objects */

	if (object_is_equipped(player->body, obj)) {
		/* Oops */
		bool stuck = (cyber ? (!obj_cyber_can_takeoff(obj)) : (!obj_can_takeoff(obj)));
		if (stuck) {
			msg("Hmmm, it seems to be stuck.");
			/* Nope */
			return false;
		}
	}

	/* Get a quantity */
	amt = get_quantity(NULL, obj->number);

	/* Allow user abort */
	if (amt <= 0) return false;

	/* Get a copy of the object representing the number being sold */
	object_copy_amt(temp_obj, obj, amt);

	if (!store_check_num(store, temp_obj)) {
		object_wipe(temp_obj);
		if (store->sidx == STORE_HOME)
			msg("Your home is full.");
		else
			msg("I have no room in my store to keep it.");

		return false;
	}

	/* Check for special quest behaviours */
	if (!quest_selling_object(obj, ctx)) {

		/* Get a full description */
		object_desc(o_name, sizeof(o_name), temp_obj, ODESC_PREFIX | ODESC_FULL, player);

		/* Real store */
		if (store->sidx != STORE_HOME) {
			int cmd = CMD_SELL;

			/* Extract the value of the items */
			uint32_t price = price_item(store, temp_obj, true, amt);

			object_wipe(temp_obj);
			screen_save();

			/* Cyber Salon: may want to install it? */
			bool equipped = (object_is_equipped(player->body, obj));
			if (cyber && (tval_is_implant(obj))) {
				/* Show price */
				prt(format("Price to %s: %d", equipped ? "remove" : "implant", store_cyber_install_price(obj)), 1, 0);

				if (store_get_check(format("%s %s? [ESC, any other key to %sinstall]",
					equipped ? "Uninstall" : "Install", o_name, equipped ? "un" : ""))) {
					cmd = CMD_INSTALL;
				}
			} else {

				/* Show price */
				if (!OPT(player, birth_no_selling))
					prt(format("Price: %d", price), 1, 0);

				/* Confirm sale */
				if (!store_get_check(format("%s %s? [ESC, any other key to accept]",
						OPT(player, birth_no_selling) ? "Give" : "Sell", o_name))) {
					screen_load();
					return false;
				}
			}

			screen_load();

			cmdq_push(cmd);
			cmd_set_arg_item(cmdq_peek(), "item", obj);
			cmd_set_arg_number(cmdq_peek(), "quantity", amt);
		} else { /* Player is at home */
			object_wipe(temp_obj);
			cmdq_push(CMD_STASH);
			cmd_set_arg_item(cmdq_peek(), "item", obj);
			cmd_set_arg_number(cmdq_peek(), "quantity", amt);
		}
	}

	/* Update the display */
	ctx->flags |= STORE_GOLD_CHANGE;

	return true;
}

/* Set a store's owner and name to you */
void store_your_name(struct store *store)
{
	char buf[256];
	store->owner = store->owners;
	string_free(store->owner->name);
	const char *shape = (player_is_shapechanged(player) ? player->shape->name : player->race->name);
	if (store->sidx == STORE_HQ)
		snprintf(buf, sizeof(buf), "General %s (%s)", player->full_name, shape);
	else
		snprintf(buf, sizeof(buf), "%s the %s (%s)", player->full_name, player_title(), shape);
	buf[sizeof(buf)-1] = 0;
	store->owner->name = string_make(buf);
}


/**
 * Buy an object from a store
 */
static bool store_purchase(struct store_context *ctx, int item, bool single, bool *exit)
{
	struct store *store = ctx->store;

	struct object *obj = ctx->list[item];
	struct object *dummy = NULL;

	char o_name[80];

	int amt, num;

	int32_t price;

	/* Clear all current messages */
	msg_flag = false;
	prt("", 0, 0);

	if (store->sidx == STORE_AIR) {
		/* Check if the player can afford it */
		int fare = ORBIT_FARE;
		int departs = ORBIT_DAYTIME;
		int flighttime = ORBIT_DURATION;
		const char *name = "the space station";
		if (item < world_connections(player->town)) {
			fare = world_airline_fare(player->town, player->town->connect[item]);
			departs = world_departure_time(player->town, player->town->connect[item]);
			flighttime = world_flight_time(player->town, player->town->connect[item]);
			name = player->town->connect[item]->name;
		}

		if ((int)player->au < fare) {
			msg("You do not have enough money for this ticket.");
			return false;
		}

		/* Check if they have statuses (because otherwise they would be expected to wear off)
		 **/
		for (int i = 0; i < TMD_MAX; i++) {
			if ((player->timed[i]) && (timed_effects[i].flag_general & PG_NASTY)) {
				msg("I am afraid you can't fly with us in that condition!");
				return false;
			}
		}

		/* Check if they are sure */
		bool light = is_daytime();
		if (store_get_check(format("Fly to %s? [ESC, any other key to accept]", name))) {

			/* Deduct payment */
			player->au -= fare;

			/* Describe the flight */
			if (item < world_connections(player->town)) {
				msg("You wait until the %s flight, take off and land in %s.",
					format_time(departs), player->town->connect[item]->name);
			} else {
				ui_text_box("You climb into the waiting Kerbalspace Sisto and blast off into the wild black yonder. Surprisingly quickly a bright star ahead resolves into the complex, gleaming shape of the Xygos Orbital Station. There's a brief deceleration, a slight jolt, a hiss of gas - and you are docked. This is it!");
			}

			/* Take time */
			int turns;
			int timeofday = (turn % (10L * z_info->day_length));
			if (departs >= timeofday) {
				/* Departs today */
				turns = departs - timeofday;
			} else {
				/* Tomorrow */
				turns = (departs + (10L * z_info->day_length)) - timeofday;
			}
			turns += flighttime;
			turn += turns;

			/* Change the danger level (for prices, if nothing else) */
			increase_danger_level();

			/* Feed */
			player->timed[TMD_FOOD] = PY_FOOD_FULL - 1;

			/* Heal */
			player->chp = player->mhp;

			/* Mention change of light */
			bool newlight = is_daytime();
			if (light != newlight) {
				msg("You step out into the %s.", newlight ? "daylight" : "darkness");
			}

			/* Signal to move to outside the Airport */
			player->upkeep->flight_level = true;
			*exit = true;

			if (item < world_connections(player->town)) {
				/* Move to the new town */
				world_change_town(player->town->connect[item]);

				/* Take a turn */
				player->upkeep->energy_use = z_info->move_energy;

				/* Change level */
				dungeon_change_level(player, 0);

				/* Update the store context (as you are now in a different airport, in a new town!) */
				ctx->store = get_store_by_idx(STORE_AIR);
			} else {
				/* No town */
				world_change_town(NULL);

				/* Space station: no town, start at level 100 */
				dungeon_change_level(player, 100);
				return false;
			}
		}

		/* Update the display */
		ctx->flags |= STORE_GOLD_CHANGE | STORE_FRAME_CHANGE;
		store_redraw(ctx);

		return true;

	} else {

		/*** Check the player can get any at all ***/

		/* Get an amount if we weren't given one */
		if (single) {
			amt = 1;

			/* Check if the player can afford any at all */
			if (store->sidx != STORE_HOME &&
					(int)player->au < (int)price_item(store, obj, false, 1)) {
				msg("You do not have enough money for this item.");
				return false;
			}
		} else {
			if (store->sidx == STORE_HOME) {
				amt = obj->number;
			} else {
				/* Price of one */
				price = price_item(store, obj, false, 1);

				/* Check if the player can afford any at all */
				if ((uint32_t)player->au < (uint32_t)price) {
					msg("You do not have enough money for this item.");
					return false;
				}

				/* Work out how many the player can afford */
				if (price == 0)
					amt = obj->number; /* Prevent division by zero */
				else
					amt = player->au / price;

				/* Double check for wands/staves */
				if ((player->au >= price_item(store, obj, false, amt+1)) &&
					(amt < obj->number))
					amt++;

				if (amt > obj->number) amt = obj->number;
			}

			/* Limit to the number that can be carried */
			amt = MIN(amt, inven_carry_num(player, obj, false));

			/* Fail if there is no room.  Don't leak information about
			 * unknown flavors for a purchase (getting it from home doesn't
			 * leak information since it doesn't show the true flavor). */
			bool flavor_aware = object_flavor_is_aware(obj);
			if (amt <= 0 || (!flavor_aware && store->sidx != STORE_HOME &&
								   pack_is_full())) {
				msg("You cannot carry that many items.");
				return false;
			}

			/* Find the number of this item in the inventory.  As above,
			 * avoid leaking information about unknown flavors. */
			if (!flavor_aware && store->sidx != STORE_HOME)
				num = 0;
			else
				num = find_inven(obj);

			strnfmt(o_name, sizeof o_name, "%s how many%s? (max %d) ",
					(store->sidx == STORE_HOME) ? "Take" : "Buy",
					num ? format(" (you have %d)", num) : "", amt);

			/* Get a quantity */
			amt = get_quantity(o_name, amt);

			/* Allow user abort */
			if (amt <= 0) return false;
		}
	}

	/* Get desired object */
	dummy = object_new();
	object_copy_amt(dummy, obj, amt);

	/* Ensure we have room */
	if (!inven_carry_okay(dummy)) {
		msg("You cannot carry that many items.");
		object_delete(NULL, NULL, &dummy);
		return false;
	}

	/* Describe the object (fully) */
	object_desc(o_name, sizeof(o_name), dummy, ODESC_PREFIX | ODESC_FULL |
		ODESC_STORE, player);

	/* Attempt to buy it */
	if (store->sidx != STORE_HOME) {
		bool response;

		/* Extract the price for the entire stack */
		price = price_item(store, dummy, false, dummy->number);

		screen_save();

		/* Show price */
		prt(format("Price: %d", price), 1, 0);

		/* Confirm purchase */
		response = store_get_check(format("Buy %s? [ESC, any other key to accept]", o_name));
		screen_load();

		/* Negative response, so give up */
		if (!response) return false;

		cmdq_push(CMD_BUY);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	} else {
		/* Home is much easier */
		cmdq_push(CMD_RETRIEVE);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	}

	/* Update the display */
	ctx->flags |= STORE_GOLD_CHANGE;

	object_delete(NULL, NULL, &dummy);

	/* Not kicked out */
	return true;
}

/* Return the store owner's first name */
static char *store_shortname(struct store_context *ctx)
{
	static char buf[256];
	strncpy(buf,  ctx->store->owner->name, sizeof(buf));
	buf[sizeof(buf)-1] = 0;
	char *space = strchr(buf, ' ');
	if (space) {
		*space = 0;
	}
	return buf;
}

/* Return the store owner's full name */
static char *store_fullname(struct store_context *ctx)
{
	static char buf[256];
	strncpy(buf,  ctx->store->owner->name, sizeof(buf));
	buf[sizeof(buf)-1] = 0;
	char *space = strchr(buf, '(');
	if (space) {
		*(space-1) = 0;
	}
	return buf;
}

/* Fight the owner
 * Returns true if successful (this doesn't imply that the fight was successful - that starts after this has returned)
 * Based on Single Combat (and maybe should be combined?)
 **/
static bool store_do_fight(struct store_context *ctx)
{
	/* Pick the randomizable monster race */
	struct monster_race *race = &r_info[1];
	assert(race);

	/* Build a random unique based on your level and the store owner's name, race etc. */
	int level = ((player->lev * 3) / 2) + 15;
	if (level < 20) {
		level = 20;
	}

	/* Basic stats */
	string_free(race->name);
	race->name = string_make(store_fullname(ctx));
	race->level = level;
	race->speed = 110;
	race->avg_hp = 100;
	race->ac = 20;
	race->freq_spell = 12;
	race->freq_innate = 12;
	rf_off(race->flags, RF_IM_ACID);

	/* Evil (BM) from description? */

	rf_off(race->flags, RF_FEMALE);
	rf_off(race->flags, RF_MALE);
	if (ctx->store->owner->male)
		rf_on(race->flags, RF_MALE);
	else
		rf_on(race->flags, RF_FEMALE);

	int i = level;
	int spells = 0;
	int innate = 0;

	/* Increase them */
	do {
		switch(randint0(8)) {
			case 0:
				if (race->speed < 140) {
					race->speed++;
					i--;
					if (race->speed > 120)
						i--;
					if (race->speed > 130)
						i--;
				}
				break;
			case 1:
				race->avg_hp = (race->avg_hp * 12) / 10;
				i--;
				break;
			case 2:
				race->ac += 5;
				i--;
				break;
			case 3:
				if (spells > 0) {
					if (race->freq_spell > 1) {
						race->freq_spell--;
						i -= 12/race->freq_spell;
					}
				}
				break;
			case 4:
				if (innate > 0) {
					if (race->freq_innate > 1) {
						race->freq_innate--;
						i -= 12/race->freq_innate;
					}
				}
				break;
			case 5: {
					uint32_t flag[] = {
						RF_IM_ACID, RF_IM_ELEC, RF_IM_FIRE, RF_IM_COLD,
						RF_IM_POIS
					};
					uint32_t f = flag[randint0(sizeof(flag)/sizeof(flag[0]))];
					if (!rf_has(race->flags, f)) {
						rf_on(race->flags, f);
						i--;
					}
					break;
				}
			case 6: {
					uint32_t sflag[] = {
						RSF_BOULDER, RSF_BOLT
					};
					uint32_t f = sflag[randint0(sizeof(sflag)/sizeof(sflag[0]))];
					if (!rsf_has(race->spell_flags, f)) {
						rsf_on(race->spell_flags, f);
						i--;
						innate++;
					}
					break;
				}
			case 7: {
					uint32_t sflag[] = {
						RSF_BA_ACID, RSF_BO_POIS, RSF_STORM, RSF_BLIND, RSF_CONF
					};
					uint32_t f = sflag[randint0(sizeof(sflag)/sizeof(sflag[0]))];
					if (!rsf_has(race->spell_flags, f)) {
						rsf_on(race->spell_flags, f);
						i--;
						spells++;
					}
					break;
				}
			default:
				quit_fmt("bug in random unique");
		}
	} while (i > 0);

	/* Pick a location */
	struct loc grid;
	struct monster_group_info info = { 0, 0 };
	do {
		scatter(cave, &grid, player->grid, 100, true);
	} while (!square_in_bounds(cave, grid) || !square_isempty(cave, grid));

	/* Place it */
	place_new_monster(cave, grid, race, true, false, info, ORIGIN_DROP_SHK);
	struct monster *mon = square_monster(cave, grid);
	assert(mon);

	/* Swap the targeted monster with the first in the monster list */
	int old_idx = mon->midx;
	if (old_idx == 1) {
		/* Do nothing */
		;
	} else if (cave_monster(cave, 1)->race) {
		monster_index_move(old_idx, cave_monster_max(cave));
		monster_index_move(1, old_idx);
		monster_index_move(cave_monster_max(cave), 1);
	} else {
		monster_index_move(old_idx, 1);
	}
	target_set_monster(cave_monster(cave, 1));

	/* This won't make you popular */
	change_faction(ctx->store, -1);

	/* Head to the arena */
	health_track(player->upkeep, square_monster(cave, grid));
	player->upkeep->arena_level = true;
	player->arena_type = arena_shop;
	dungeon_change_level(player, 1);

	return true;
}

/* attempt to steal an item */
static void store_steal(struct store_context *ctx, int item, struct menu *m, bool *exit)
{
	struct store *store = ctx->store;
	assert(store);

	/* Clear all current messages */
	msg_flag = false;
	prt("", 0, 0);

	if (ctx->store->sidx == STORE_HOME) {
		msg("You daringly steal from your own home!");
		return;
	}

	if (ctx->store->sidx == STORE_HQ) {
		msg("You decide that would be a bad move.");
		return;
	}

	if (ctx->store->sidx == STORE_AIR) {
		msg("You don't think you could fit an airplane in your pocket.");
		return;
	}

	/* Confirmation is needed as it is risky, but you get at least one later prompt so it is OK
	 * to not confirm immediately
	 **/
	msg("Steal which item? (ESC to cancel, Enter to select)");

	msg_flag = false;
	item = store_get_stock(m, item, false);

	/* Negative response, so give up */
	if (item < 0) {
		return;
	}

	int price = 0;
	double difficulty = 0.0;
	int num = 0;
	int amt = 0;

	struct object *obj = ctx->list[item];
	struct object *dummy = NULL;

	char o_name[80];

	/* Get an amount if we weren't given one */
	{
		/* Price of one */
		price = price_item(ctx->store, obj, false, 1);

		/* Limit to the number that can be carried */
		amt = MIN(obj->number, inven_carry_num(player, obj, false));

		/* Fail if there is no room */
		if ((amt <= 0) || (!object_flavor_is_aware(obj) && pack_is_full())) {
			msg("You cannot carry that many items.");
			return;
		}

		/* Find the number of this item in the inventory */
		if (object_flavor_is_aware(obj))
			num = find_inven(obj);

		strnfmt(o_name, sizeof o_name, "Steal how many%s? (max %d) ", num ? format(" (you have %d)", num) : "", amt);

		/* Get a quantity */
		amt = get_quantity(o_name, amt);

		/* Allow user abort */
		if (amt <= 0) return;
	}

	/* Get desired object */
	dummy = object_new();
	object_copy_amt(dummy, obj, amt);

	/* Ensure we have room */
	if (!inven_carry_okay(dummy)) {
		msg("You cannot carry that many items.");
		object_delete(NULL, NULL, &dummy);
		return;
	}

	/* Describe the object (fully) */
	object_desc(o_name, sizeof(o_name), dummy, ODESC_PREFIX | ODESC_FULL |
		ODESC_STORE, player);

	/* Extract the price for the entire stack */
	price = price_item(store, dummy, false, dummy->number);

	screen_save();

	/* Difficulty.
	 * DEX based, but biased by amount of suspicious activity so far and/or faction (with
	 * town and BM. BM faction implies skill, so improves your chances)
	 * Item weight and cost should figure in, and rogueish classes (TODO) should have a
	 * better chance - at least, at higher level.
	 */
	difficulty = 12;
	difficulty += sqrt(dummy->number * obj->weight);
	difficulty += sqrt(dummy->number * price);

	/* Negative town faction = suspect = higher difficulty, unlimited.
	 * Positive = trusted = lower, but to a limit.
	 * This is intentionally applying a combination of town and BM faction for all stores, including BM and Salon.
	 * Salon and BM factions only apply when they are negative (i.e. you have stolen from them).
	 */
	int town_faction = player->town_faction + MIN(0, player->bm_faction) + (MIN(0, player->cyber_faction) / 100);
	if (town_faction < 0)
		difficulty *= 1 - town_faction;
	else if (town_faction > 0)
		difficulty *= 0.5 + (0.5 / ((double)town_faction + 1));

	/* Negative bm faction doesn't hurt, as this is representing skill.
	 * Positive improves difficulty, halving it with the first step and making smaller imporovements later.
	 */
	int bm_faction = player->bm_faction;
	if (bm_faction > 0)
		difficulty *= (1.5 / ((double)bm_faction + 2));

	/* Dexterity is always the most important point, though */
	double dex_difficulty = difficulty / (player->state.stat_ind[STAT_DEX] + 3);
	double chr_difficulty = difficulty / (player->state.stat_ind[STAT_CHR] + 3);

	/* Chance of dex failure (0..1) */
	double dex_chance = dex_difficulty / ((double)(z_info->theft_dex) + dex_difficulty);

	/* Chance of chr failure (0..1) */
	double chr_chance = chr_difficulty / ((double)(z_info->theft_chr) + chr_difficulty);

	/* Show price and difficulty (not (entirely) numerically - and not at all unless roguelike, experienced, wizard?)
	 * A percentage (or more than one, as there are more than one result) would be more
	 * transparent.
	 **/

	/* Confirm stealing */
	ctx->flags |= STORE_SHOW_STEAL | STORE_INIT_CHANGE | STORE_FRAME_CHANGE;
	store_display_recalc(ctx);
	store_redraw(ctx);

	prt(format("Price: $%d, Fail %.1lf%%, Item %.1f%%, Ban %.1f%%, Fight %.1f%%",
		price,
		dex_chance * 100.0,	/* Fail: one dex check fails */
		(1.0 - (dex_chance * dex_chance)) * 100.0, /* Item: either of two dex checks succeed */
		/* Ban: Fail first dex check, minus
		 * 		(Failed first, failed second dex, passed chr check).
		 **/
		(dex_chance - (dex_chance * dex_chance * (1.0 - chr_chance))) * 100.0,
		/* Fight: Fail 3 dex checks and 2 chr checks. Round up as it's the only one likely to be that low,
		 * and don't want to imply that the chance is ever actually zero.
		 **/
		MAX(0.1, (dex_chance * dex_chance * chr_chance * dex_chance * chr_chance * 100.0))),
		1, 0);

	int response = store_get_check(format("Steal %s? [ESC, any other key accepts]", o_name));
	screen_load();

	/* Negative response, so give up */
	if (!response) return;

	/*
	 * Make a dex check. If successful, you got away with the item, without being noticed.
	 * Otherwise, make a second dex check - if successful, you still got away but were noticed.
	 * If not, then you were caught and don't get the item, but may be able to talk your way
	 * out of it.
	 * So make a chr check to not be "noticed" (that is, no bad effects).
	 * If this fails, you lose faction etc. and are kickbanned.
	 * If either a chr or dex check passes, that's all.
	 * If both fail though - FIGHT!
	 */
	bool get_item = true;
	bool get_ban = false;
	bool get_faction = false;
	bool get_fight = false;
	const char *tmsg = "You successfully pull off the theft.";

	if (Rand_double(1.0) < dex_chance) {
		/* Failed to avoid notice */
		get_ban = get_faction = true;
		tmsg = "You are caught but manage to flee, with the item.";
		if (Rand_double(1.0) < dex_chance) {
			/* Also failed to get away */
			get_item = false;
			if (Rand_double(1.0) < chr_chance) {
				/* And failed to convince */
				if (Rand_double(1.0) < dex_chance) {
					if (Rand_double(1.0) < chr_chance) {
						get_faction = false;	// because fighting does that
						get_fight = true;
						tmsg = "You are caught, and %s grabs you. Uh-oh...";
					} else {
						tmsg = "You are caught, but talk your way into being let go.";
					}
				} else {
					tmsg = "You are caught, but leg it before %s can grab you.";
				}
			} else {
				/* Nah not a thief, really */
				get_item = get_ban = get_faction = false;
				tmsg = "You are caught, but manage to convince %s that it was unintentional.";
			}
		}
	}

	/* Clear all current messages */
	msg_flag = false;
	prt("", 0, 0);
	msgt(MSG_FLEE, tmsg, store_shortname(ctx));

	if (get_item) {
		// make item '100% off'
		of_on(obj->flags, OF_STOLEN);
		obj->origin = ORIGIN_STOLEN;

		// and buy it for 0
		cmdq_push(CMD_BUY);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
		cmd_set_arg_number(cmdq_peek(), "steal", 1);
	}

	if (get_faction)
		change_faction(store, -1);

	if (get_ban) {
		store->bandays = randint1(7 * -(MIN(player->town_faction, -1)));
		store->banreason = strdup("Get out, thief!");
		*exit = true;
	}

	if (get_fight) {
		/* Fight! */
		*exit = true;
		store_do_fight(ctx);
	}

	object_delete(NULL, NULL, &dummy);
}

/* pick a fight with the owner */
static void store_fight(struct store_context *ctx, bool *exit)
{
	if ((ctx->store->sidx == STORE_HOME) || (you_own(ctx->store))) {
		msg("You decide against punching yourself in the face.");
		return;
	}

	if ((ctx->store->sidx == STORE_HQ) || (ctx->store->sidx == STORE_AIR)) {
		msg("You decide that would be a bad move.");
		return;
	}

	/* Confirm as it is risky */
	screen_save();
	int response = store_get_check(format("Sure you want to attack %s? [ESC, any other key to accept]", store_shortname(ctx)));
	screen_load();

	/* Negative response, so give up */
	if (!response) return;

	/* Fight! */
	*exit = true;
	store_do_fight(ctx);
}

/* Sum of sale prices of all items */
static int store_price_all(struct store *s)
{
	int cost = 0;
	struct object *o = s->stock;
	while (o) {
		cost += price_item(s, o, false, o->number);
		o = o->next;
	}
	return cost;
}

/* obtain new stock */
static void store_newstock(struct store_context *ctx)
{
	if (ctx->store->sidx == STORE_HOME) {
		msg("Unfortunately it's not that easy.");
		return;
	}

	if (ctx->store->sidx == STORE_HQ) {
		msg("What you see is what you get.");
		return;
	}

	if (ctx->store->sidx == STORE_AIR) {
		msg("We only run the daily scheduled flights you see.");
		return;
	}

	int price = store_roundup((store_price_all(ctx->store) / 4) + (ctx->store->owner->max_cost / 20) + 200);

	/* Confirm if they really wanted it */
	screen_save();
	int response = store_get_check(format("Re-stocking will cost you $%d. Go ahead? [ESC, any other key to accept]", price));
	screen_load();

	/* Negative response, so give up */
	if (!response) return;

	/* Check if the player can afford it */
	if ((int)player->au < price) {
		msg("You do not have enough money.");
		return;
	}

	/* Pay */
	player->au -= price;
	msg("%s gets out new stock.", store_shortname(ctx));

	/* Remove old stock */
	struct store *s = ctx->store;
	s->stock_num = 0;
	object_pile_free(NULL, NULL, s->stock);
	s->stock = NULL;
	
	/* Get new stock */
	for (int j = 0; j < 10; j++)
		store_maint(s);

	/* Update the display */
	event_signal(EVENT_STORECHANGED);
}

/* replace by a different shopkeeper */
static void store_replace(struct store_context *ctx, bool *exit)
{
	char buf[256];
	struct store *store = ctx->store;

	int price = store_roundup((store_price_all(store) / 3) + store->owner->max_cost + 2000);
	if ((store->sidx == STORE_HOME) || (you_own(store))) {
		/* Could treat this a request to sell */
		msg("It's all yours, and it's staying that way.");
		return;
	}

	if (ctx->store->sidx == STORE_HQ) {
		msg("You decide that would be a bad move.");
		return;
	}

	if (ctx->store->sidx == STORE_AIR) {
		msg("The cashier doesn't have any way to do that.");
		return;
	}

	/* Confirm if they really wanted it */
	screen_save();
	int response = store_get_check(format("Retiring will cost you $%d. Go ahead? [ESC, any other key to accept]", price));
	screen_load();

	/* Negative response, so give up */
	if (!response) return;

	/* Check if the player can afford it */
	if ((int)player->au < price) {
		msg("You do not have enough money.");
		return;
	}

	/* Pay */
	player->au -= price;
	strcpy(buf, store_shortname(ctx));

	/* Shuffle it */
	store_shuffle(ctx->store);
	store_maint(ctx->store);
	msg("%s agrees to sell the store to %s.", buf, store_shortname(ctx));

	/* Close and kick you */
	*exit = true;
	ctx->store->bandays = 1;
	ctx->store->banreason = strdup("This store is under new management. It will re-open tomorrow at dawn.");

	/* Update the display */
	event_signal(EVENT_STORECHANGED);
}

static void store_do_buy(struct store *store)
{
	store_your_name(store);

	store->bandays = 0;
	store->layaway_idx = -1;
	store->income = 0;

	/* Update the display */
	event_signal(EVENT_STORECHANGED);
}

static void store_do_sell(struct store *store)
{
	store->bandays = 0;
	store->layaway_idx = -1;
	store->income = 0;

	/* Update the display */
	event_signal(EVENT_STORECHANGED);
}

/* A store is marked 'to destroy' with the stores[x].destroy flags at the same time
 * player->danger increases, when the store's 'danger' field equals player->danger.
 * But it is not actually destroyed until the next time the town is entered - at
 * which point the destroy flag is cleared.
 * So the number of destroyed stores is the number of "destroyed or pending destruction"
 * stores (danger <= player->danger), minus the number which have not yet been
 * destroyed (and so have the destroy flag set).
 */
static int stores_destroyed(void)
{
	int n = 0;
	for(int i=0;i<MAX_STORES;i++) {
		if ((stores[i].max_danger <= player->danger) && (!(stores[i].destroy)))
			n++;
	}
	return n;
}

/* buys the store for yourself */
static void store_buy(struct store_context *ctx, bool *exit)
{
	struct store *store = ctx->store;
	if (ctx->store->sidx == STORE_HQ) {
		msg("You decide that would be a bad move.");
		return;
	}

	if (ctx->store->sidx == STORE_AIR) {
		msg("There isn't any way to do that.");
		return;
	}

	if (store->sidx == STORE_HOME) {
		msg("It's all yours, and it's staying that way.");
		return;
	} else if (you_own(store)) {
		/* Already own it.
		 * Sell?
		 * To avoid being able to make money from speculating here - the price of stock (which can change) is ignored.
		 * The previous owner's max_cost isn't known so must also be ignored.
		 * This gives a flat price which if always below $100k will always be less than the price bought for.
		 * It could change with CHR, level, etc. though.
		 * It could also be entirely random (as it is now, below), so long as it only changes daily not on every request.
		 * 
		 * It should also always be more expensive to buy and then sell, than it would be to ask for a reshuffle (unless the previous
		 * owner was stored) - for this to be always the case:
		 * 		The maximum value (4 s.d.s above the mean) of the Rand_normal below must be above the constant in the buy price.
		 * 		The multiplier in the owner->max_cost term of the buy price must be at least as high as the one in store_replace.
		 * 		The divisor in the store_price_all() term of the buy price must be at least as low as the one in store_replace.
		 **/
		rng_state state;
		Rand_extract_state(&state);
		int day = turn / (10L * z_info->day_length);
		Rand_state_init((day * 53) + store->sidx);
		int price = 100 * Rand_normal(650, 80);	/* 4 s.d.s - the max - will give 97000 - below the 100K limit */
		Rand_restore_state(&state);
		price >>= stores_destroyed();

		/* Confirm if they really wanted it */
		screen_save();
		int response = store_get_check(format("Selling the store will get you $%d. Go ahead? [ESC, any other key to accept]", price));
		screen_load();

		/* Negative response, so give up */
		if (!response) return;

		/* Get price, deal with faction */
		player->au += price;
		if ((store->sidx != STORE_CYBER) && (store->sidx != STORE_B_MARKET)) {
			player->stores_owned--;
			if (player->stores_owned == 0)
				player->town_faction--;
		}

		/* Buy it */
		store_shuffle(store);
		store_maint(store);
		msg("%s agrees to buy the store from you.\n\n", store_shortname(ctx));
		store_do_sell(store);

		*exit = true;
		return;
	}

	/* Buy price. See selling discussion above before changing */
	int price = store_roundup((store_price_all(store) / 3) + (store->owner->max_cost * 3) + 100000);
	
	/* Confirm if they really wanted it */
	screen_save();
	int response = store_get_check(format("Buying the store will cost you $%d. Go ahead? [ESC, any other key to accept]", price));
	screen_load();

	/* Negative response, so give up */
	if (!response) return;

	/* Check if the player can afford it */
	if ((int)player->au < price) {
		msg("You do not have enough money.");
		return;
	}

	/* Pay, deal with faction */
	player->au -= price;
	if ((store->sidx != STORE_CYBER) && (store->sidx != STORE_B_MARKET)) {
		if (player->stores_owned == 0)
			player->town_faction++;
		player->stores_owned++;
	}

	/* Buy it */
	msg("%s agrees to sell the store to you.", store_shortname(ctx));
	store_do_buy(ctx->store);
	*exit = true;
}

static int locate_get(int min, int max, int feature, int xc, int yc, int *xout, int *yout, int total)
{
	int count = 0;
	if (xout)
		*xout = 0;
	if (yout)
		*yout = 0;

	/* To allow squared distances in the loop */
	if (min >= 0)
		min *= min;
	if (max >= 0)
		max *= max;

	/* Scan the level */
	for(int y=1; y<cave->height-1; y++) {
		for(int x=1; x<cave->width-1; x++) {
			/* Is it floor? */
			if (square(cave, loc(x, y))->feat != FEAT_FLOOR)
				continue;
			/* Is it in bounds? */
			int distance = ((x-xc)*(x-xc)) + ((y-yc) * (y-yc));
			if (((min >= 0) && (distance < min)) || ((max >= 0) && (distance > max)))
				continue;
			/* Is there a nearby feature? */
			if (feature != FEAT_NONE) {
				if ((square(cave, loc(x-1, y-1))->feat != feature) &&
					(square(cave, loc(x+1, y-1))->feat != feature) &&
					(square(cave, loc(x-1, y))->feat != feature) &&
					(square(cave, loc(x+1, y))->feat != feature) &&
					(square(cave, loc(x-1, y+1))->feat != feature) &&
					(square(cave, loc(x+1, y+1))->feat != feature) &&
					(square(cave, loc(x, y-1))->feat != feature) &&
					(square(cave, loc(x, y+1))->feat != feature))
						continue;
			}

			/* It's OK - increase the count, and return if this is asked for */
			if (xout) {
				if (count == total) {
					*xout = x;
					*yout = y;
					return total;
				}
			}
			count++;
		}
	}
	return count;
}

/** Find a place to put a town quest.
 * It should be
 * - on floor
 * - near a given tile (granite for the edge, lava, permanite for a shop - FEAT_NONE if you dont care)
 * - possibly close to the store (give a min and max distance, either can be -1 if unwanted)
 * - selected at random from grids matching
 * - placed in an emergency position if none match?
 */
static void locate_quest(struct quest *q)
{
	int xout, yout;
	
	/* Center position is the current player postion */
	int x = player->grid.x;
	int y = player->grid.y;

	/* Min/max and feature from the quest */
	int min = q->entry_min;
	int max = q->entry_max;
	int feature = q->entry_feature;

	/* Try as asked for */
	int grids = locate_get(min, max, feature, x, y, NULL, NULL, 0);
	/* If not, ignore the min/max distance */
	if (!grids) {
		max = -1;
		min = -1;
		grids = locate_get(min, max, feature, x, y, NULL, NULL, 0);
	}
	/* If not, ignore the feature */
	if (!grids) {
		feature = FEAT_NONE;
		grids = locate_get(min, max, feature, x, y, NULL, NULL, 0);
	}
	/* Shouldn't happen - just put it next to the questgiver. */
	if (!grids) {
		q->x = x;
		if (y > cave->height / 2)
			y--;
		else
			y++;
		q->y = y;
		return;
	}
	/* Select at random */ 
	locate_get(min, max, feature, x, y, &xout, &yout, randint0(grids));
	q->x = xout;
	q->y = yout;
}

/* Is there anything that can be done for an active quest?
 * Returns 0 if there is not (no msg)
 * 1 "Your task is still in progress" / no msg
 * 2 "Your task is complete. Press 'Q' to discuss it." / no msg?
 * 3 "You can press 'Q' to ask about a task." / "I've got something that needs doing - could you help?" (etc).
 * 
 * Of these, 0 means that no quest has anything (so there can be no conflicts)
 * 1 means that a task is in progress
 * 2 means that a task is complete (ignoring whether any are in progress)
 * 3 means that a task is available.
 * 
 * This is the same priority as implicitly in store_quest(), and the order of quests
 * as there takes priority over 1/2/3. The assumption in both places is that having
 * more than one quest in any of these states at the same time shouldn't happen.
 */
static int store_quest_status(struct store *store)
{
	if ((store->sidx == STORE_HOME) || (you_own(store)))
		return 0;

	if (store->sidx == STORE_AIR)
		return 0;

	quest_changed_level(true);

	// Scan the quests looking for a quest which is 'available' and based from this store.
	for(int i=0;i<z_info->quest_max;i++)
	{
		struct quest *q = &player->quests[i];
		if (((q->town < 0) || (q->town == (player->town - t_info))) && (q->store == (int)store->sidx) && (!(q->flags & QF_LOCKED))) {
			if (!(q->flags & (QF_ACTIVE | QF_FAILED | QF_SUCCEEDED | QF_UNREWARDED))) {
				return 3;
			} else if (q->flags & QF_UNREWARDED) {
				return 2;
			} else if (q->flags & QF_ACTIVE) {
				return 1;
			}
		}
	}

	return 0;
}

/* Asks the owner if there is anything they want doing */
static void store_quest(struct store_context *ctx)
{
	struct store *store = ctx->store;
	if ((store->sidx == STORE_HOME) || (you_own(store))) {
		// not if it's you? May depend on the quest - some may make sense if differently worded, or should move to another store.
		msg("You question yourself extensively, but see no gain in WIS.");
		return;
	}

	// The std message is misleading, given that it's not in the help
	if (ctx->store->sidx == STORE_AIR) {
		msg("There isn't anything that needs to be done here.");
		return;
	}

	// In case of e.g. Pie quest's card
	quest_changed_level(true);

	int progress = 0;
	int progi = -1;
	// Scan the quests looking for a quest which is 'available' and based from this store.
	for(int i=0;i<z_info->quest_max;i++)
	{
		struct quest *q = &player->quests[i];
		if (((q->town < 0) || (q->town == (player->town - t_info))) && (q->store == (int)store->sidx) && (!(q->flags & QF_LOCKED))) {
			if (streq(q->name, "Arena") && (q->flags & (QF_ACTIVE))) {
				quest_play_arena(player);
				return;
			} else {
				if (!(q->flags & (QF_ACTIVE | QF_FAILED | QF_SUCCEEDED | QF_UNREWARDED))) {
					/* Take new quest - ask first */
					screen_save();
					int response = store_get_long_check(ctx, quest_get_intro(q));
					screen_load();
					if (response) {
						/* Accepted TODO message */
						q->flags |= QF_ACTIVE;
						locate_quest(q);

						/* Create a stairway */
						if ((!(q->flags & QF_TOWN)) && (!(streq(q->name, "Hit List"))))
							square_set_feat(cave, loc(q->x,q->y), FEAT_ENTRY);
						return;
					}
				} else if (q->flags & QF_UNREWARDED) {
					/* Debrief */
					if (q->flags & QF_SUCCEEDED) {
						/* There may be an additional check (such as carrying the right object)
						 * before you can get a reward. This will print messages itself if needed.
						 **/
						if (quest_is_rewardable(q)) {
							ui_text_box(q->succeed);
							quest_reward(q, true, ctx);
							/* Unlock quests depending on this */
							if (q->unlock) {
								for(int j=0;j<z_info->quest_max;j++)
								{
									if (streq(player->quests[j].name, q->unlock))
										player->quests[j].flags &= ~QF_LOCKED;
								}
							}
							ctx->flags |= STORE_GOLD_CHANGE;
							ctx->flags |= STORE_FRAME_CHANGE;
							event_signal(EVENT_STORECHANGED);
						}
					} else {
						ui_text_box(q->failure);
						quest_reward(q, false, ctx);
					}
					return;
				} else if (q->flags & QF_ACTIVE) {
					progi = i;
					progress++;
				}
			}
		}
	}

	if (progress) {
		/* Still in progress */
		if (progress > 1)
			msg("Your tasks are still in progress.");
		else
			msg("Your task '%s' is still in progress.", player->quests[progi]);
		return;
	}
 
	/* Special cases */
	if (quest_special_endings(ctx))
		return;

	msg("%s doesn't have anything that needs to be done right now.", store_shortname(ctx));
}

/* Select an item from a store's stock.
 * Returns -1 if cancelled or a non-negative index into the store's stock array if an item is selected.
 * Items available are filtered through the predicate if this is non-NULL.
 */
static int store_select_item(bool (*pred)(struct store *, struct object *))
{
	return -1;
}

/* Returns true if an item is suitable for layaway */
static bool select_layaway(struct store *store, struct object *obj)
{
	return true;
}

/* Put an item on layaway. Pay 10% now */
static void store_hold(struct store_context *ctx)
{
	/* Clear all current messages */
	msg_flag = false;
	prt("", 0, 0);

	/* Check it's not the home or an owned store */
	struct store *store = ctx->store;
	if (store->sidx == STORE_HOME || (store->owner == &store->owners[0])) {
		msg("You pay 10% of the price of an item in your home from your left pocket into your right pocket. A real bargain!");
		return;
	}
	
	/* Confirm that they really wanted to lose the previous one */
	if (store->layaway_idx >= 0) {
		screen_save();
		msg("You already have a held item. Switching will lose your 10% deposit.");
		int response = store_get_check(format("Select a new item to hold? [ESC, any other key to accept]"));
		screen_load();

		/* Negative response, so give up */
		if (!response) return;
		
		/* as it is possible to not select a new item */
		store->layaway_idx = -1;
	}

	/* Select an item */
	msg("Please select an item to hold for you (costing 10% of the item's price)");
	int o_idx = store_select_item(select_layaway);
	if (o_idx < 0) {
		// no valid selection was made
		return;
	}

	/* Selected this item from stock */
	struct object *obj = &store->stock[o_idx];
	int32_t price = (int)price_item(store, obj, false, obj->number);	/* assume that all are wanted */
	price = (price + 9) / 10;
	if (price <= 0) {
		msg("It's already free, and you want it cheaper?!");
		return;
	}

	/* And hold the item */
	store->layaway_idx = o_idx;
	store->layaway_day = turn / (10L * z_info->day_length);		/* yay */

	/* Pay and update the display */
	player->au -= price;
	ctx->flags |= STORE_GOLD_CHANGE;
	event_signal(EVENT_STORECHANGED);
}


/**
 * Examine an item in a store
 */
static void store_examine(struct store_context *ctx, int item)
{
	struct object *obj;
	char header[120];
	textblock *tb;
	region area = { 0, 0, 0, 0 };
	uint32_t odesc_flags = ODESC_PREFIX | ODESC_FULL;

	if (item < 0) return;

	if (ctx->store->sidx == STORE_AIR) {
		tb = textblock_new();
		if (item < world_connections(player->town)) {
			sprintf(header, "Fly to %s!", player->town->connect[item]->name);
			textblock_append(tb, world_describe_town(player->town->connect[item]));
			const char *name = NULL;
			{
				rng_state state;
				Rand_extract_state(&state);
				Rand_state_init(world_town_seed ^ (player->town - t_info));
				name = random_line(firstnames);
				Rand_restore_state(&state);
			}
			/* How difficult is it? The Fortress might be a special case, and might also want to look at
			 * recall depth / quest monster being dead.
			 **/
			struct quest *quest = quest_guardian_any(player->town->connect[item]);
			if (quest) {
				const char *desc = NULL;
				textblock_append_c(tb, COLOUR_YELLOW, "  %s adds: ", name);
				if (player->orbitable) {
					desc = "Feel free, but wouldn't you like to be a real traveller? Orbit awaits!";
				} else {
					if (player->town->connect[item]->recall_depth) {
						if (player->town->connect[item] == t_info) {
							/* Fortress */
							desc = "Yep, that's where you've got to be in the end, after all!";
						} else {
							/* Minor dungeon */
							desc = "Sure, why not make another attempt? Never give up!";
							if (quest->flags & QF_SUCCEEDED) {
								if (streq(quest->name, "Stores")) {
									/* Your starting town */
									desc = "Going home again for a while? No problem.";
								} else {
									desc = "Great, I'm sure they'll be glad to see you again there!";
								}
							}
						}
					} else {
						char dungeon[80];
						my_strcpy(dungeon, player->town->connect[item]->downto, sizeof(dungeon));
						char *s = strchr(dungeon, ' ');
						if (s)
							*s = 0;
						int difficulty = dungeon_top_level(dungeon);
						int power = MAX(player->max_depth, (player->max_lev * 5) / 8);
						for (int i=0;i<z_info->quest_max;i++) {
							if (player->quests[i].flags & QF_SUCCEEDED) {
								power = MAX(player->quests[i].level, power);
							}
						}

						difficulty -= power;
						
						if (difficulty < 0)
							desc = "You should have no trouble at all there - really, a relaxing stroll!";
						else if (difficulty < 5)
							desc = "A recommended next destination. Just right for you - go for it!";
						else if (difficulty < 10)
							desc = "Definitely a bit of a challenge, but then - hey, why not?";
						else if (difficulty < 20)
							desc = "That's a stretch - seriously chancy. But maybe you could still pull it off?";
						else
							desc = "I admire your courage, but I'd really not recommend going there just yet!";
					}
				}
				textblock_append_c(tb, COLOUR_GREEN, desc);
			}
		} else {
			sprintf(header, "The Orbital Station!");
			textblock_append(tb, "Yes, you too can be an astronaut! See the world roll away below you from 300km up from the Xygos Orbital Station. Flights are available every morning for a very reasonable price!");
		}
	} else {
		/* Get the actual object */
		obj = ctx->list[item];

		/* Items in the home get less description */
		if (ctx->store->sidx == STORE_HOME) {
			odesc_flags |= ODESC_CAPITAL;
		} else {
			odesc_flags |= ODESC_STORE;
		}

		/* Hack -- no flush needed */
		msg_flag = false;

		/* Show full info in most stores, but normal info in player home */
		tb = object_info(obj, OINFO_NONE);
		object_desc(header, sizeof(header), obj, odesc_flags, player);
	}

	textui_textblock_show(tb, area, header);
	textblock_free(tb);
}

static void store_menu_set_selections(struct menu *menu, bool knowledge_menu)
{
	if (knowledge_menu) {
		if (OPT(player, rogue_like_commands)) {
			/* These two can't intersect! */
			menu->cmd_keys = "?|IeilxFQSNOB";
			menu->selections = "abcdfghjkmnopqrstuvwyz134567";
		} else {
			/* These two can't intersect! */
			menu->cmd_keys = "?|IeilFQSNOB";
			menu->selections = "abcdfghjkmnopqrstuvwxyz13456";
		}
	} else {
		if (OPT(player, rogue_like_commands)) {
			/* These two can't intersect! */
			menu->cmd_keys = "\x04\x05\x10?={|}~CEIPTdegilpswxFQSNOB"; /* \x10 = ^p , \x04 = ^D, \x05 = ^E */
			menu->selections = "abcfmnoqrtuvyz13456790ADGH";
		} else {
			/* These two can't intersect! */
			menu->cmd_keys = "\x05\x010?={|}~CEIbdegiklpstwxFQSNOB"; /* \x05 = ^E, \x10 = ^p */
			menu->selections = "acfhjmnoqruvyz13456790ADGH";
		}
	}
}

static void store_menu_recalc(struct menu *m)
{
	struct store_context *ctx = menu_priv(m);
	int entries = ctx->store->stock_num;
	if (ctx->store->sidx == STORE_AIR) {
		entries = world_connections(player->town);
		if (player->orbitable)
			entries++;
	}
	menu_setpriv(m, entries, ctx);
}

/**
 * Process a command in a store
 *
 * Note that we must allow the use of a few "special" commands in the stores
 * which are not allowed in the dungeon, and we must disable some commands
 * which are allowed in the dungeon but not in the stores, to prevent chaos.
 */
static bool store_process_command_key(struct keypress kp)
{
	int cmd = 0;

	/* Hack -- no flush needed */
	prt("", 0, 0);
	msg_flag = false;

	/* Process the keycode */
	switch (kp.code) {
		case 'T': /* roguelike */
		case 't': cmd = CMD_TAKEOFF; break;

		case KTRL('D'): /* roguelike */
		case 'k': textui_cmd_ignore(); break;

		case 'P': /* roguelike */
		case 'b': textui_spell_browse(); break;

		case '~': textui_browse_knowledge(); break;
		case 'I': textui_obj_examine(); break;
		case 'w': cmd = CMD_WIELD; break;
		case '{': cmd = CMD_INSCRIBE; break;
		case '}': cmd = CMD_UNINSCRIBE; break;

		case 'e': do_cmd_equip(); break;
		case 'i': do_cmd_inven(); break;
		case '|': do_cmd_quiver(); break;
		case KTRL('E'): toggle_inven_equip(); break;
		case 'C': do_cmd_change_name(); break;
		case KTRL('P'): do_cmd_messages(); break;
		case ')': do_cmd_save_screen(); break;

		default: return false;
	}

	if (cmd)
		cmdq_push_repeat(cmd, 0);

	return true;
}

/**
 * Select an item from the store's stock, and return the stock index
 */
static int store_get_stock(struct menu *m, int oid, bool sel)
{
	ui_event e;
	int no_act = m->flags & MN_NO_ACTION;

	/* Set a flag to make sure that we get the selection or escape
	 * without running the menu handler */
	m->flags |= MN_NO_ACTION;
	e = menu_select(m, 0, sel);
	if (!no_act) {
		m->flags &= ~MN_NO_ACTION;
	}

	if (e.type == EVT_SELECT) {
		return m->cursor;
	} else if (e.type == EVT_ESCAPE) {
		return -1;
	}

	/* if we do not have a new selection, just return the original item */
	return oid;
}

/** Enum for context menu entries */
enum {
	ACT_INSPECT_INVEN,
	ACT_SELL,
	ACT_EXAMINE,
	ACT_BUY,
	ACT_BUY_ONE,
	ACT_EXIT
};

/* pick the context menu options appropriate for a store */
static int context_menu_store(struct store_context *ctx, const int oid, int mx, int my)
{
	struct store *store = ctx->store;
	bool home = (store->sidx == STORE_HOME) ? true : false;

	struct menu *m = menu_dynamic_new();

	int selected;
	char *labels = string_make(lower_case);
	m->selections = labels;

	menu_dynamic_add_label(m, "Inspect inventory", 'I', ACT_INSPECT_INVEN, labels);
	if (!ctx->inspect_only) {
		menu_dynamic_add_label(m, home ? "Stash" : "Sell", 'd',
			ACT_SELL, labels);
	}
	menu_dynamic_add_label(m, "Exit", '`', ACT_EXIT, labels);

	/* Hack -- no flush needed */
	msg_flag = false;
	screen_save();

	menu_dynamic_calc_location(m, mx, my);
	region_erase_bordered(&m->boundary);

	prt("(Enter to select, ESC) Command:", 0, 0);
	selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	string_free(labels);

	screen_load();

	switch (selected) {
		case ACT_SELL:
			store_sell(ctx);
			break;
		case ACT_INSPECT_INVEN:
			textui_obj_examine();
			break;
		case ACT_EXIT:
			return false;
	}

	return true;
}

/* pick the context menu options appropriate for an item available in a store */
static void context_menu_store_item(struct store_context *ctx, const int oid, int mx, int my)
{
	struct store *store = ctx->store;
	bool home = (store->sidx == STORE_HOME) ? true : false;

	struct menu *m = menu_dynamic_new();
	struct object *obj = ctx->list[oid];

	int selected;
	char *labels;
	char header[120];

	object_desc(header, sizeof(header), obj,
		ODESC_PREFIX | ODESC_FULL | ODESC_STORE, player);

	labels = string_make(lower_case);
	m->selections = labels;

	menu_dynamic_add_label(m, "Examine", 'x', ACT_EXAMINE, labels);
	if (!ctx->inspect_only) {
		menu_dynamic_add_label(m, home ? "Take" : "Buy", 'd',
			ACT_SELL, labels);
		if (obj->number > 1) {
			menu_dynamic_add_label(m, home ? "Take one" : "Buy one",
				'o', ACT_BUY_ONE, labels);
		}
	}

	/* Hack -- no flush needed */
	msg_flag = false;
	screen_save();

	menu_dynamic_calc_location(m, mx, my);
	region_erase_bordered(&m->boundary);

	prt(format("(Enter to select, ESC) Command for %s:", header), 0, 0);
	selected = menu_dynamic_select(m);

	menu_dynamic_free(m);
	string_free(labels);

	screen_load();

	bool exit = false;
	switch (selected) {
		case ACT_EXAMINE:
			store_examine(ctx, oid);
			break;
		case ACT_BUY:
			store_purchase(ctx, oid, false, &exit);
			break;
		case ACT_BUY_ONE:
			store_purchase(ctx, oid, true, &exit);
			break;
	}
}

/**
 * Handle store menu input
 */
static bool store_menu_handle(struct menu *m, const ui_event *event, int oid, bool *exit)
{
	bool processed = true;
	struct store_context *ctx = menu_priv(m);
	struct store *store = ctx->store;

	if (event->type == EVT_SELECT) {
		/* Nothing for now, except "handle" the event */
		return true;
		/* In future, maybe we want a display a list of what you can do. */
	} else if (event->type == EVT_MOUSE) {
		if (event->mouse.button == 2) {
			/* exit the store? what already does this? menu_handle_mouse
			 * so exit this so that menu_handle_mouse will be called */
			return false;
		} else if (event->mouse.button == 1) {
			bool action = false;
			if ((event->mouse.y == 0) || (event->mouse.y == 1)) {
				/* show the store context menu */
				if (context_menu_store(ctx, oid, event->mouse.x, event->mouse.y) == false)
					return false;

				action = true;
			} else if ((oid >= 0) && (event->mouse.y == m->active.row + oid)) {
				/* if press is on a list item, so store item context */
				context_menu_store_item(ctx, oid, event->mouse.x,
										event->mouse.y);
				action = true;
			}

			if (action) {
				ctx->flags |= (STORE_FRAME_CHANGE | STORE_GOLD_CHANGE);

				/* Let the game handle any core commands (equipping, etc) */
				cmdq_pop(CTX_STORE);

				/* Notice and handle stuff */
				notice_stuff(player);
				handle_stuff(player);

				/* Display the store */
				store_display_recalc(ctx);
				store_menu_recalc(m);
				store_redraw(ctx);

				return true;
			}
		}
	} else if (event->type == EVT_KBRD) {
		switch (event->key.code) {
			case 's':
			case 'd': store_sell(ctx); break;

			case 'S':
				store_steal(ctx, oid, m, exit);
				break;

			case 'F': store_fight(ctx, exit); break;

			case 'N': store_newstock(ctx); break;

			case 'O': store_replace(ctx, exit); break;

			case 'B': store_buy(ctx, exit); break;

			case 'H': store_hold(ctx); break;

			case 'Q': store_quest(ctx); break;

			case 'p':
			case 'g':
				/* use the old way of purchasing items */
				msg_flag = false;
				if (store->sidx != STORE_HOME) {
					if (store->sidx == STORE_AIR)
						prt("Purchase which ticket? (ESC to cancel, Enter to select)", 0, 0);
					else
						prt("Purchase which item? (ESC to cancel, Enter to select)", 0, 0);
				} else {
					prt("Get which item? (Esc to cancel, Enter to select)",
						0, 0);
				}
				oid = store_get_stock(m, oid, true);
				prt("", 0, 0);
				if (oid >= 0) {
					store_purchase(ctx, oid, false, exit);
				}
				break;
			case 'l':
			case 'x':
				/* use the old way of examining items */
				msg_flag = false;
				prt(format("Examine which %s? (ESC to cancel, Enter to select)", (store->sidx == STORE_AIR) ? "ticket" : "item"),
					0, 0);
				oid = store_get_stock(m, oid, true);
				prt("", 0, 0);
				if (oid >= 0) {
					store_examine(ctx, oid);
				}
				break;

			case '?': {
				/* Toggle help */
				if (ctx->flags & STORE_SHOW_HELP)
					ctx->flags &= ~(STORE_SHOW_HELP);
				else
					ctx->flags |= STORE_SHOW_HELP;

				/* Redisplay */
				ctx->flags |= STORE_INIT_CHANGE;

				store_display_recalc(ctx);
				store_redraw(ctx);

				break;
			}

			case '=': {
				do_cmd_options();
				store_menu_set_selections(m, false);
				break;
			}

			default:
				processed = store_process_command_key(event->key);
		}

		/* Let the game handle any core commands (equipping, etc) */
		cmdq_pop(CTX_STORE);

		if (processed) {
			event_signal(EVENT_INVENTORY);
			event_signal(EVENT_EQUIPMENT);
		}

		/* Notice and handle stuff */
		notice_stuff(player);
		handle_stuff(player);

		return processed;
	}

	return false;
}

static region store_menu_region = { 1, 4, -1, -2 };
static const menu_iter store_menu =
{
	NULL,
	NULL,
	store_display_entry,
	store_menu_handle,
	NULL
};

/**
 * Init the store menu
 */
static void store_menu_init(struct store_context *ctx, struct store *store, bool inspect_only)
{
	struct menu *menu = &ctx->menu;

	ctx->store = store;
	ctx->flags = STORE_INIT_CHANGE;
	ctx->inspect_only = inspect_only;
	ctx->list = mem_zalloc(sizeof(struct object *) * z_info->store_inven_max);

	store_stock_list(ctx->store, ctx->list, z_info->store_inven_max);

	/* Init the menu structure */
	menu_init(menu, MN_SKIN_SCROLL, &store_menu);
	menu_setpriv(menu, 0, ctx);

	/* Calculate the positions of things and draw */
	menu_layout(menu, &store_menu_region);
	store_menu_set_selections(menu, inspect_only);
	store_display_recalc(ctx);
	store_menu_recalc(menu);
	store_redraw(ctx);
}

/**
 * Display contents of a store from knowledge menu
 *
 * The only allowed actions are 'I' to inspect an item
 */
void textui_store_knowledge(int n)
{
	struct store_context ctx;

	screen_save();
	clear_from(0);

	store_menu_init(&ctx, &stores[n], true);
	menu_select(&ctx.menu, 0, false);

	/* Flush messages XXX XXX XXX */
	event_signal(EVENT_MESSAGE_FLUSH);

	screen_load();

	mem_free(ctx.list);
}


/**
 * Handle stock change.
 */
static void refresh_stock(game_event_type type, game_event_data *unused, void *user)
{
	struct store_context *ctx = user;
	struct menu *menu = &ctx->menu;

	store_stock_list(ctx->store, ctx->list, z_info->store_inven_max);

	/* Display the store */
	store_display_recalc(ctx);
	store_menu_recalc(menu);
	store_redraw(ctx);
}

/**
 * Enter a store.
 */
void enter_store(game_event_type type, game_event_data *data, void *user)
{
	struct store *store = store_at(cave, player->grid);

	/* Check that we're on a store */
	if (!store) {
		msg("You see no store here.");
		return;
	}

	sound((store->sidx == STORE_HOME) ? MSG_STORE_HOME : MSG_STORE_ENTER);

	/* Shut down the normal game view */
	event_signal(EVENT_LEAVE_WORLD);
}

/**
 * Interact with a store.
 */
void use_store(game_event_type type, game_event_data *data, void *user)
{
	struct store *store = store_at(cave, player->grid);
	struct store_context ctx;

	/* Check that we're on a store */
	if (!store) return;

	/* Check for special handling */
	bool do_default = true;
	player_hook(building, store->sidx, true, &do_default);
	if (!do_default)
		return;

	/* Check whether it's blocked by a quest */
	if (quest_enter_building(store))
		return;

	/* Check that we aren't banned */
	if (store->bandays) {
		msg("%s", store->banreason);
		return;
	}

	/*** Display ***/

	/* Save current screen (ie. dungeon) */
	screen_save();
	msg_flag = false;

	/* Get a array version of the store stock, register handler for changes */
	event_add_handler(EVENT_STORECHANGED, refresh_stock, &ctx);

	/* Before initializing the menu, set up your name if you own it. */
	if (store->sidx != STORE_HOME)
		if (you_own(store))
			store_your_name(store);

	store_menu_init(&ctx, store, false);

	/* Say a friendly hello - or collect some loot. */
	if ((store->sidx != STORE_HOME) && (store->sidx != STORE_HQ) && (store->sidx != STORE_AIR)) {
		if (you_own(store)) {
			collect_from_store(store);
		} else {
			prt_welcome(store);
		}
	}

	/* Shopping */
	menu_select(&ctx.menu, 0, false);

	/* Shopping's done */
	event_remove_handler(EVENT_STORECHANGED, refresh_stock, &ctx);
	msg_flag = false;
	mem_free(ctx.list);

	/* Check for special handling */
	player_hook(building, store->sidx, false, NULL);

	/* Take a turn */
	player->upkeep->energy_use = z_info->move_energy;

	/* Flush messages */
	event_signal(EVENT_MESSAGE_FLUSH);

	/* Load the screen */
	screen_load();

	/* In case of e.g. triffid quest */
	quest_changed_level(true);
}

void leave_store(game_event_type type, game_event_data *data, void *user)
{
	/* Disable repeats */
	cmd_disable_repeat();

	sound(MSG_STORE_LEAVE);

	/* Switch back to the normal game view. */
	event_signal(EVENT_ENTER_WORLD);

	/* Update the visuals */
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);

	/* Redraw entire screen */
	player->upkeep->redraw |= (PR_BASIC | PR_EXTRA);

	/* Redraw map */
	player->upkeep->redraw |= (PR_MAP);
}

/** Fight complete
 * Determine the current store, make you the owner
 **/
void store_complete_fight(void)
{
	char buf[256];
	struct store *store = get_store_by_idx(player->current_store);
	strnfmt(buf, sizeof(buf), "You kick out %s! You are now the owner of this store.", store->owner->name);
	store_do_buy(store);
	ui_text_box(buf);
	dungeon_change_level(player, 0);
}

