/**
 * \file world.c
 * \brief World (surface, towns, wilderness): initialization, generation, etc.
 *
 * Copyright (c) 1997 Ben Harrison
 * Copyright (c) 2021 Mike Searle
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
 *
 * This file is used to initialize and run the world: the overall layout of
 *		towns, dungeons and any surface wilderness between them.
 *
 * The world array is built from the "world.txt" data file in the
 * "lib/gamedata" directory.
 */

#include "game-world.h"
#include "init.h"
#include "world.h"
#include "z-virt.h"

struct town {
	struct town **connect;		/* Array of connected towns */
	u32b connections;			/* Total number of connected towns */
	char *name;					/* Name of town */
};

struct town *t_info;

/* Arrays of names from town-names.txt */
static char **town_full_name;
static int town_full_names;
static char **town_front_name;
static int town_front_names;
static char **town_back_name;
static int town_back_names;

/**
 * Return the number of flight connections from a town
 **/
int world_connections(struct town *t)
{
	return t->connections;
}

/** Return any town matching the name, or NULL if none do.
 */
struct town *get_town_by_name(const char *name)
{
	for(int i=0; i<z_info->town_max; i++) {
		if (streq(t_info[i].name, name)) {
			return t_info + i;
		}
	}
	return NULL;
}

/** Select a random string from a string array */
static char *world_random_text(char **array, int length)
{
	return array[randint0(length)];
}

/** Generate a random name in static buffer
 */
static char *world_random_name(void)
{
	static char buf[64];
	char *front;
	char *mid;
	char *back;

	/* 1 in 5 are single names. The rest are combined from two halves. */
	if (one_in_(5)) {
		front = world_random_text(town_full_name, town_full_names);
		mid = "";
		back = "";
	} else {
		front = world_random_text(town_front_name, town_front_names);
		mid = " ";
		back = world_random_text(town_back_name, town_back_names);
	}

	strnfmt(buf, sizeof(buf), "%s%s%s", front, mid, back);
	return buf;
}

/** Generate a unique random name
 * Returned as string_make'd string
 */
static char *world_make_name(void)
{
	char *name;
	struct town *t;
	do
	{
		name = world_random_name();
		t = get_town_by_name(name);
	} while ((!name ) || (t));
	return string_make(name);
}

/** Set a town's name (passed name must be string_make'd) */
static void world_name_town(struct town *t, char *name)
{
	t->name = name;
}

/**
 * Add and return an empty town. Has a random name.
 */
static struct town *world_new_town(void)
{
	z_info->town_max++;
	t_info = mem_realloc(t_info, sizeof(struct town) * z_info->town_max);
	struct town *ret = t_info + (z_info->town_max - 1);
	memset(ret, 0, sizeof(*ret));
	world_name_town(ret, world_make_name());
	return ret;
}

/**
 * Generate towns
 */
void world_init_towns(void)
{
	/* Clear */
	if (t_info) {
		mem_free(t_info);
		t_info = NULL;
	}
	z_info->town_max = 0;

	struct town *t = world_new_town();
}

/**
 * ------------------------------------------------------------------------
 * Initialize town names
 * ------------------------------------------------------------------------ */

static enum parser_error parse_town_name(struct parser *p,  char ***names, int *n_names) {
	(*n_names)++;
	*names = mem_realloc(*names, sizeof(**names) * (*n_names));
	(*names)[(*n_names)-1] = string_make(parser_getstr(p, "text"));
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_town_name_simple(struct parser *p) {
	return parse_town_name(p, &town_full_name, &town_full_names);
}

static enum parser_error parse_town_name_front(struct parser *p) {
	return parse_town_name(p, &town_front_name, &town_front_names);
}

static enum parser_error parse_town_name_back(struct parser *p) {
	return parse_town_name(p, &town_back_name, &town_back_names);
}

struct parser *init_parse_town_names(void) {
	struct parser *p = parser_new();
	parser_reg(p, "S str text", parse_town_name_simple);
	parser_reg(p, "F str text", parse_town_name_front);
	parser_reg(p, "B str text", parse_town_name_back);
	return p;
}

static errr run_parse_town_names(struct parser *p) {
	return parse_file_quit_not_found(p, "town-names");
}

static errr finish_parse_town_names(struct parser *p) {
	parser_destroy(p);
	return 0;
}

static void cleanup_town_names(void)
{
	mem_free(town_full_name);
	mem_free(town_front_name);
	mem_free(town_back_name);
}

struct file_parser town_names_parser = {
	"town-names",
	init_parse_town_names,
	run_parse_town_names,
	finish_parse_town_names,
	cleanup_town_names
};

/**
 * ------------------------------------------------------------------------
 * Initialize world map
 * ------------------------------------------------------------------------ */
static enum parser_error parse_world_level(struct parser *p) {
	const int depth = parser_getint(p, "depth");
	const char *name = parser_getsym(p, "name");
	const char *up = parser_getsym(p, "up");
	const char *down = parser_getsym(p, "down");
	struct level *last = parser_priv(p);
	struct level *lev = mem_zalloc(sizeof *lev);

	if (last) {
		last->next = lev;
	} else {
		world = lev;
	}
	lev->depth = depth;
	lev->name = string_make(name);
	lev->up = streq(up, "None") ? NULL : string_make(up);
	lev->down = streq(down, "None") ? NULL : string_make(down);
	parser_setpriv(p, lev);
	return PARSE_ERROR_NONE;
}

struct parser *init_parse_world(void) {
	struct parser *p = parser_new();

	parser_reg(p, "level int depth sym name sym up sym down",
			   parse_world_level);
	return p;
}

static errr run_parse_world(struct parser *p) {
	return parse_file_quit_not_found(p, "world");
}

static errr finish_parse_world(struct parser *p) {
	struct level *level_check;

	/* Check that all levels referred to exist */
	for (level_check = world; level_check; level_check = level_check->next) {
		struct level *level_find = world;

		/* Check upwards */
		if (level_check->up) {
			while (level_find && !streq(level_check->up, level_find->name)) {
				level_find = level_find->next;
			}
			if (!level_find) {
				quit_fmt("Invalid level reference %s", level_check->up);
			}
		}

		/* Check downwards */
		level_find = world;
		if (level_check->down) {
			while (level_find && !streq(level_check->down, level_find->name)) {
				level_find = level_find->next;
			}
			if (!level_find) {
				quit_fmt("Invalid level reference %s", level_check->down);
			}
		}
	}

	parser_destroy(p);
	return 0;
}

static void cleanup_world(void)
{
	struct level *level = world;
	while (level) {
		struct level *old = level;
		string_free(level->name);
		string_free(level->up);
		string_free(level->down);
		level = level->next;
		mem_free(old);
	}
}

struct file_parser world_parser = {
	"world",
	init_parse_world,
	run_parse_world,
	finish_parse_world,
	cleanup_world
};

