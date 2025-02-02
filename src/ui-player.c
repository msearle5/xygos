/**
 * \file ui-player.c
 * \brief character screens and dumps
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
#include "buildid.h"
#include "game-world.h"
#include "init.h"
#include "obj-fault.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-util.h"
#include "player.h"
#include "player-ability.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "store.h"
#include "ui-display.h"
#include "ui-entry.h"
#include "ui-entry-renderers.h"
#include "ui-history.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-output.h"
#include "ui-player.h"

//Use a three rather than four column layout for the first page, missing the leftmost panel (faction, etc.)
//#define THREE_COLUMN

/**
 * ------------------------------------------------------------------------
 * Panel utilities
 * ------------------------------------------------------------------------ */

/**
 * Panel line type
 */
struct panel_line {
	uint8_t attr;
	const char *label;
	char value[20];
};

/**
 * Panel holder type
 */
struct panel {
	size_t len;
	size_t max;
	struct panel_line *lines;
};

/**
 * Allocate some panel lines
 */
static struct panel *panel_allocate(int n) {
	struct panel *p = mem_zalloc(sizeof *p);

	p->len = 0;
	p->max = n;
	p->lines = mem_zalloc(p->max * sizeof *p->lines);

	return p;
}

/**
 * Free up panel lines
 */
static void panel_free(struct panel *p) {
	assert(p);
	mem_free(p->lines);
	mem_free(p);
}

/**
 * Add a new line to the panel
 */
static void panel_line(struct panel *p, uint8_t attr, const char *label,
		const char *fmt, ...) {
	va_list vp;

	struct panel_line *pl;

	/* Get the next panel line */
	assert(p);
	assert(p->len != p->max);
	pl = &p->lines[p->len++];

	/* Set the basics */
	pl->attr = attr;
	pl->label = label;

	/* Set the value */
	va_start(vp, fmt);
	vstrnfmt(pl->value, sizeof pl->value, fmt, vp);
	va_end(vp);
}

/**
 * Add a spacer line in a panel
 */
static void panel_space(struct panel *p) {
	assert(p);
	assert(p->len != p->max);
	p->len++;
}

#define CS_MAX_REGIONS 4
#define CS_MAX_LABEL 28

/**
 * Cache the layout of the character sheet, currently only for the resistance
 * panel, since it is no longer hardwired.
 */
struct char_sheet_resist {
	struct ui_entry* entry;
	wchar_t label[CS_MAX_LABEL+1];
};
struct char_sheet_config {
	struct ui_entry** stat_mod_entries;
	region res_regions[CS_MAX_REGIONS];
	struct char_sheet_resist *resists_by_region[CS_MAX_REGIONS];
	int label_width[CS_MAX_REGIONS];
	int n_resist_by_region[CS_MAX_REGIONS];
	int n_stat_mod_entries;
	int res_cols[CS_MAX_REGIONS];
	int res_rows;
	int regions;
	int sustain_region;
};
static struct char_sheet_config *cached_config = NULL;
static void display_resistance_panel(int ipart, struct char_sheet_config* config, bool percentmode);


static bool have_valid_char_sheet_config(void)
{
	if (!cached_config) {
		return false;
	}
	if (cached_config->res_cols[0] != cached_config->label_width[0] + player->body.count) {
		return false;
	}
	return true;
}


static void release_char_sheet_config(void)
{
	int i;

	if (!cached_config) {
		return;
	}
	for (i = 0; i < 4; ++i) {
		mem_free(cached_config->resists_by_region[i]);
	}
	mem_free(cached_config->stat_mod_entries);
	mem_free(cached_config);
	cached_config = 0;
}


static bool check_for_two_categories(const struct ui_entry* entry,
	void *closure)
{
	const char **categories = closure;

	return ui_entry_has_category(entry, categories[0]) &&
		ui_entry_has_category(entry, categories[1]);
}


static void configure_char_sheet(bool minimum_size, bool percentmode)
{
	const char* region_categories[] = {
		"resistances",
		"hindrances",
		"abilities",
		"modifiers"
	};
	const char* test_categories[2];
	struct ui_entry_iterator* ui_iter;
	int i, n;
	const int percent_width = 5;

	release_char_sheet_config();

	cached_config = mem_zalloc(sizeof(*cached_config));
	cached_config->regions = CS_MAX_REGIONS;
	cached_config->sustain_region = 1;

	for(int i=0;i<CS_MAX_REGIONS; i++)
		cached_config->label_width[i] = 0;
	int wid, hgt;
	if (minimum_size) {
		wid = 90;
		hgt = 24;
	} else {
		Term_get_size(&wid, &hgt);
	}

	/* Fit columns to the screen */
	int offset, low, lowi = 0;
	do {
		/* +1, because there is no gap after the rightmost region */
		offset = wid+1;
		low = wid;
		for(int i=0;i<cached_config->regions;i++) {
			int width = 2 + player->body.count + cached_config->label_width[i];

			if ((percentmode) && (i == cached_config->sustain_region))
				width = percent_width;

			offset -= width + 1;
			if (cached_config->label_width[i] < low) {
				low = cached_config->label_width[i];
				lowi = i;
			}
		}
		if (offset > 0) {
			cached_config->label_width[lowi]++;
		}
	} while (offset > 0);

	for(int i=0;i<cached_config->regions;i++) {
		if ((cached_config->label_width[i]) > CS_MAX_LABEL)
			cached_config->label_width[i] = CS_MAX_LABEL;
	}

	/* Same size regions, if minimum size */
	if (minimum_size) {
		for(int i=0;i<cached_config->regions;i++)
			cached_config->label_width[i] = low;
	}

	test_categories[0] = "CHAR_SCREEN1";
	test_categories[1] = "stat_modifiers";
	ui_iter = initialize_ui_entry_iterator(check_for_two_categories,
		test_categories, test_categories[1]);
	n = count_ui_entry_iterator(ui_iter);
	/*
	 * Linked to hardcoded stats display with STAT_MAX entries so only use
	 * that many.
	 */
	if (n > STAT_MAX) {
	    n = STAT_MAX;
	}

	cached_config->n_stat_mod_entries = n;
	cached_config->stat_mod_entries = mem_alloc(n * sizeof(*cached_config->stat_mod_entries));
	for (i = 0; i < n; ++i) {
		cached_config->stat_mod_entries[i] =
			advance_ui_entry_iterator(ui_iter);
	}
	release_ui_entry_iterator(ui_iter);

	cached_config->res_rows = 0;
	for (i = 0; i < cached_config->regions; ++i) {
		int j;

		cached_config->res_cols[i] = cached_config->label_width[i] + 2 + player->body.count;

		if ((percentmode) && (i == cached_config->sustain_region))
			cached_config->res_cols[i] = percent_width;

		int col = 0;
		if (i > 0)
			col = cached_config->res_cols[i-1] + cached_config->res_regions[i-1].col + 1;

		cached_config->res_regions[i].col = col;

		int row = 2 + STAT_MAX;
		if ((percentmode) || (i != cached_config->sustain_region))
			row = 1;
		cached_config->res_regions[i].row = row;
		cached_config->res_regions[i].width = cached_config->res_cols[i];

		test_categories[1] = region_categories[i];
		ui_iter = initialize_ui_entry_iterator(check_for_two_categories, test_categories, region_categories[i]);
		n = count_ui_entry_iterator(ui_iter);

		if ((percentmode) && (i == cached_config->sustain_region))
			n = 0;

		/*
		 * Fit in 24 row display; leave at least one row blank before
		 * prompt on last row.
		 */
		if (n + cached_config->res_regions[i].row > 22) {
		    n = 22 - cached_config->res_regions[i].row;
		}
		cached_config->n_resist_by_region[i] = n;
		cached_config->resists_by_region[i] = mem_alloc(n * sizeof(*cached_config->resists_by_region[i]));
		for (j = 0; j < n; ++j) {
			struct ui_entry *entry = advance_ui_entry_iterator(ui_iter);

			cached_config->resists_by_region[i][j].entry = entry;
			get_ui_entry_label(entry, cached_config->label_width[i] + 1, true, cached_config->resists_by_region[i][j].label);
			(void) text_mbstowcs(cached_config->resists_by_region[i][j].label + cached_config->label_width[i], ":", 1);
		}
		release_ui_entry_iterator(ui_iter);

		if (cached_config->res_rows <
			cached_config->n_resist_by_region[i]) {
			cached_config->res_rows =
				cached_config->n_resist_by_region[i];
		}
	}
	for (i = 0; i < cached_config->regions; ++i) {
		cached_config->res_regions[i].page_rows =
			cached_config->res_rows + 2;
	}
}


/**
 * Returns a "rating" of x depending on y, and sets "attr" to the
 * corresponding "attribute".
 */
static const char *likert(int x, int y, uint8_t *attr)
{
	/* Paranoia */
	if (y <= 0) y = 1;

	/* Negative value */
	if (x < 0) {
		*attr = COLOUR_RED;
		return ("Very Bad");
	}

	/* Analyze the value */
	switch ((x / y))
	{
		case 0:
		case 1:
		{
			*attr = COLOUR_RED;
			return ("Bad");
		}
		case 2:
		{
			*attr = COLOUR_RED;
			return ("Poor");
		}
		case 3:
		case 4:
		{
			*attr = COLOUR_YELLOW;
			return ("Fair");
		}
		case 5:
		{
			*attr = COLOUR_YELLOW;
			return ("Good");
		}
		case 6:
		{
			*attr = COLOUR_YELLOW;
			return ("Very Good");
		}
		case 7:
		case 8:
		{
			*attr = COLOUR_L_GREEN;
			return ("Excellent");
		}
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		{
			*attr = COLOUR_L_GREEN;
			return ("Superb");
		}
		case 14:
		case 15:
		case 16:
		case 17:
		{
			*attr = COLOUR_L_GREEN;
			return ("Heroic");
		}
		default:
		{
			*attr = COLOUR_L_GREEN;
			return ("Legendary");
		}
	}
}


/**
 * Equippy chars
 */
static void display_player_equippy(int y, int x)
{
	int i;

	uint8_t a;
	wchar_t c;

	struct object *obj;

	/* Dump equippy chars */
	for (i = 0; i < player->body.count; ++i) {
		/* Object */
		obj = slot_object(player, i);

		/* Skip empty objects */
		if (!obj) continue;

		/* Get attr/char for display */
		a = object_attr(obj);
		c = object_char(obj);

		/* Dump */
		if ((tile_width == 1) && (tile_height == 1))
			Term_putch(x + i, y, a, c);
	}
}

static void display_player_header(struct char_sheet_config *config, int region, int row, int col)
{
	char buf[64];
	memset(buf, ' ', sizeof(buf));
	int width = config->label_width[region];
	strcpy(buf+width, "abcdefghijklmnopqrstuvwxyz");
	strcpy(buf+player->body.count+width, "*@");

	Term_putstr(col, row, config->res_cols[region], COLOUR_WHITE, buf);
}

static void display_resistance_panel(int ipart, struct char_sheet_config *config, bool percentmode)
{
	int *vals = mem_zalloc((player->body.count + 2) * sizeof(*vals));
	int *auxs = mem_zalloc((player->body.count + 2) * sizeof(*auxs));
	struct object **equipment =
		mem_alloc(player->body.count * sizeof(*equipment));
	struct cached_object_data **ocaches =
		mem_zalloc(player->body.count * sizeof(*ocaches));
	struct cached_player_data *pcache = NULL;
	struct cached_object_data *gcache = NULL;
	struct ui_entry_details render_details;
	int i;
	int j;
	int col = config->res_regions[ipart].col;
	int row = config->res_regions[ipart].row;

	for (i = 0; i < player->body.count; i++) {
		equipment[i] = slot_object(player, i);
	}

	/* Equippy / header */
	if (ipart != config->sustain_region) {
		display_player_equippy(row++, col + config->label_width[ipart]);

		display_player_header(config, ipart, row++, col);
	} else {
		if (percentmode) {
			Term_putstr(col, row, config->res_cols[ipart], COLOUR_WHITE, "% Res");
		}
	}

	render_details.label_position.x = col;
	render_details.value_position.x = col + config->label_width[ipart];
	render_details.position_step = loc(1, 0);
	render_details.combined_position = loc(0, 0);
	render_details.vertical_label = false;
	render_details.alternate_color_first = false;
	render_details.show_combined = false;
	for (i = 0; i < config->n_resist_by_region[ipart]; i++, row++) {
		const struct ui_entry *entry = config->resists_by_region[ipart][i].entry;

		for (j = 0; j < player->body.count; j++) {
			compute_ui_entry_values_for_object(entry, equipment[j], player, ocaches + j, vals + j, auxs + j);
		}
		compute_ui_entry_values_for_gear(entry, player, &gcache, vals + player->body.count, auxs + player->body.count);
		compute_ui_entry_values_for_player(entry, player, &pcache, vals + player->body.count + 1, auxs + player->body.count + 1);

		combine_ui_entry_values(entry, vals, auxs, player->body.count + 1);

		int val = vals[player->body.count + 1];
		int aux = auxs[player->body.count + 1];
		int sum;
		if ((val < IMMUNITY) && (aux < IMMUNITY))
			sum = MIN(IMMUNITY-1, (val + aux));
		else {
			sum = MAX(val, aux);
			if ((sum != UI_ENTRY_UNKNOWN_VALUE) && (sum != UI_ENTRY_VALUE_NOT_PRESENT))
				sum = val = IMMUNITY;
		}
		vals[player->body.count + 1] = val;

		render_details.label_position.y = row;
		render_details.value_position.y = row;
		render_details.known_icon = is_ui_entry_for_known_icon(entry, player);
		ui_entry_renderer_apply(get_ui_entry_renderer_index(entry), config->resists_by_region[ipart][i].label, config->label_width[ipart], vals, auxs, player->body.count + 2, &render_details);
		if (percentmode && (ipart == 0)) {
			char buf[32];
			bool perm = (sum == val);
			int colour = COLOUR_WHITE;
			if (sum == UI_ENTRY_UNKNOWN_VALUE)
				strnfmt(buf, sizeof(buf), "  ??");
			else if (sum == UI_ENTRY_VALUE_NOT_PRESENT) {
				strnfmt(buf, sizeof(buf), "  ??");
				colour = COLOUR_SLATE;
			} else {
				sum = resist_to_percent(sum, i);
				strnfmt(buf, sizeof(buf), "%3d%%", sum);
				if (perm) {
					colour = COLOUR_YELLOW;
					if (sum >= 10)
						colour = COLOUR_GREEN;
					if (sum < 0)
						colour = COLOUR_RED;
				} else {
					colour = COLOUR_L_YELLOW;
					if (sum >= 10)
						colour = COLOUR_L_GREEN;
					if (sum < 0)
						colour = COLOUR_L_RED;
				}
			}
			Term_putstr(config->res_regions[config->sustain_region].col, row, config->res_cols[config->sustain_region], colour, buf);
		}
	}

	if (pcache) {
		release_cached_player_data(pcache);
	}
	if (gcache) {
		release_cached_object_data(gcache);
	}
	for (i = 0; i < player->body.count; ++i) {
		if (ocaches[i]) {
			release_cached_object_data(ocaches[i]);
		}
	}
	mem_free(ocaches);
	mem_free(equipment);
	mem_free(auxs);
	mem_free(vals);
}

static void display_player_flag_info(bool percentmode)
{
	int i;

	for (i = 0; i < 4; i++)
		display_resistance_panel(i, cached_config, percentmode);
}

static void display_player_stat(int i, int row, int statcol)
{
	if (player->stat_cur[i] < player->stat_max[i])
		/* Use lowercase stat name */
		put_str(stat_names_reduced[i], row, statcol);
	else
		/* Assume uppercase stat name */
		put_str(stat_names[i], row, statcol);

	/* Indicate natural maximum */
	if (player->stat_max[i] == 18+100)
		put_str("!", row, statcol+3);
	else
		put_str(":", row, statcol+3);
}

static void display_player_sust_stats(int col)
{
	int row = 3;

	/* Display the stat labels */
	for (int i = 0; i < STAT_MAX; i++) {
		display_player_stat(i, row+i, col);
	}
}

/**
 * Special display, part 2b
 */
void display_player_stat_info(bool generating)
{
	int i, row, col;

	char buf[80];

	/* Row */
	row = 2;

	/* Column */
	col = 39;
	if (!generating) {
		col += 4;
	}

	/* Get the terminal size */
	int width, height;
	Term_get_size(&width, &height);

	/* Print out the labels for the columns */
	const char *title = "  Self Race Ext Per Abi Cla Equ  Best ";
	c_put_str(COLOUR_WHITE, title, row-1, col);
	if (width >= 87) {
		c_put_str(COLOUR_WHITE, "Curr", row-1, col+strlen(title));
	} else if (generating) {
		c_put_str(COLOUR_RED, "Pts", row-1, col+strlen(title));
	}

	/* Display the stats */
	for (i = 0; i < STAT_MAX; i++) {
		/* Reduced or normal */
		int statcol = col;
		if (generating)
			statcol -= 1;
		else
			statcol -= 5;

		display_player_stat(i, row+i, statcol);

		/* Internal "natural" maximum value
		 * When doing character generation it is possible to
		 * assume that this will be 10..18 to save the space
		 * that would be used to display 18/xxx by cnv_stat()
		 **/
		if (generating) {
			strnfmt(buf, sizeof(buf), " %2d", player->stat_max[i]);
			c_put_str(COLOUR_L_GREEN, buf, row+i, col+3) ;
		} else {
			cnv_stat(player->stat_max[i], buf, sizeof(buf));
			c_put_str(COLOUR_L_GREEN, buf, row+i, col);
		}

		/* Race Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->race->r_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+7);

		/* Extension Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->extension->r_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+11);

		/* Personality Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->personality->r_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+15);

		/* Ability Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", ability_to_stat(i));
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+19);

		/* Class Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", class_to_stat(i));
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+23);

		/* Equipment Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->state.stat_add[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+27);

		/* Resulting "modified" maximum value */
		cnv_stat(player->state.stat_top[i], buf, sizeof(buf));
		c_put_str(COLOUR_L_GREEN, buf, row+i, col+31);

		/* Only display stat_use if there has been draining. Ignore for small terminals (as it's a dup of the left panel) */
		if ((generating) || (width >= 87)) {
			if (player->stat_cur[i] < player->stat_max[i]) {
				cnv_stat(player->state.stat_use[i], buf, sizeof(buf));
				c_put_str(COLOUR_YELLOW, buf, row+i, col+38);
			}
		}
	}
}


/**
 * Special display, part 2c
 *
 * Display stat modifiers from equipment and sustains.  Colors and symbols
 * are configured from ui_entry.txt and ui_entry_renderers.txt.  Other
 * configuration that's possible there (extra characters for each number
 * for instance) aren't well handled here - the assumption is just one digit
 * for each equipment slot.
 */
static void display_player_sust_info(struct char_sheet_config *config)
{
	int *vals = mem_alloc((player->body.count + 1) * sizeof(*vals));
	int *auxs = mem_alloc((player->body.count + 1) * sizeof(*auxs));
	struct object **equipment =
		mem_alloc(player->body.count * sizeof(*equipment));
	struct cached_object_data **ocaches =
		mem_zalloc(player->body.count * sizeof(*ocaches));
	struct cached_player_data *pcache = NULL;
	struct ui_entry_details render_details;
	int i, row, col;

	for (i = 0; i < player->body.count; i++) {
		equipment[i] = slot_object(player, i);
	}

	/* Row */
	row = 3;

	/* Column */
	col = config->res_regions[config->sustain_region].col;

	display_player_sust_stats(col + config->label_width[config->sustain_region] - 4);

	/* Header */
	display_player_equippy(row - 2, col + config->label_width[config->sustain_region]);
	display_player_header(config, config->sustain_region, row - 1, col);

	render_details.label_position.x = col + player->body.count;
	render_details.value_position.x = col + config->label_width[config->sustain_region] + 1;
	render_details.position_step = loc(1, 0);
	render_details.combined_position = loc(0, 0);
	render_details.vertical_label = false;
	render_details.alternate_color_first = false;
	render_details.known_icon = true;
	render_details.show_combined = false;
	for (i = 0; i < config->n_stat_mod_entries; i++) {
		const struct ui_entry *entry = config->stat_mod_entries[i];
		int j;

		for (j = 0; j < player->body.count; j++) {
			compute_ui_entry_values_for_object(entry, equipment[j], player, ocaches + j, vals + j, auxs + j);
		}
		compute_ui_entry_values_for_player(entry, player, &pcache, vals + player->body.count, auxs + player->body.count);
		/* Just use the sustain information for the player column. */
		vals[player->body.count] = 0;

		render_details.label_position.y = row + i;
		render_details.value_position.y = row + i;
		ui_entry_renderer_apply(get_ui_entry_renderer_index(entry), NULL, 0, vals, auxs, player->body.count + 1, &render_details);
	}

	if (pcache) {
		release_cached_player_data(pcache);
	}
	for (i = 0; i < player->body.count; ++i) {
		if (ocaches[i]) {
			release_cached_object_data(ocaches[i]);
		}
	}
	mem_free(ocaches);
	mem_free(equipment);
	mem_free(auxs);
	mem_free(vals);
}



static void display_panel(const struct panel *p, bool left_adj,
		const region *bounds)
{
	size_t i;
	int col = bounds->col;
	int row = bounds->row;
	int w = bounds->width;
	int offset = 0;

	region_erase(bounds);

	if (left_adj) {
		for (i = 0; i < p->len; i++) {
			struct panel_line *pl = &p->lines[i];

			int len = pl->label ? strlen(pl->label) : 0;
			if (offset < len) offset = len;
		}
		offset += 2;
	}

	for (i = 0; i < p->len; i++, row++) {
		int len;
		struct panel_line *pl = &p->lines[i];

		if (!pl->label)
			continue;

		Term_putstr(col, row, strlen(pl->label), COLOUR_WHITE, pl->label);

		len = strlen(pl->value);
		len = len < w - offset ? len : w - offset - 1;

		if (left_adj)
			Term_putstr(col+offset, row, len, pl->attr, pl->value);
		else
			Term_putstr(col+w-len, row, len, pl->attr, pl->value);
	}
}

static const char *show_title(void)
{
	if (player->wizard)
		return "[=-WIZARD-=]";
	else if (player->total_winner || player->lev > PY_MAX_LEVEL)
		return "***WINNER***";
	else
		return player_title();
}

static const char *show_personality(void)
{
	return player->personality->name;
}

static const char *show_adv_exp(void)
{
	if (player->lev < PY_MAX_LEVEL) {
		static char buffer[30];
		int32_t advance = exp_to_gain(player->lev + 1);
		strnfmt(buffer, sizeof(buffer), "%d", advance);
		return buffer;
	}
	else {
		return "********";
	}
}

static const char *show_depth(void)
{
	static char buffer[13];

	if (player->max_depth == 0) return "Town";

	strnfmt(buffer, sizeof(buffer), "%dm (L%d)",
	        player->max_depth * 50, player->max_depth);
	return buffer;
}

static const char *show_speed(void)
{
	static char buffer[10];
	int tmp = player->state.speed;
	if (player->timed[TMD_FAST]) tmp -= 10;
	if (player->timed[TMD_SLOW]) tmp += 10;
	if (tmp == 110) return "Normal";
	int multiplier = 10 * extract_energy[tmp] / extract_energy[110];
	int int_mul = multiplier / 10;
	int dec_mul = multiplier % 10;
	if (OPT(player, effective_speed))
		strnfmt(buffer, sizeof(buffer), "%d.%dx (%+d)", int_mul, dec_mul, tmp - 110);
	else
		strnfmt(buffer, sizeof(buffer), "%+d (%d.%dx)", tmp - 110, int_mul, dec_mul);
	return buffer;
}

static uint8_t max_color(int val, int max)
{
	return val < max ? COLOUR_YELLOW : COLOUR_L_GREEN;
}

/**
 * Colours for table items
 */
static const uint8_t colour_table[] =
{
	COLOUR_RED, COLOUR_RED, COLOUR_RED, COLOUR_L_RED, COLOUR_ORANGE,
	COLOUR_YELLOW, COLOUR_YELLOW, COLOUR_GREEN, COLOUR_GREEN, COLOUR_L_GREEN,
	COLOUR_L_BLUE
};


static struct panel *get_panel_topleft(void) {
	struct panel *p = panel_allocate(8);

	panel_line(p, COLOUR_L_BLUE, "Name", "%s", player->full_name);
	if (!streq(player->extension->name, "None"))
		panel_line(p, COLOUR_L_BLUE, "Extend",	"%s", player->extension->name);
	panel_line(p, COLOUR_L_BLUE, "Race",	"%s", player->race->name);
	panel_line(p, COLOUR_L_BLUE, "Class", "%s", player->class->name);
	if (player->switch_class != player->class->cidx) {
		panel_line(p, COLOUR_L_BLUE, "  -->", "%s", get_class_by_idx(player->switch_class)->name);
	} else {
		int lic = levels_in_class(player->class->cidx);
		if (lic != player->max_lev) {
			char buf[32];
			strnfmt(buf, sizeof(buf), "%d/%d", lic, player->max_lev);
			panel_line(p, COLOUR_L_BLUE, "Level", "%s", buf);
		}
	}
	panel_line(p, COLOUR_L_BLUE, "Title", "%s", show_title());
	panel_line(p, COLOUR_L_BLUE, "Perso", "%s", show_personality());
	panel_line(p, COLOUR_L_BLUE, "HP", "%d/%d", player->chp, player->mhp);

	return p;
}


static struct panel *get_panel_farleft(void) {
	struct panel *p = panel_allocate(9);

	/* Town, mob and salon factions */
	int town = MIN(MAX(player->town_faction + 2, 0), 8);
	int mob = MIN(MAX(player->bm_faction + 2, 0), 8);
	int cyber = MIN(MAX((player->cyber_faction / 100) + 2, 0), 8);
	int fc = (player->fc_faction <= 0) ? 0 : 1+((MIN(player->fc_faction, z_info->arena_max_depth) * 7) / z_info->arena_max_depth);

	/* Descriptions of how much they like you or not. The first entry is for -2 (or less) up to +6 (or more) */
	static const char *town_name[9] = {
		"Public Enemy",
		"Suspect",
		"Customer",
		"Regular",
		"Associate",
		"Partner",
		"Partner",
		"Partner",
		"Partner"
	};
	static const char *mob_name[9] = {
		"Deadly Enemy",
		"Disliked",
		"Outsider",
		"Initiate",
		"Mobster",
		"Assassin",
		"Assassin",
		"Mob Boss",
		"Mob Boss",
	};
	static const char *cyber_name[9] = {
		"Oppressor",
		"Hater",
		"Wannabe",
		"Member",
		"Chrome Hand",
		"Silver Hand",
		"Golden Hand",
		"Platinum Hand",
		"Diamond Hand"
	};
	static const char *fc_name[9] = {
		"Spectator",
		"Brawler",
		"Scrapper",
		"Bruiser",
		"Slugger",
		"Fighter",
		"Prizefighter",
		"Contender",
		"Champion"
	};
	static const int colour_name[9] = {
		COLOUR_RED,
		COLOUR_ORANGE,
		COLOUR_YELLOW,
		COLOUR_GREEN,
		COLOUR_BLUE,
		COLOUR_BLUE,
		COLOUR_BLUE,
		COLOUR_BLUE,
		COLOUR_BLUE
	};
	static const int fc_colour_name[9] = {
		COLOUR_YELLOW,
		COLOUR_GREEN,
		COLOUR_GREEN,
		COLOUR_GREEN,
		COLOUR_GREEN,
		COLOUR_GREEN,
		COLOUR_GREEN,
		COLOUR_GREEN,
		COLOUR_BLUE
	};

	panel_line(p, colour_name[town], "Town", "%s", town_name[town]);
	panel_line(p, colour_name[mob], "Mob", "%s", mob_name[mob]);
	panel_line(p, colour_name[cyber], "Cyber", "%s", cyber_name[cyber]);
	panel_line(p, fc_colour_name[fc], "Arena", "%s", fc_name[fc]);

	/* Show the danger level */
	if (OPT(player, birth_time_limit)) {
		panel_space(p);
		static const char *danger_name[] = {
			"None",
			"Slight",
			"Modest",
			"Modest",
			"Elevated",
			"Elevated",
			"Elevated",
			"Severe",
			"Severe",
			"Severe",
			"Severe",
			"Severe",
			"Extreme",
		};
		static const int danger_colour[(sizeof(danger_name) / sizeof(danger_name[0]))] = {
			COLOUR_GREEN,
			COLOUR_L_GREEN,
			COLOUR_YELLOW,
			COLOUR_YELLOW,
			COLOUR_ORANGE,
			COLOUR_ORANGE,
			COLOUR_ORANGE,
			COLOUR_RED,
			COLOUR_RED,
			COLOUR_RED,
			COLOUR_RED,
			COLOUR_RED,
			COLOUR_MAGENTA
		};
		int danger = MIN((player->danger + 4) / 5, (int)(sizeof(danger_name) / sizeof(danger_name[0])) - 1);
		const char *descr = danger_name[danger];
		int col = danger_colour[danger];
		panel_line(p, col, "Danger", "%s (%d)", descr, player->danger);
		panel_line(p, COLOUR_GREEN, "Postponed", "%d", player->danger_reduction);
	}

	/* Per-class */
	int32_t allowed, used;
	bool timelord = get_regens(&allowed, &used);
	if (timelord) {
		panel_space(p);
		panel_line(p, COLOUR_L_BLUE, "Regenerations", "%d/%d", used, allowed);
	}
	return p;
}

static struct panel *get_panel_midleft(void) {
	struct panel *p = panel_allocate(9);
	int diff = weight_remaining(player);
	uint8_t attr = diff < 0 ? COLOUR_L_RED : COLOUR_L_GREEN;

	panel_line(p, max_color(player->lev, player->max_lev),
			"Level", "%d", player->lev);
	panel_line(p, max_color(player->exp, player->max_exp),
			"Cur Exp", "%d", player->exp);
	panel_line(p, COLOUR_L_GREEN, "Max Exp", "%d", player->max_exp);
	panel_line(p, COLOUR_L_GREEN, "Adv Exp", "%s", show_adv_exp());
	panel_space(p);
	panel_line(p, COLOUR_L_GREEN, "Cash", "%d", player->au);
	panel_line(p, attr, "Burden", fmt_weight(burden_weight(player), NULL));
	panel_line(p, attr, "Overweight", fmt_weight(-diff, NULL));
	panel_line(p, COLOUR_L_GREEN, "Max Depth", "%s", show_depth());

	return p;
}

static struct panel *get_panel_combat(void) {
	struct panel *p = panel_allocate(9);
	struct object *obj;
	int bth = 0;
	int dam = 0;
	int hit = 0;
	int melee_dice = 1, melee_sides = 1;

	/* AC */
	panel_line(p, COLOUR_L_BLUE, "Armor", "[%d,%+d]",
			player->known_state.ac, player->known_state.to_a);

	/* Melee */
	panel_space(p);

	obj = equipped_item_by_slot_name(player, "weapon");
	if (obj) {
		dam = player->known_state.to_d + (obj ? obj->known->to_d : 0);
		hit = player->known_state.to_h + (obj ? obj->known->to_h : 0);

		melee_dice = obj->dd;
		melee_sides = obj->ds;

		panel_line(p, COLOUR_L_BLUE, "Melee", "%dd%d,%+d", melee_dice, melee_sides, dam);
	} else {
		double avg = py_unarmed_damage(player);
		panel_line(p, COLOUR_L_BLUE, "Unarmed", "%.2lf", avg);
	}

	bth = (weapon_skill(player) * 10) / BTH_PLUS_ADJ;
	panel_line(p, COLOUR_L_BLUE, "To-hit", "%d,%+d", bth / 10, hit);
	panel_line(p, COLOUR_L_BLUE, "Blows", "%d.%d/turn",
			player->state.num_blows / 100, (player->state.num_blows / 10 % 10));

	/* Ranged */
	obj = equipped_item_by_slot_name(player, "shooting");
	bth = (player->state.skills[SKILL_TO_HIT_GUN] * 10) / BTH_PLUS_ADJ;
	hit = player->known_state.to_h + (obj ? obj->known->to_h : 0);
	dam = obj ? obj->known->to_d : 0;

	panel_space(p);
	panel_line(p, COLOUR_L_BLUE, "Shoot to-dam", "%+d", dam);
	panel_line(p, COLOUR_L_BLUE, "To-hit", "%d,%+d", bth / 10, hit);
	panel_line(p, COLOUR_L_BLUE, "Shots", "%d.%d/turn",
			   player->state.num_shots / 10, player->state.num_shots % 10);

	return p;
}

static struct panel *get_panel_skills(void) {
	struct panel *p = panel_allocate(8);

	int skill;
	uint8_t attr;
	const char *desc;
	int depth = cave ? cave->depth : 0;

#define BOUND(x, min, max)		MIN(max, MAX(min, x))

	/* Saving throw */
	skill = BOUND(player->state.skills[SKILL_SAVE], 0, 100);
	panel_line(p, colour_table[skill / 10], "Saving Throw", "%d%%", skill);

	/* Stealth */
	desc = likert(player->state.skills[SKILL_STEALTH], 1, &attr);
	panel_line(p, attr, "Stealth", "%s", desc);

	/* Physical disarming: assume we're disarming a dungeon trap */
	skill = BOUND(player->state.skills[SKILL_DISARM_PHYS] - depth / 5, 2, 100);
	panel_line(p, colour_table[skill / 10], "Disarm - mech.", "%d%%", skill);

	/* Magical disarming */
	skill = BOUND(player->state.skills[SKILL_DISARM_MAGIC] - depth / 5, 2, 100);
	panel_line(p, colour_table[skill / 10], "Disarm - elec.", "%d%%", skill);

	/* Devices */
	skill = player->state.skills[SKILL_DEVICE];
	panel_line(p, colour_table[skill / 13], "Devices", "%d", skill);

	/* Searching ability */
	skill = BOUND(player->state.skills[SKILL_SEARCH], 0, 100);
	panel_line(p, colour_table[skill / 10], "Searching", "%d%%", skill);

	/* Infravision */
	panel_line(p, COLOUR_L_GREEN, "Infravision", "%d ft",
			player->state.see_infra * 10);

	/* Speed */
	skill = player->state.speed;
	if (player->timed[TMD_FAST]) skill -= 10;
	if (player->timed[TMD_SLOW]) skill += 10;
	attr = skill < 110 ? COLOUR_L_UMBER : COLOUR_L_GREEN;
	panel_line(p, attr, "Speed", "%s", show_speed());

	return p;
}

static struct panel *get_panel_misc(void) {
	struct panel *p = panel_allocate(7);
	uint8_t attr = COLOUR_L_BLUE;

	panel_line(p, attr, "Age", "%d", player->age);
	panel_line(p, attr, "Height", "%d'%d\"", player->ht / 12, player->ht % 12);
	panel_line(p, attr, "Weight", fmt_weight(player->wt, NULL));
	panel_line(p, attr, "Turns used:", "");
	panel_line(p, attr, "Game", "%d", turn);
	panel_line(p, attr, "Active", "%d", player->total_energy / 100);
	panel_line(p, attr, "Rest", "%d", player->resting_turn);

	return p;
}

/**
 * Panels for main character screen
 */
struct panel_entry {
	region bounds;
	bool align_left;
	struct panel *(*panel)(void);
};

static const struct panel_entry panels[] =
{
	/*   x  y wid rows */
	{ {  1, 1, 40, 7 }, true,  get_panel_topleft },	/* Name, Class, ... */
	{ { 21, 1, 16, 3 }, false, get_panel_misc },	/* Age, ht, wt, ... */
#ifdef THREE_COLUMN_PANEL
	{ {  1, 9, 24, 9 }, false, get_panel_midleft },	/* Cur Exp, Max Exp, ... */
	{ { 29, 9, 19, 9 }, false, get_panel_combat },
	{ { 52, 9, 20, 8 }, false, get_panel_skills },
#else
	{ {  1, 9, 19, 9 }, false, get_panel_farleft },	/* Faction, Danger */
	{ { 21, 9, 22, 9 }, false, get_panel_midleft },	/* Cur Exp, Max Exp, ... */
	{ { 44, 9, 17, 9 }, false, get_panel_combat },
	{ { 62, 9, 18, 8 }, false, get_panel_skills },
	{ { 0 } },
#endif
};

static const struct panel_entry panels_drop[] =
{
	/*   x  y wid rows */
	{ {  1, 1, 40, 7 }, true,  get_panel_topleft },	/* Name, Class, ... */
	{ { 21, 1, 16, 3 }, false, get_panel_misc },	/* Age, ht, wt, ... */
#ifdef THREE_COLUMN_PANEL
	{ {  1, 11, 24, 9 }, false, get_panel_midleft },	/* Cur Exp, Max Exp, ... */
	{ { 29, 11, 19, 9 }, false, get_panel_combat },
	{ { 52, 11, 20, 8 }, false, get_panel_skills },
#else
	{ {  1, 11, 19, 9 }, false, get_panel_farleft },	/* Faction, Danger */
	{ { 21, 11, 22, 9 }, false, get_panel_midleft },	/* Cur Exp, Max Exp, ... */
	{ { 44, 11, 17, 9 }, false, get_panel_combat },
	{ { 62, 11, 18, 8 }, false, get_panel_skills },
	{ { 0 } },
#endif
};

void display_player_xtra_info(bool drop)
{
	size_t i = 0;
	const struct panel_entry *pan = drop ? panels_drop : panels;
	while (pan[i].panel) {
		struct panel *p = pan[i].panel();
		display_panel(p, pan[i].align_left, &pan[i].bounds);
		panel_free(p);
		i++;
	};

	/* Indent output by 1 character, and wrap at column 72 */
	text_out_wrap = 72;
	text_out_indent = 1;

	/* History */
	if (!drop) {
		Term_gotoxy(text_out_indent, 19);
		text_out_to_screen(COLOUR_WHITE, player->history);
	}

	/* Reset text_out() vars */
	text_out_wrap = 0;
	text_out_indent = 0;

	return;
}

/**
 * Display box text, wait for a key, return to play.
 */
ui_event ui_text_box(const char *text)
{
	/* Save the screen */
	Term_save();

	/* Erase screen */
	clear_from(0);

	/* When not playing, do not display in subwindows */
	if (Term != angband_term[0] && !player->upkeep->playing) {
		ui_event ev = { 0 };
		return ev;
	}

	/* Display the message */
	int w, h;
	Term_get_size(&w, &h);
	text_out_wrap = (w/2) + 32;
	text_out_indent = (w/2) - 32;
	Term_gotoxy(text_out_indent, (h/2) - (strlen(text) / 64));
	text_out_to_screen(COLOUR_WHITE, text);

	/* Prompt for a key */
	c_put_str(COLOUR_L_BLUE, "Press any key to continue", h-3, (w/2) - 12);

	/* Wait for a key */
	Term_flush();
	ui_event ev = inkey_ex();

	/* Restore the screen */
	Term_load();
	return ev;
}

/**
 * Display the character on the screen (two different modes)
 *
 * The top two lines, and the bottom line (or two) are left blank.
 *
 * Mode 0 = standard display with skills/history
 * Mode 1 = special display with equipment flags
 * Mode 3 = ", 80x24 (for character dumps).
 * Mode 2 = special display with equipment flags and percentages
 */
void display_player(int mode)
{
	static int lastmode = -1;
	if (!have_valid_char_sheet_config() || (mode != lastmode)) {
		configure_char_sheet(mode == 3, mode == 2);
		lastmode = mode;
	}

	/* Erase screen */
	clear_from(0);

	/* When not playing, do not display in subwindows */
	if (Term != angband_term[0] && !player->upkeep->playing) return;

	switch(mode) {
		case 2:
			/* Other flags */
			display_player_flag_info(true);
			break;
		case 1:
		case 3:
			/* Stat/Sustain flags */
			display_player_sust_info(cached_config);

			/* Other flags */
			display_player_flag_info(false);
			break;
		case 0:
			/* Extra info */
			display_player_xtra_info(false);

			/* Stat info */
			display_player_stat_info(false);
	}

}


/**
 * Write a character dump
 */
void write_character_dump(ang_file *fff)
{
	int i, x, y, ylim;

	int a;
	wchar_t c;

	struct store *home = &stores[STORE_HOME];
	struct object **home_list = mem_zalloc(sizeof(struct object *) *
										   z_info->store_inven_max);
	char o_name[80];

	int n;
	char *buf, *p;

	configure_char_sheet(true, false);

	n = 90;
	if (n < 2 * cached_config->res_cols[0] + 1) {
		n = 2 * cached_config->res_cols[0] + 1;
	}
	buf = mem_alloc(text_wcsz() * n + 1);

	/* Begin dump */
	file_putf(fff, "  [%s Character Dump]\n\n", buildid);

	/* Display player basics */
	display_player(0);

	int width, height;
	Term_get_size(&width, &height);

	/* Dump part of the screen */
	for (y = 1; y < 23; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x <= MIN(89, width-1); x++) {
			/* Get the attr/char */
			(void)(Term_what(x, y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Display player resistances etc */
	display_player(1);

	/* Print a header */
	file_putf(fff, format("%-21s%s\n", "Resistances", "Sustains/Hindrances"));

	/* Dump part of the screen */
	ylim = ((cached_config->n_resist_by_region[0] >
		cached_config->n_resist_by_region[1]) ?
		cached_config->n_resist_by_region[0] :
		cached_config->n_resist_by_region[1]) +
		cached_config->res_regions[0].row + 2;
	for (y = cached_config->res_regions[0].row + 2; y < ylim; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x < 2 * cached_config->res_cols[0] + 1; x++) {
			/* Get the attr/char */
			(void)(Term_what(x, y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Skip a line */
	file_putf(fff, "\n");

	/* Print a header */
	file_putf(fff, format("%-21s%s\n", "Abilities", "Modifiers"));

	/* Dump part of the screen */
	ylim = ((cached_config->n_resist_by_region[2] >
		cached_config->n_resist_by_region[3]) ?
		cached_config->n_resist_by_region[2] :
		cached_config->n_resist_by_region[3]) +
		cached_config->res_regions[0].row + 2;
	for (y = cached_config->res_regions[0].row + 2; y < ylim; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x < 2 * cached_config->res_cols[0] + 1; x++) {
			/* Get the attr/char */
			(void)(Term_what(x + 2 * cached_config->res_cols[0], y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Skip some lines */
	file_putf(fff, "\n\n");


	/* If dead, dump last messages -- Prfnoff */
	if (player->is_dead) {
		i = messages_num();
		if (i > 15) i = 15;
		file_putf(fff, "  [Last Messages]\n\n");
		while (i-- > 0)
		{
			file_putf(fff, "> %s\n", message_str((int16_t)i));
		}
		file_putf(fff, "\nKilled by %s.\n\n", player->died_from);
	}


	/* Dump the equipment */
	file_putf(fff, "  [Character Equipment]\n\n");
	for (i = 0; i < player->body.count; i++) {
		struct object *obj = slot_object(player, i);
		if (!obj) continue;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the inventory */
	file_putf(fff, "\n\n  [Character Inventory]\n\n");
	for (i = 0; i < z_info->pack_size; i++) {
		struct object *obj = player->upkeep->inven[i];
		if (!obj) break;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the quiver */
	file_putf(fff, "\n\n  [Character Ammo]\n\n");
	for (i = 0; i < z_info->quiver_size; i++) {
		struct object *obj = player->upkeep->quiver[i];
		if (!obj) continue;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the Home -- if anything there */
	store_stock_list(home, home_list, z_info->store_inven_max);
	if (home->stock_num) {
		/* Header */
		file_putf(fff, "  [Home Inventory]\n\n");

		/* Dump all available items */
		for (i = 0; i < z_info->store_inven_max; i++) {
			struct object *obj = home_list[i];
			if (!obj) break;
			object_desc(o_name, sizeof(o_name), obj,
				ODESC_PREFIX | ODESC_FULL, player);
			file_putf(fff, "%c) %s\n", I2A(i), o_name);

			object_info_chardump(fff, obj, 5, 72);
		}

		/* Add an empty line */
		file_putf(fff, "\n\n");
	}

	/* Dump character history */
	dump_history(fff);
	file_putf(fff, "\n\n");

	/* Dump options */
	file_putf(fff, "  [Options]\n\n");

	/* Dump options */
	for (i = 0; i < OP_MAX; i++) {
		int opt;
		const char *title = "";
		switch (i) {
			case OP_MAP: title = "Overhead map display"; break;
			case OP_INTERFACE: title = "Other user interface"; break;
			case OP_BIRTH: title = "Birth"; break;
		    default: continue;
		}

		file_putf(fff, "  [%s]\n\n", title);
		for (opt = 0; opt < OPT_MAX; opt++) {
			if (option_type(opt) != i) continue;

			file_putf(fff, "%-45s: %s (%s)\n",
			        option_desc(opt),
			        player->opts.opt[opt] ? "yes" : "no ",
			        option_name(opt));
		}

		/* Skip some lines */
		file_putf(fff, "\n");
	}

	/*
	 * Display the randart seed, if applicable.  Use the same format as is
	 * used when constructing the randart file name.
	 */
	if (OPT(player, birth_randarts)) {
		file_putf(fff, "  [Randart seed]\n\n");
		file_putf(fff, "%08x\n\n", seed_randart);
	}

	mem_free(home_list);
	mem_free(buf);
}

/**
 * Save the lore to a file in the user directory.
 *
 * \param path is the path to the filename
 *
 * \returns true on success, false otherwise.
 */
bool dump_save(const char *path)
{
	if (text_lines_to_file(path, write_character_dump)) {
		msg("Failed to create file %s.new", path);
		return false;
	}

	return true;
}



#define INFO_SCREENS 3 /* Number of screens in character info mode */


/**
 * Hack -- change name
 */
void do_cmd_change_name(void)
{
	ui_event ke;
	int mode = 0;

	const char *p;

	bool more = true;

	/* Prompt */
	p = "['c' to change name, 'f' to file, 'h' to change mode, or ESC]";

	/* Save screen */
	screen_save();

	/* Forever */
	while (more) {
		/* Display the player */
		display_player(mode);

		/* Prompt */
		Term_putstr(2, 23, -1, COLOUR_WHITE, p);

		/* Query */
		ke = inkey_ex();

		if ((ke.type == EVT_KBRD) || (ke.type == EVT_BUTTON)) {
			switch (ke.key.code) {
				case ESCAPE: more = false; break;
				case 'c': {
					if(arg_force_name)
						msg("You are not allowed to change your name!");
					else {
					char namebuf[32] = "";

					/* Set player name */
					if (get_character_name(namebuf, sizeof namebuf))
						my_strcpy(player->full_name, namebuf,
								  sizeof(player->full_name));
					}

					break;
				}

				case 'f': {
					char buf[1024];
					char fname[80];

					/* Get the filesystem-safe name and append .txt */
					player_safe_name(fname, sizeof(fname), player->full_name, false);
					my_strcat(fname, ".txt", sizeof(fname));

					if (get_file(fname, buf, sizeof buf)) {
						if (dump_save(buf))
							msg("Character dump successful.");
						else
							msg("Character dump failed!");
					}
					break;
				}
				
				case 'h':
				case ARROW_LEFT:
				case ' ':
					mode = (mode + 1) % INFO_SCREENS;
					break;

				case 'l':
				case ARROW_RIGHT:
					mode = (mode - 1) % INFO_SCREENS;
					break;
			}
		} else if (ke.type == EVT_MOUSE) {
			if (ke.mouse.button == 1) {
				/* Flip through the screens */			
				mode = (mode + 1) % INFO_SCREENS;
			} else if (ke.mouse.button == 2) {
				/* exit the screen */
				more = false;
			} else {
				/* Flip backwards through the screens */			
				mode = (mode - 1) % INFO_SCREENS;
			}
		}

		/* Flush messages */
		event_signal(EVENT_MESSAGE_FLUSH);
	}

	/* Load screen */
	screen_load();
}


static void init_ui_player(void)
{
	/* Nothing to do; lazy initialization. */
}


static void cleanup_ui_player(void)
{
	release_char_sheet_config();
}


struct init_module ui_player_module = {
	.name = "ui-player",
	.init = init_ui_player,
	.cleanup = cleanup_ui_player
};
