/**
 * \file game-world.h
 * \brief Game core management of the game world
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

#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include "cave.h"
#include "z-rand.h"

struct level {
	int depth;
	char *name;
	char *up;
	char *down;
	struct level *next;
};

struct dungeon {
	random_value min;
	random_value max;
	char *name;
	struct dungeon *next;
};

extern uint16_t daycount;
extern uint32_t seed_randart;
extern uint32_t seed_flavor;
extern int32_t turn;
extern bool character_generated;
extern bool character_dungeon;
extern uint8_t *extract_energy;
extern int n_extract_energy;

extern struct level *world;

struct level *level_by_name(const char *name);
void increase_danger_level(void);
struct level *level_by_depth(int depth);
bool is_daytime(void);
int turn_energy(int speed);
void play_ambient_sound(void);
void process_world(struct chunk *c);
void on_new_level(void);
void process_player(void);
void run_game_loop(void);
char *format_time(int turns);
char *format_duration(int turns);

#endif /* !GAME_WORLD_H */
