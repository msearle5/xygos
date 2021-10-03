/**
 * \file ui-spell.c
 * \brief Spell UI handing
 *
 * Copyright (c) 2010 Andi Sidwell
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
#include "cmd-core.h"
#include "effects-info.h"
#include "game-input.h"
#include "game-world.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "ui-display.h"
#include "ui-menu.h"
#include "ui-output.h"
#include "ui-spell.h"


/**
 * Spell menu data struct
 */
struct spell_menu_data {
	bool (*is_valid)(const struct player *p, int spell_index);
	int *spells;
	struct class_book **books;
	int n_spells;
	int n_books;
	int selected_spell;
	int selected_book;
	bool browse;
	bool show_description;
};


/**
 * Is item oid valid?
 */
static int spell_menu_valid(struct menu *m, int oid)
{
	struct spell_menu_data *d = menu_priv(m);
	int *spells = d->spells;

	return (d->is_valid && oid < d->n_spells) ? d->is_valid(player, spells[oid]) : true;
}

/**
 * Display a row of the spell menu
 */
static void spell_menu_display(struct menu *m, int oid, bool cursor,
		int row, int col, int wid)
{
	struct spell_menu_data *d = menu_priv(m);
	char out[256];

	if (oid < d->n_spells) {
		int spell_index = d->spells[oid];
		const struct class_spell *spell = spell_by_index(player, spell_index);

		char help[30];

		int attr;
		const char *illegible = NULL;
		const char *comment = NULL;

		if (!spell) return;

		if (player->spell_flags[spell_index] & PY_SPELL_WORKED) {
			/* Get extra info */
			get_spell_info(spell_index, help, sizeof(help));
			comment = help;
			attr = COLOUR_WHITE;

		} else {
			comment = " untried";
			attr = COLOUR_L_GREEN;
		}

		/* Dump the spell --(--
		 * 'Cost' - what was once 'Mana' - may be:
		 * 	- (No cost)
		 *  X HP (HP cost, where X may be a fixed number, dice, or normal ("~50") and may change with level)
		 *  X t (Cooldown - X is as above. If the spell can't be cast because cooldown, change the colour and
		 * 			display the number of turns remaining.)
		 * If it has both display the HP (as more urgent) if it is available, the turns remaining otherwise.
		 **/
		char randval[30];
		*randval = 0;
		const char *tag = "";
		/* Cooldown in progress? */
		if (player->spell[spell_index].cooldown > 0) {
			attr = COLOUR_RED;
			snprintf(randval, sizeof(randval), "%d", player->spell[spell_index].cooldown);
		} else {
			if (randcalc(spell->hp, 0, AVERAGE) == 0) {
				/* Display cooldown */
				append_random_value_string(randval, sizeof(randval), &spell->turns);
			} else if (randcalc(spell->turns, 0, AVERAGE) == 0) {
				tag = "-";
			} else {
				/* Display HP */
				append_random_value_string(randval, sizeof(randval), &spell->hp);
				tag = " HP";
			}
		}
		strcat(randval, tag);

		strnfmt(out, sizeof(out), "%-26s%s %2d %8s %3d%%%s", spell->name,
				stat_names[spell->stat], spell->slevel, randval, spell_chance(spell_index), comment);
		c_prt(attr, illegible ? illegible : out, row, col);
	} else {
		/* It's a book */
		struct class_book *book = d->books[oid - d->n_spells];
		strnfmt(out, sizeof(out), "%s (", book->name);
		int space = MIN(wid, (int)sizeof(out)) - (strlen(out) + 5);
		int extra = 0;
		for(int i=0;i<book->num_spells;i++) {
			struct class_spell *spell = &book->spells[i];
			if ((strlen(spell->name) + 2) <= (size_t)space) {
				strcat(out, spell->name);
				strcat(out, ", ");
				space -= (strlen(spell->name) + 2);
			} else {
				extra++;
			}
		}
		char *close = out + strlen(out) - 2; /* remove the , */
		if (extra)
			strcpy(close, "...)");
		else
			strcpy(close, ")");
		c_prt(COLOUR_GREEN, out, row, col);
	}
}

/**
 * Handle an event on a menu row.
 */
static bool spell_menu_handler(struct menu *m, const ui_event *e, int oid, bool *exit)
{
	struct spell_menu_data *d = menu_priv(m);

	if (e->type == EVT_SELECT) {
		if (oid < d->n_spells) {
			d->selected_spell = d->spells[oid];
			return d->browse ? true : false;
		} else {
			assert(oid - d->n_spells < d->n_books);
			d->selected_book = d->books[oid - d->n_spells]->spells[0].bidx;
			return false;
		}
	}
	else if (e->type == EVT_KBRD) {
		if (e->key.code == '?') {
			d->show_description = !d->show_description;
		}
	}

	return false;
}

/**
 * Show spell long description when browsing
 */
static void spell_menu_browser(int oid, void *data, const region *loc)
{
	struct spell_menu_data *d = data;
	if (oid < d->n_spells) {
		int spell_index = d->spells[oid];
		const struct class_spell *spell = spell_by_index(player, spell_index);

		if (d->show_description) {
			/* Redirect output to the screen */
			text_out_hook = text_out_to_screen;
			text_out_wrap = 0;
			text_out_indent = loc->col - 1;
			text_out_pad = 1;

			Term_gotoxy(loc->col, loc->row + loc->page_rows);
			/* Spell description */
			text_out("\n%s", spell->text);

			/* To summarize average damage, count the damaging effects */
			int num_damaging = 0;
			for (struct effect *e = spell->effect; e != NULL; e = effect_next(e)) {
				if (effect_damages(e)) {
					num_damaging++;
				}
			}
			/* Now enumerate the effects' damage and type if not forgotten */
			if (num_damaging > 0
				&& (player->spell_flags[spell_index] & PY_SPELL_WORKED)
				&& !(player->spell_flags[spell_index] & PY_SPELL_FORGOTTEN)) {
				text_out("  Inflicts an average of");
				int i = 0;
				for (struct effect *e = spell->effect; e != NULL; e = effect_next(e)) {
					if (effect_damages(e)) {
						if (num_damaging > 2 && i > 0) {
							text_out(",");
						}
						if (num_damaging > 1 && i == num_damaging - 1) {
							text_out(" and");
						}
						text_out_c(COLOUR_L_GREEN, " %d", effect_avg_damage(e));
						const char *projection = effect_projection(e);
						if (strlen(projection) > 0) {
							text_out(" %s", projection);
						}
						i++;
					}
				}
				text_out(" damage.");
			}
		}
	}
	text_out("\n\n");
	/* XXX */
	text_out_pad = 0;
	text_out_indent = 0;
}

static const menu_iter spell_menu_iter = {
	NULL,	/* get_tag = NULL, just use lowercase selections */
	spell_menu_valid,
	spell_menu_display,
	spell_menu_handler,
	NULL	/* no resize hook */
};

/**
 * Create and initialise a spell menu, given a validity hook
 */
static struct menu *spell_menu_new(bool (*is_valid)(const struct player *p, int spell_index), bool show_description)
{
	struct menu *m = menu_new(MN_SKIN_SCROLL, &spell_menu_iter);
	struct spell_menu_data *d = mem_zalloc(sizeof *d);
	size_t width = MAX(0, MIN(Term->wid - 15, 80));

	region loc = { 0 - width, 1, width, -99 };

	/* collect spells from object */
	d->n_spells = spell_collect_from_book(player, &d->spells, &d->n_books, &d->books);
	if ((d->n_spells + d->n_books) == 0 || !spell_okay_list(player, is_valid, d->spells, d->n_spells)) {
		mem_free(m);
		mem_free(d->spells);
		mem_free(d->books);
		mem_free(d);
		return NULL;
	}

	/* Copy across private data */
	d->is_valid = is_valid;
	d->selected_spell = -1;
	d->selected_book = -1;
	d->browse = false;
	d->show_description = show_description;

	menu_setpriv(m, d->n_spells + d->n_books, d);

	/* Set flags */
	m->header = "Name                        Stat Lv     Cost Fail Info";
	m->flags = MN_CASELESS_TAGS;
	m->selections = lower_case;
	m->browse_hook = spell_menu_browser;
	m->cmd_keys = "?";

	/* Set size */
	loc.page_rows = d->n_spells + d->n_books + 1;
	menu_layout(m, &loc);

	return m;
}

/**
 * Clean up a spell menu instance
 */
static void spell_menu_destroy(struct menu *m)
{
	struct spell_menu_data *d = menu_priv(m);
	mem_free(d->spells);
	mem_free(d->books);
	mem_free(d);
	mem_free(m);
}

/**
 * Run the spell menu to select a spell.
 */
static int spell_menu_select(struct menu *m, const char *noun, const char *verb)
{
	struct spell_menu_data *d = menu_priv(m);
	char buf[80];

	screen_save();
	region_erase_bordered(&m->active);

	/* Format, capitalise and display */
	strnfmt(buf, sizeof buf, "%s which %s? ('?' to toggle description)",
			verb, noun);
	my_strcap(buf);
	prt(buf, 0, 0);

	menu_select(m, 0, true);
	screen_load();

	return d->selected_spell;
}

/**
 * Run the spell menu, without selections.
 */
static void spell_menu_browse(struct menu *m, const char *noun)
{
	struct spell_menu_data *d = menu_priv(m);

	screen_save();

	region_erase_bordered(&m->active);
	prt(format("Browsing %ss. ('?' to toggle description)", noun), 0, 0);

	d->browse = true;
	menu_select(m, 0, true);

	screen_load();
}

/**
 * Set up which spells are folded.
 * They must be all unfolded on entry.
 */
static void spell_menu_fold(struct menu *m, struct player *p)
{
	struct spell_menu_data *d = menu_priv(m);

	/* Get the maximum number of lines usable */
	int maxlines, lines;
	Term_get_size(&lines, &maxlines);
	maxlines -= 2;

	/* All spells are unfolded */
	int books = 0;
	int n_spells = d->n_spells;

	/* Fold until it fits, or until there is nothing more that can be done */
	int prevcount = -2;
	int lastcount = spell_count_visible(p) + books;
	while ((lastcount > maxlines) && (prevcount != lastcount)) {
		int booklength = 0;
		int bestbook = -1;
		double bestscore = -1.0;
		for(int i=0; i<n_spells; i++) {
			booklength++;
			/* Last spell, or different book to the next one? */
			if ((i == (n_spells - 1)) || ((spell_by_index(p, d->spells[i])->bidx) != (spell_by_index(p, d->spells[i+1])->bidx))) {
				/* Not already folded */
				if (!(p->spell_flags[d->spells[i]] & PY_SPELL_FOLDED)) {
					/* Not the selected book */
					if ((spell_by_index(p, d->spells[i])->bidx) != d->selected_book) {
						/* End of a book */
						double score = -1.0;
						/* Ignore single spell books, prioritize longer ones */
						if (booklength > 1) {
							/* This adds a small bias towards longer books in case of a tie between books
							 * with no used spells, and a smaller one towards books further up the list.
							 */
							score = (booklength * 0.0001) + (((double)(n_spells - i)) / (n_spells * 100000.0));
							/* Score is the sum of all per-spell scores (which means that longer books are prioritized).
							 * Per-spell scores are derived from the number of total uses and the time since last use.
							 * More recent use = higher score
							 * More total uses = higher score
							 */
							for(int j=(i-booklength)+1; j<=i; j++) {
								struct spell_state *spell = &p->spell[d->spells[j]];
								double turns_ago = MAX(10.0, (turn - spell->turn));
								double uses = spell->uses;
								score += (uses / turns_ago);
							}
						}
						/* Higher score = more win from folding */
						if (score > bestscore) {
							bestscore = score;
							bestbook = spell_by_index(p, d->spells[i])->bidx;
						}
					}
				}
				booklength = 0;
			}
		}
		/* If there is any gain to be made by folding any book, do it */
		if (bestbook >= 0) {
			assert(bestscore >= 0.0);
			spell_fold_book(p, bestbook);
		} else {
			/* If not, stop trying */
			break;
		}
		prevcount = lastcount;
		lastcount = spell_count_visible(p) + books;
	};
}

/**
 * Browse techniques
 */
void textui_spell_browse(void)
{
	struct menu *m;
	struct player *p = player;

	handle_stuff(p);

	spell_unfold_all(p);
	m = spell_menu_new(spell_okay_to_browse, true);
	if (m) {
		spell_menu_fold(m, p);
		spell_menu_destroy(m);
		m = spell_menu_new(spell_okay_to_browse, false);
	}

	if (m) {
		bool ok = false;
		do {
			ok = true;
			struct spell_menu_data *d = menu_priv(m);
			spell_menu_browse(m, "technique");
			if ((d->selected_spell < 0) && (d->selected_book >= 0)) {
				int book = d->selected_book;
				spell_unfold_all(p);
				spell_menu_destroy(m);
				m = spell_menu_new(spell_okay_to_browse, true);
				if (m) {
					d = menu_priv(m);
					d->selected_book = book;
					spell_menu_fold(m, p);
					spell_menu_destroy(m);
					m = spell_menu_new(spell_okay_to_browse, true);
				}
				ok = false;
			}
		} while (!ok);
		spell_menu_destroy(m);
	} else {
		msg("You know no techniques.");
	}
}

/**
 * Get a technique from specified book.
 */
int textui_get_spell_from_book(struct player *p, const char *verb,
							   const char *error,
							   bool (*spell_filter)(const struct player *p, int spell_index))
{
	const char *noun = "technique";
	struct menu *m;

	handle_stuff(p);

	spell_unfold_all(p);
	m = spell_menu_new(spell_filter, false);
	if (m) {
		spell_menu_fold(m, p);
		spell_menu_destroy(m);
		m = spell_menu_new(spell_filter, false);
	}

	int spell_index = -1;
	if (m) {
		struct spell_menu_data *d = menu_priv(m);
		spell_index = spell_menu_select(m, noun, verb);
		bool ok = false;
		do {
			ok = true;
			d = menu_priv(m);

			if ((d->selected_spell < 0) && (d->selected_book >= 0)) {
				spell_unfold_all(p);
				spell_menu_fold(m, p);
				spell_menu_destroy(m);
				m = spell_menu_new(spell_filter, false);
				spell_index = spell_menu_select(m, noun, verb);
				ok = false;
			}
		} while (!ok);
		spell_menu_destroy(m);
		return spell_index;
	} else {
		msg(error);
	}

	return -1;
}

/**
 * Get a technique from the player.
 */
int textui_get_spell(struct player *p, const char *verb,
					 cmd_code cmd, const char *error,
					 bool (*spell_filter)(const struct player *p, int spell_index))
{
	return textui_get_spell_from_book(p, verb, error, spell_filter);
}
