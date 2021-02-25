/**
 * \file world.h
 * \brief World (surface, towns, wilderness): interface
 *
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
 */

struct town;

/* The world contains z->town_max towns, in this array */
extern struct town *t_info;

extern struct file_parser world_parser;
extern struct file_parser town_names_parser;

extern int world_connections(struct town *t);

extern void world_init_towns(void);
