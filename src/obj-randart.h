/**
 * \file obj-randart.h
 * \brief Random artifact generation
 *
 * Copyright (c) 1998 Greg Wooledge, Ben Harrison, Robert Ruhlmann
 * Copyright (c) 2001 Chris Carr, Chris Robertson
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
 * Original random artifact generator (randart) by Greg Wooledge.
 * Updated by Chris Carr / Chris Robertson 2001-2010.
 */

#ifndef OBJECT_RANDART_H
#define OBJECT_RANDART_H

#include "object.h"

#define MAX_TRIES 200
#define BUFLEN 1024

#define MIN_NAME_LEN 5
#define MAX_NAME_LEN 9

/**
 * Inhibiting factors for large bonus values
 * "HIGH" values use INHIBIT_WEAK
 * "VERYHIGH" values use INHIBIT_STRONG
 */
#define INHIBIT_STRONG  (randint0(100) < z_info->inhibit_strong)
#define INHIBIT_WEAK    (randint0(100) < z_info->inhibit_weak)

/**
 * Numerical index values for the different learned probabilities
 * These are to make the code more readable.
 */
enum {
	#define ART_IDX(a, b, c) ART_IDX_##a,
	#include "list-randart-properties.h"
	#undef ART_IDX
};

struct artifact_set_data {
	/* Mean start and increment values for to_hit, to_dam and AC */
	int hit_increment;
	int dam_increment;
	int hit_startval;
	int dam_startval;
	int ac_startval;
	int ac_increment;

	/* Data structures for learned probabilities */
	int *art_probs;
	int *tv_probs;
	int *tv_num;
	int gun_total;
	int melee_total;
	int boot_total;
	int glove_total;
	int headgear_total;
	int shield_total;
	int cloak_total;
	int belt_total;
	int armor_total;
	int other_total;
	int total;
	int neg_power_total;

	/* Tval frequency values */
	int *tv_freq;

	/* Artifact power ratings */
	int *base_power;
	int max_power;
	int min_power;
	int avg_power;
	int var_power;
	int *avg_tv_power;
	int *min_tv_power;
	int *max_tv_power;
	int *power;
	bool *bad;

	/* Base item levels */
	int *base_item_level;

	/* Base item rarities */
	int *base_item_prob;

	/* Artifact rarities */
	int *base_art_alloc;

	/* Set if name has been used */
	bool *name_used;
};

char *artifact_gen_name(struct artifact_set_data *data, struct artifact *a, const char ***words, int power, int tval, bool bad);
void do_randart(uint32_t randart_seed, bool create_file, bool qa_only);
extern struct file_parser artinames_parser;
bool new_random_artifact(struct object *obj, struct artifact *art, int power);
struct artifact_set_data *artifact_set_data_new(void);

#endif /* OBJECT_RANDART_H */
