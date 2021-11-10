/**
 * \file obj-make.c
 * \brief Object generation functions.
 *
 * Copyright (c) 1987-2007 Angband contributors
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
#include "alloc.h"
#include "cave.h"
#include "effects.h"
#include "init.h"
#include "obj-chest.h"
#include "obj-fault.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-init.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-power.h"
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"

#include <math.h>
#include <time.h>

/** Let the stats see into the artifact probability table */
extern double *wiz_stats_prob;

/**
 * Stores cumulative probability distribution for objects at each level.  The
 * value at ilv * (z_info->k_max + 1) + itm is the probability, out of
 * obj_alloc[ilv * (z_info->k_max + 1) + z_info->k_max], that an item whose
 * index is less than itm occurs at level, ilv.
 */
static double *obj_alloc;

/**
 * Has the same layout and interpretation as obj_alloc, but only items that
 * are good or better contribute to the cumulative probability distribution.
 */
static double *obj_alloc_great;

/**
 * Store the total allocation value for each tval by level.  The value at
 * ilv * TV_MAX + tval is the total for tval at the level, ilv.
 */
static double *obj_total_tval;

/**
 * Same layout and interpretation as obj_total_tval, but only items that are
 * good or better contribute.
 */
static double *obj_total_tval_great;

static alloc_entry *alloc_ego_table;

struct money {
	char *name;
	int type;
};

struct multiego_entry
{
	double prob;
	u16b ego[MAX_EGOS];
};

static struct money *money_type;
static int num_money_types;

static bool kind_is_good(const struct object_kind *kind, int level);

/*
 * Initialize object allocation info
 */
static void alloc_init_objects(void) {
	int item, lev;
	int k_max = z_info->k_max;

	/* Allocate */
	obj_alloc = mem_alloc_alt((z_info->max_obj_depth + 1) * (k_max + 1) * sizeof(*obj_alloc));
	obj_alloc_great = mem_alloc_alt((z_info->max_obj_depth + 1) * (k_max + 1) * sizeof(*obj_alloc_great));
	obj_total_tval = mem_zalloc_alt((z_info->max_obj_depth + 1) * TV_MAX * sizeof(*obj_total_tval));
	obj_total_tval_great = mem_zalloc_alt((z_info->max_obj_depth + 1) * TV_MAX * sizeof(*obj_total_tval));

	/* The cumulative chance starts at zero for each level. */
	for (lev = 0; lev <= z_info->max_obj_depth; lev++) {
		obj_alloc[lev * (k_max + 1)] = 0;
		obj_alloc_great[lev * (k_max + 1)] = 0;
	}

	/* Fill the cumulative probability tables */
	for (item = 0; item < k_max; item++) {
		const struct object_kind *kind = &k_info[item];

		int min = kind->alloc_min;
		int max = kind->alloc_max;

		/* Go through all the dungeon levels */
		for (lev = 0; lev <= z_info->max_obj_depth; lev++) {
			double rarity = kind->alloc_prob;

			/* Add to the cumulative prob. in the standard table */
			double scale = 0.0;
			if (max > min) {
				if (lev > max) {
					;
				} else if (lev >= min) {
					scale = 1.0 - ((double)(lev - min) / (double)(max - min));
				} else {
					scale = 1.0 / (1.0 + (min - lev));
				}
			}
			rarity *= scale;
			obj_alloc[(lev * (k_max + 1)) + item + 1] =
				obj_alloc[(lev * (k_max + 1)) + item] + rarity;

			obj_total_tval[lev * TV_MAX + kind->tval] += rarity;
			/* Add to the cumulative prob. in the "great" table */
			if (!kind_is_good(kind, lev))
				rarity = 0;
			obj_alloc_great[(lev * (k_max + 1)) + item + 1] =
				obj_alloc_great[(lev * (k_max + 1)) + item] + rarity;
			obj_total_tval_great[lev * TV_MAX + kind->tval] += rarity;
		}
	}
}

/*
 * Initialize ego-item allocation info
 */
static void alloc_init_egos(void) {
	/* Allocate the alloc_ego_table */
	alloc_ego_table = mem_zalloc(z_info->e_max * sizeof(alloc_entry));

	/* Scan the ego-items */
	for (int i = 0; i < z_info->e_max; i++) {
		struct ego_item *ego = &e_info[i];

		/* Load the entry */
		alloc_ego_table[i].index = i;
		alloc_ego_table[i].prob2 = ego->alloc_prob;
	}
}

/*
 * Initialize money info
 */
static void init_money_svals(void)
{
	int *money_svals;
	int i;

	/* Count the money types and make a list */
	num_money_types = tval_sval_count("cash");
	money_type = mem_zalloc(num_money_types * sizeof(struct money));
	money_svals = mem_zalloc(num_money_types * sizeof(struct money));
	assert(num_money_types);
	tval_sval_list("cash", money_svals, num_money_types);

	/* List the money types */
	for (i = 0; i < num_money_types; i++) {
		struct object_kind *kind = lookup_kind(TV_GOLD, money_svals[i]);
		money_type[i].name = string_make(kind->name);
		money_type[i].type = money_svals[i];
	}

	mem_free(money_svals);
}

static void init_obj_make(void) {
	alloc_init_objects();
	alloc_init_egos();
	init_money_svals();
}

static void cleanup_obj_make(void) {
	int i;
	for (i = 0; i < num_money_types; i++) {
		string_free(money_type[i].name);
	}
	mem_free(money_type);
	mem_free(alloc_ego_table);
	mem_free_alt(obj_total_tval_great);
	mem_free_alt(obj_total_tval);
	mem_free_alt(obj_alloc_great);
	mem_free_alt(obj_alloc);
}

/*** Make an ego item ***/

/**
 * This is a safe way to choose a random new flag to add to an object.
 * It takes the existing flags and an array of new flags,
 * and returns an entry from newf, or 0 if there are no
 * new flags available.
 */
static int get_new_attr(bitflag flags[OF_SIZE], bitflag newf[OF_SIZE])
{
	size_t i;
	int options = 0, flag = 0;

	for (i = of_next(newf, FLAG_START); i != FLAG_END; i = of_next(newf, i + 1))
	{
		/* skip this one if the flag is already present */
		if (of_has(flags, i)) continue;

		/* each time we find a new possible option, we have a 1-in-N chance of
		 * choosing it and an (N-1)-in-N chance of keeping a previous one */
		if (one_in_(++options)) flag = i;
	}

	return flag;
}

/**
 * Get a random new resist on an item.
 * Picked randomly from the range >= from < to
 * Always picks from the lowest level of resistance.
 * Will increase to any level of resistance but not to immunity (so returns false only if every resistance
 * is already at immunity-1)
 */
static int random_resist(struct object *obj, int *resist, int from, int to)
{
	int i, count = 0;
	int low = IMMUNITY;

	/* Find the lowest base resist */
	for (i = from; i < to; i++)
		if (obj->el_info[i].res_level < low)
			low = obj->el_info[i].res_level;

	/* Count the available base resists */
	for (i = from; i < to; i++)
		if (obj->el_info[i].res_level == low) count++;

	if (count == 0)
		low++;

	/* Pick one */
	do {
		*resist = rand_range(from, to-1);
	} while (obj->el_info[*resist].res_level < low);

	/* Fail only if everything is immune */
	if (obj->el_info[*resist].res_level >= IMMUNITY-1)
		return false;

	return true;
}

/**
 * Get a random new base resist on an item
 */
static int random_base_resist(struct object *obj, int *resist)
{
	return random_resist(obj, resist, ELEM_BASE_MIN, ELEM_HIGH_MIN);
}

/**
 * Get a random new high resist on an item
 */
static int random_high_resist(struct object *obj, int *resist)
{
	return random_resist(obj, resist, ELEM_HIGH_MIN, ELEM_HIGH_MAX);
}


/**
 * Return the index, i, into the cumulative probability table, tbl, such that
 * tbl[i] <= p < tbl[i + 1].  p must be less than tbl[n - 1] and tbl[0] must be
 * zero.
 */
static int binary_search_probtable(const double *tbl, int n, double p)
{
	int ilow = 0, ihigh = n;

	assert(tbl[0] == 0 && tbl[n - 1] > p);
	while (1) {
		int imid;

		if (ilow == ihigh - 1) {
			assert(tbl[ilow] <= p && tbl[ihigh] > p);
			return ilow;
		}
		imid = (ilow + ihigh) / 2;
		if (tbl[imid] <= p) {
			ilow = imid;
		} else {
			ihigh = imid;
		}
	}
}

/**
 * Randomly select from a table of probabilities
 * prob is a table of doubles, total is the sum of these values
 * Returns an index into the table
 */
 static int select_random_table(double total, double *prob, int length) {
	double value = Rand_double(total);
	int last = -1;
fprintf(stderr,"select_random_table: total %lf, length %d\n", total, length);
	for (int i=0;i<length; i++) {
		if (prob[i] > 0.0) {
fprintf(stderr,"select_random_table: entry %d has prob %lf, ", i, prob[i]);
			last = i;
			if (value < prob[i]) {
				fprintf(stderr,"returning\n");
				return i;
			} else {
				value -= prob[i];
				fprintf(stderr,"new value %lf\n", value);
			}
		}
	}
	assert(last >= 0);
	return last;
}

/**
 * Select an ego for an existing object
 * Used by branding
 */
struct ego_item *select_ego_base(int level, struct object *obj)
{
	struct poss_item *poss;
	double *prob = mem_zalloc(sizeof(*prob) * z_info->e_max);
	double total = 0.0;
	struct object_kind *kind = obj->kind;
	u16b egoi[MAX_EGOS+1];
	int negos = 0;
	for(int i=0;i<MAX_EGOS;i++) {
		if (obj->ego[i]) {
			negos++;
			egoi[i] = obj->ego[i]->eidx;
		} else {
			egoi[i] = 0;
		}
	}

	/* Fill a table of usable base items */
	for(int i=0;i<z_info->e_max;i++) {
		bool ok = false;
		for (poss = e_info[i].poss_items; poss; poss = poss->next) {
			if (poss->kidx == kind->kidx) {
				egoi[negos] = i;
				if (multiego_allow(egoi)) {
					ok = true;
					for(int j=0;j<negos;j++)
						if (egoi[j] == i)
							ok = false;
					if (ok)
						break;
				}
			}
		}
		if (ok) {
			prob[i] = e_info[i].alloc_prob;
			if (level < e_info[i].alloc_min)
				prob[i] /= (e_info[i].alloc_min - level) + 0.5;
			if (level > e_info[i].alloc_max)
				prob[i] /= (level - e_info[i].alloc_min) + 0.5;
			total += prob[i];
		}
	}

	/* No possibilities */
	if (total == 0.0) {
		mem_free(prob);
		return NULL;
	}

	/* Select at random */
	int idx = select_random_table(total, prob, z_info->e_max);
	mem_free(prob);
	return e_info + idx;
}

/**
 * Select a base item from a possible-item table.
 */
struct object_kind *select_poss_kind(struct poss_item *poss, int level, int tval)
{
	double *prob = mem_zalloc(sizeof(*prob) * z_info->k_max);
	double total = 0.0;

	/* Fill a table of usable base items */
	for (; poss; poss = poss->next) {
		if ((tval == 0) || (tval == k_info[poss->kidx].tval)) {
			assert(poss->kidx);
			double newprob = obj_alloc[poss->kidx+1] - obj_alloc[poss->kidx];
			newprob *= poss->scale;
			prob[poss->kidx] += newprob;
			total += newprob;
fprintf(stderr,"Item %s: new prob %lf, this prob %lf, total %lf\n",k_info[poss->kidx].name, newprob, prob[poss->kidx], total);
		}
	}

	/* No possibilities */
	if (total == 0.0) {
		mem_free(prob);
		return NULL;
	}

	/* Select at random */
	int idx = select_random_table(total, prob, z_info->k_max);
	mem_free(prob);
	return k_info + idx;
}

/**
 * Select a base item for an ego.
 */
struct object_kind *select_ego_kind(const struct ego_item *ego, int level, int tval)
{
	return select_poss_kind(ego->poss_items, level, tval);
}

/**
 * Select a base item for a multiego.
 * This selects one of the component egos at random, generates a random kind for that ego,
 * then tests it against all the other egos. If all match it is returned.
 * 
 * It will return NULL only if select_ego_kind does.
 */
static struct object_kind *select_multiego_kind(struct multiego_entry *me, int level, int tval)
{
	struct object_kind *kind = NULL;
	int matches = 0;
	do {
		int first = randint0(MAX_EGOS);
		kind = select_ego_kind(e_info + me->ego[first], level, tval);
		if (!kind)
			return NULL;
		matches = 0;
		for(int i=0;i<MAX_EGOS;i++) {
			if (i != first) {
				struct poss_item *poss = e_info[me->ego[i]].poss_items;
				while (poss) {
					if (poss->kidx == kind->kidx) {
						matches++;
						break;
					}
					poss = poss->next;
				}
			}
		}
	} while (matches < MAX_EGOS-1);

	return kind;
}

/**
 * Return true if at least one item has a matching tval
 */
static bool ego_can_use_tval(struct ego_item *ego, int tval)
{
	struct poss_item *poss;

	for (poss = ego->poss_items; poss; poss = poss->next) {
		if (tval == k_info[poss->kidx].tval)
			return true;
	}

	return false;
}

/**
 * Select an ego-item at random, based on the level.
 */
static struct ego_item *ego_find_random(int level, int tval)
{
	int i;
	double *prob = mem_zalloc(sizeof(*prob) * z_info->e_max);
	struct alloc_entry *table = alloc_ego_table;
	double total = 0.0;

	/* Go through all possible ego items and find ones which are possible */
	for (i = 0; i < z_info->e_max; i++) {
		struct ego_item *ego = &e_info[i];
		double p = 0.0;

		if ((tval == 0) || (ego_can_use_tval(ego, tval))) {
			if (level <= ego->alloc_max) {
				p = table[i].prob2;
				if (level >= ego->alloc_min) {
					/* Between min and max levels - scale linearly
					 * from maximum probability at native depth to
					 * zero at maximum depth + 1
					 **/
					p *= (ego->alloc_max + 1) - level;
					p /= (ego->alloc_max + 1) - ego->alloc_min;
				} else {
					/* Out of depth.
					 * Divide by the # of levels OOD, * a constant
					 **/
					p /= 1.0 + (((double)(ego->alloc_min - level)) / 3.0);
				}
			}
		}

		prob[i] = p;
		total += prob[i];
	}

	/* No possibilities */
	if (total == 0.0) {
		mem_free(prob);
		return NULL;
	}

	/* Select at random */
	int idx = select_random_table(total, prob, z_info->e_max);
	mem_free(prob);
	return e_info + idx;
}


static void apply_random_high_resist(struct object *obj)
{
	int resist = 0;

	/* Get a high resist if available, mark it as random
	 * If none are available, try for a low resist
	 **/
	if (random_high_resist(obj, &resist)) {
		obj->el_info[resist].res_level++;
		obj->el_info[resist].flags |= EL_INFO_RANDOM | EL_INFO_IGNORE;
	} else {
		if (random_base_resist(obj, &resist)) {
			obj->el_info[resist].res_level++;
			obj->el_info[resist].flags |= EL_INFO_RANDOM | EL_INFO_IGNORE;
		}
	}
}

static void apply_random_low_resist(struct object *obj)
{
	int resist = 0;

	/* Get a base resist if available, mark it as random
	 * If none are available, try for a high resist
	 **/
	if (random_base_resist(obj, &resist)) {
		obj->el_info[resist].res_level++;
		obj->el_info[resist].flags |= EL_INFO_RANDOM | EL_INFO_IGNORE;
	} else {
		if (random_high_resist(obj, &resist)) {
			obj->el_info[resist].res_level++;
			obj->el_info[resist].flags |= EL_INFO_RANDOM | EL_INFO_IGNORE;
		}
	}
}

static void apply_random_power(struct object *obj)
{
	bitflag newf[OF_SIZE];
	create_obj_flag_mask(newf, false, OFT_PROT, OFT_MISC, OFT_MAX);
	int flag = get_new_attr(obj->flags, newf);
	if (flag)
		of_on(obj->flags, flag);
}

static void apply_random_sustain(struct object *obj)
{
	bitflag newf[OF_SIZE];
	create_obj_flag_mask(newf, false, OFT_SUST, OFT_MAX);
	int flag = get_new_attr(obj->flags, newf);
	if (flag)
		of_on(obj->flags, flag);
}

/**
 * Apply random resistances, sustains, powers etc.
 */
static void apply_random_powers(struct object *obj, bitflag *kind_flags)
{
	/* Resist or power? */
	if (kf_has(kind_flags, KF_RAND_RES_POWER)) {
		do {
			switch(randint0(6)) {
				case 0:
					apply_random_high_resist(obj);
					break;
				case 1:
				case 2:
					apply_random_low_resist(obj);
					break;
				default:
					apply_random_power(obj);
			}
		} while (one_in_(10));
	}

	if (kf_has(kind_flags, KF_RAND_SUSTAIN)) {
		do {
			apply_random_sustain(obj);
		} while (one_in_(4));
	}

	if (kf_has(kind_flags, KF_RAND_POWER)) {
		do {
			apply_random_power(obj);
		} while (one_in_(10));
	}

	if (kf_has(kind_flags, KF_RAND_HI_RES)) {
		do {
			apply_random_high_resist(obj);
		} while (one_in_(10));
	}

	if (kf_has(kind_flags, KF_RAND_BASE_RES)) {
		do {
			apply_random_low_resist(obj);
		} while (one_in_(6));
	}
}


/**
 * Apply generation magic to an ego-item from a given ego index 'j'.
 * Used because brand_object needs to add magic to an item that is already an ego item.
 */
void ego_apply_magic_from(struct object *obj, int level, int j)
{
	struct ego_item *ego;
	int i, x;

	ego = obj->ego[j];
	if (ego) {

		/* Extra powers */
		apply_random_powers(obj, ego->kind_flags);

		/* Apply extra ego bonuses */
		obj->to_h += randcalc(ego->to_h, level, RANDOMISE);
		obj->to_d += randcalc(ego->to_d, level, RANDOMISE);
		obj->to_a += randcalc(ego->to_a, level, RANDOMISE);

		/* Change weight */
		int weight = randcalc(ego->weight, level, RANDOMISE);
		if (weight) {
			if (weight < 0)
				weight = 0;
			obj->weight = (obj->weight * weight) / 100;
		}

		/* Apply pval - maximum (with unchanged timeout) by default, otherwise
		 * use a % scaling factor to pval and timeout
		 **/
		int ego_pval = randcalc(obj->ego[j]->pval, level, RANDOMISE);
		if (kf_has(obj->ego[j]->kind_flags, KF_PVAL_SCALE)) {
			obj->pval = ((obj->pval * ego_pval) / 100);
			obj->timeout = ((obj->timeout * ego_pval) / 100);
		} else {
			obj->pval = MAX(obj->pval, ego_pval);
		}

		/* Apply modifiers */
		for (i = 0; i < OBJ_MOD_MAX; i++) {
			x = randcalc(ego->modifiers[i], level, RANDOMISE);
			obj->modifiers[i] += x;
		}

		/* Apply flags */
		of_union(obj->flags, ego->flags);
		of_diff(obj->flags, ego->flags_off);
		of_union(obj->carried_flags, ego->carried_flags);
		of_diff(obj->carried_flags, ego->carried_flags_off);
		pf_union(obj->pflags, ego->pflags);

		/* Add slays, brands and faults */
		copy_slays(&obj->slays, ego->slays);
		copy_brands(&obj->brands, ego->brands);
		copy_faults(obj, ego->faults);

		/* Add resists */
		for (i = 0; i < ELEM_MAX; i++) {
			/* Take the sum of ego and base object resist levels (clipped at immunity) */
			obj->el_info[i].res_level = MIN(ego->el_info[i].res_level + obj->el_info[i].res_level, IMMUNITY);

			/* Union of flags so as to know when ignoring is notable */
			obj->el_info[i].flags |= ego->el_info[i].flags;
		}

		/* Add effect (ego effect will trump object effect, when there are any) */
		if (ego->effect) {
			obj->effect = ego->effect;
			obj->time = ego->time;
		}
	}
}

/**
 * Apply generation magic to an ego-item.
 * This should do nothing if called on a non-ego item, and handle single and
 * multiple ego items.
 * It should however only be called once - branding must use only ego_apply_magic_from()
 * on the newly added ego.
 */
void ego_apply_magic(struct object *obj, int level)
{
	for(int j=0;j<MAX_EGOS;j++)
		ego_apply_magic_from(obj, level, j);
}

/**
 * Apply minimum standards for ego-items.
 */
static void ego_apply_minima(struct object *obj)
{
	int i;
	for(int j=0;j<MAX_EGOS;j++) {
		if (obj->ego[j]) {

			if (obj->ego[j]->min_to_h != NO_MINIMUM &&
					obj->to_h < obj->ego[j]->min_to_h)
				obj->to_h = obj->ego[j]->min_to_h;
			if (obj->ego[j]->min_to_d != NO_MINIMUM &&
					obj->to_d < obj->ego[j]->min_to_d)
				obj->to_d = obj->ego[j]->min_to_d;
			if (obj->ego[j]->min_to_a != NO_MINIMUM &&
					obj->to_a < obj->ego[j]->min_to_a)
				obj->to_a = obj->ego[j]->min_to_a;

			for (i = 0; i < OBJ_MOD_MAX; i++) {
				if (obj->modifiers[i] < obj->ego[j]->min_modifiers[i])
					obj->modifiers[i] = obj->ego[j]->min_modifiers[i];
			}
		}
	}
}


/**
 * Try to find an ego-item for an object, setting obj->ego if successful and
 * applying various bonuses.
 */
static struct ego_item *find_ego_item(int level, int tval)
{
	/* Try to get a legal ego type for this item */
	return ego_find_random(level, tval);
}


/*** Make an artifact ***/

/**
 * Copy artifact data to a normal object.
 */
void copy_artifact_data(struct object *obj, const struct artifact *art)
{
	int i;
	struct object_kind *kind = lookup_kind(art->tval, art->sval);

	/* Extract the data */
	for (i = 0; i < OBJ_MOD_MAX; i++)
		obj->modifiers[i] = art->modifiers[i];
	obj->ac = art->ac;
	obj->dd = art->dd;
	obj->ds = art->ds;
	obj->to_a = art->to_a;
	obj->to_h = art->to_h;
	obj->to_d = art->to_d;
	obj->weight = art->weight;

	/* Activations can come from the artifact or the kind */
	if (art->activation) {
		obj->activation = art->activation;
		obj->time = art->time;
	} else if (kind->activation) {
		obj->activation = kind->activation;
		obj->time = kind->time;
	}

	/* Fix for artifact lights */
	of_off(obj->flags, OF_TAKES_FUEL);
	of_off(obj->flags, OF_BURNS_OUT);

	/* Timeouts are always 0 */
	obj->timeout = 0;

	of_union(obj->flags, art->flags);
	of_union(obj->carried_flags, art->carried_flags);
	pf_union(obj->pflags, art->pflags);
	copy_slays(&obj->slays, art->slays);
	copy_brands(&obj->brands, art->brands);
	copy_faults(obj, art->faults);
	for (i = 0; i < ELEM_MAX; i++) {
		/* Take the larger of artifact and base object resist levels */
		obj->el_info[i].res_level =
			MAX(art->el_info[i].res_level, obj->el_info[i].res_level);

		/* Union of flags so as to know when ignoring is notable */
		obj->el_info[i].flags |= art->el_info[i].flags;
	}
}

static double make_artifact_probs(double *prob, int lev, int tval, bool max)
{
	int i;
	double total = 0.0;

	/* No artifacts, do nothing */
	if (OPT(player, birth_no_artifacts)) return 0.0;

	/* No artifacts in the town */
	if (!player->depth) return 0.0;

	for (i = 0; i < z_info->a_max; ++i) {
		const struct artifact *art = &a_info[i];
		struct object_kind *kind = lookup_kind(art->tval, art->sval);

		/* No chance by default */
		prob[i] = 0.0;

		/* Skip "empty" items */
		if (!art->name) continue;

		/* Make sure the kind was found */
		if (!kind) continue;

		/* Special generation */
		if (kf_has(kind->kind_flags, KF_QUEST_ART)) continue;

		/* Cannot make an artifact twice */
		if (is_artifact_created(art)) continue;

		/* Must have the correct tval, if one is provided */
		if ((tval != 0) && (art->tval != tval)) continue;

		/* Rarity - sets the basic probability */
		prob[i] = art->alloc_prob;

		/* Enforce maximum depth (strictly, if max mode is set) */
		if (art->alloc_max <= lev) {
			if (max) {
				prob[i] = 0.0;
			} else {
				/* Get the "out-of-depth factor" */
				prob[i] /= ((lev - art->alloc_max) + 1) * 10;
			}
		}

		/* Enforce minimum "depth" (loosely) */
		else if (art->alloc_min > lev) {
			/* Get the "out-of-depth factor" */
			prob[i] /= 1.0 + ((art->alloc_min - lev) * (art->alloc_min - lev) * 0.1);
		}

		/* If in depth, reduce probability at higher levels */
		else {
			prob[i] *= (art->alloc_max + 1) - lev;
			prob[i] /= (art->alloc_max + 1) - art->alloc_min;
		}

		total += prob[i];
	}
	return total;
}

/**
 * Attempt to create an artifact.
 *
 * With the exceptions of being in town or having artifacts turned
 * off, this will always create an artifact if there are any artifacts
 * left to create - or if tval is specified, any artifacts left of
 * that tval. This means that even though the maximum depth is
 * otherwise enforced strictly, if this is the only way to produce
 * an artifact then the max depth will be treated as a soft limit.
 *
 * This routine should only be called by "apply_magic()" and stats.
 *
 * Note -- see "apply_magic()"
 */
struct object *make_artifact(int lev, int tval)
{
	static double *prob;
	static double total;
	struct object *obj = NULL;

	/* Make sure birth no artifacts isn't set */
	if (OPT(player, birth_no_artifacts)) return NULL;

	/* No artifacts in the town */
	if (!player->depth) return NULL;

	if (!prob) {
		prob = mem_zalloc(z_info->a_max * sizeof(double));
		wiz_stats_prob = prob;
	}

	/* Check the artifact list */
	total = make_artifact_probs(prob, lev, tval, true);
	if (total == 0.0) {
		/* No matches. Try with loose maximum depth */
		total = make_artifact_probs(prob, lev, tval, false);
	}

	/* Still nothing - give up */
	if (total == 0.0)
		return NULL;

	/* Select an artifact.
	 * Generate a random value between 0 and the total probability, scan the array until
	 * the running sum exceeds it.
	 **/
	double rand = Rand_double(total);
	double sum = 0.0;
	int a_idx = 0;
	for (a_idx=0;a_idx<z_info->a_max;a_idx++) {
		sum += prob[a_idx];
		if (rand < sum) {
			break;
		}
	}

	assert(a_idx < z_info->a_max);

	/* Generate the base item */
	const struct artifact *art = &a_info[a_idx];

	/* The table should not contain any already existing artifacts */ 
	assert(!(is_artifact_created(art)));

	struct object_kind *kind = lookup_kind(art->tval, art->sval);
	obj = object_new();
	object_prep(obj, kind, art->alloc_min, RANDOMISE);

	/* Mark the item as an artifact */
	obj->artifact = art;
	copy_artifact_data(obj, obj->artifact);

	/* Paranoia -- no "plural" artifacts */
	if (obj->number != 1) return NULL;

	mark_artifact_created(obj->artifact, true);

	return obj;
}


/**
 * Create a fake artifact directly from a blank object
 *
 * This function is used for describing artifacts, and for creating them for
 * debugging.
 *
 * Since this is now in no way marked as fake, we must make sure this function
 * is never used to create an actual game object
 */
bool make_fake_artifact(struct object *obj, const struct artifact *artifact)
{
	struct object_kind *kind;

	/* Don't bother with empty artifacts */
	if (!artifact->tval) return false;

	/* Get the "kind" index */
	kind = lookup_kind(artifact->tval, artifact->sval);
	if (!kind) return false;

	/* Create the artifact */
	object_prep(obj, kind, 0, MAXIMISE);
	obj->artifact = artifact;
	copy_artifact_data(obj, artifact);

	return (true);
}


/*** Apply magic to an item ***/

/**
 * Apply magic to a weapon.
 */
static void apply_magic_weapon(struct object *obj, int level, int power)
{
	if (power <= 0)
		return;

	obj->to_h += randint1(5) + m_bonus(5, level);
	obj->to_d += randint1(5) + m_bonus(5, level);

	if (power > 1) {
		obj->to_h += m_bonus(10, level);
		obj->to_d += m_bonus(10, level);

		if (tval_is_melee_weapon(obj)) {
			/* Super-charge the damage dice */
			while ((obj->dd * obj->ds > 0) && one_in_(4 * obj->dd * obj->ds)) {
				/* More dice or sides means more likely to get still more */
				if (randint0(obj->dd + obj->ds) < obj->dd) {
					int newdice = randint1(2 + obj->dd/obj->ds);
					while (((obj->dd + 1) * obj->ds <= 40) && newdice) {
						if (!one_in_(3)) {
							obj->dd++;
						}
						newdice--;
					}
				} else {
					int newsides = randint1(2 + obj->ds/obj->dd);
					while ((obj->dd * (obj->ds + 1) <= 40) && newsides) {
						if (!one_in_(3)) {
							obj->ds++;
						}
						newsides--;
					}
				}
			}
		} else if (tval_is_ammo(obj)) {
			/* Up to two chances to enhance damage dice. */
			if (one_in_(6) == 1) {
				obj->ds++;
				if (one_in_(10) == 1) {
					obj->ds++;
				}
			}
		}
	}
}


/**
 * Apply magic to armour
 */
static void apply_magic_armour(struct object *obj, int level, int power)
{
	if (power <= 0)
		return;

	obj->to_a += randint1(5) + m_bonus(5, level);
	if (power > 1)
		obj->to_a += m_bonus(10, level);
}


/**
 * Wipe an object clean and make it a standard object of the specified kind.
 */
void object_prep(struct object *obj, struct object_kind *k, int lev,
				 aspect rand_aspect)
{
	int i;

	/* Clean slate */
	memset(obj, 0, sizeof(*obj));

	/* Assign the kind and copy across data */
	obj->kind = k;
	obj->tval = k->tval;
	obj->sval = k->sval;
	obj->ac = k->ac;
	obj->dd = k->dd;
	obj->ds = k->ds;
	obj->weight = k->weight;
	obj->effect = k->effect;
	obj->time = k->time;

	/* Default number */
	obj->number = 1;

	/* Copy flags */
	of_copy(obj->flags, k->base->flags);
	of_union(obj->flags, k->flags);
	of_copy(obj->carried_flags, k->carried_flags);
	pf_copy(obj->pflags, k->pflags);

	/* Assign modifiers */
	for (i = 0; i < OBJ_MOD_MAX; i++)
		obj->modifiers[i] = randcalc(k->modifiers[i], lev, rand_aspect);

	/* Assign charges (wands/devices only) */
	if (tval_can_have_charges(obj))
		obj->pval = randcalc(k->charge, lev, rand_aspect);

	/* Assign pval for food, batteries, pills, printers and launchers */
	if (tval_is_edible(obj) || tval_is_pill(obj) || tval_is_fuel(obj) ||
		tval_is_launcher(obj) || tval_is_printer(obj))
			obj->pval = randcalc(k->pval, lev, rand_aspect);

	/* Default fuel */
	if (tval_is_light(obj))
		obj->timeout = randcalc(k->pval, lev, rand_aspect);

	/* Default magic */
	obj->to_h = randcalc(k->to_h, lev, rand_aspect);
	obj->to_d = randcalc(k->to_d, lev, rand_aspect);
	obj->to_a = randcalc(k->to_a, lev, rand_aspect);

	/* Default slays, brands and faults */
	copy_slays(&obj->slays, k->slays);
	copy_brands(&obj->brands, k->brands);
	copy_faults(obj, k->faults);

	/* Default resists */
	for (i = 0; i < ELEM_MAX; i++) {
		obj->el_info[i].res_level = k->el_info[i].res_level;
		obj->el_info[i].flags = k->el_info[i].flags;
		obj->el_info[i].flags |= k->base->el_info[i].flags;
	}

	/* Apply random powers */
	apply_random_powers(obj, k->kind_flags);
}

/**
 * Attempt to apply faults to an object, with a corresponding increase in
 * generation level of the object
 */
static int apply_fault(struct object *obj, int lev)
{
	int pick, max_faults = randint1(4);
	int power = randint1(9) + 10 * m_bonus(9, lev);
	int new_lev = lev;

	while (max_faults--) {
		/* Try to break it */
		int tries = 3;
		while (tries--) {
			pick = randint1(z_info->fault_max - 1);
			if (faults[pick].poss[obj->tval]) {
				if (append_object_fault(obj, pick, power)) {
					new_lev += randint1(1 + power / 10);
				}
				break;
			}
		}
	}

	return new_lev;
}

/**
 * Applying magic to an object, which includes creating ego-items, and applying
 * random bonuses,
 *
 * The `good` argument forces the item to be at least `good`, and the `great`
 * argument does likewise.  Setting `allow_artifacts` to true allows artifacts
 * to be created here.
 *
 * If `good` or `great` are not set, then the `lev` argument controls the
 * quality of item.
 *
 * Returns 0 if a normal object, 1 if a good object, 2 if an ego item, 3 if an
 * artifact.
 */

int apply_magic(struct object *obj, int lev, bool allow_artifacts, bool good,
				bool great, bool extra_roll)
{
	s16b power = 0;

	/* It's "good" */
	if (good) {
		power = 1;

		/* It's "great" */
		if (great)
			power = 2;
	}

	/* Give it a chance to be faulty */
	if (one_in_(20) && tval_is_wearable(obj)) {
		lev = apply_fault(obj, lev);
	}

	/* Apply magic */
	if (tval_is_weapon(obj)) {
		apply_magic_weapon(obj, lev, power);
	} else if (tval_is_armor(obj)) {
		apply_magic_armour(obj, lev, power);
	} else if (tval_is_chest(obj)) {
		/* Get a random, level-dependent set of chest traps */
		obj->pval = pick_chest_traps(obj);
	}

	/* Apply minima from ego items if necessary */
	ego_apply_minima(obj);

	return power;
}


/*** Generate a random object ***/

/**
 * Hack -- determine if a template is "good".
 *
 * Note that this test only applies to the object *kind*, so it is
 * possible to choose a kind which is "good", and then later cause
 * the actual object to be faulty.  We do explicitly forbid objects
 * which are known to be boring or which start out somewhat damaged.
 */
static bool kind_is_good(const struct object_kind *kind, int level)
{
	/* Some item types are (almost) always good */

	/* Anything with a high base cost */
	if (kind->cost > 150 + (level*200))
		return true;

	/* Anything with the GOOD flag */
	if (kf_has(kind->kind_flags, KF_GOOD))
		return true;

	/* Armor -- Good unless damaged */
	if (kind_tval_is_armor(kind)) {
		if (randcalc(kind->to_a, 0, MINIMISE) < 0) return (false);
		return true;
	}

	/* Weapons, including melee, guns and ammo -- Good unless damaged
	 * Having a to hit penalty doesn't mean that is damaged, though.
	 * So assume that anything that is supposed to have a penalty and still
	 * be good has already exited by now.
	 */
	if (kind_tval_is_weapon(kind)) {
		if (randcalc(kind->to_h, 0, MINIMISE) < 0) return (false);
		if (randcalc(kind->to_d, 0, MINIMISE) < 0) return (false);
		return true;
	}

	/* Assume not good */
	return (false);
}


/**
 * Choose an object kind of a given tval given a dungeon level.
 */
static struct object_kind *get_obj_num_by_kind(int level, bool good, int tval)
{
	const double *objects;
	u32b total = 0;
	u32b value;
	int item;

	assert(level >= 0 && level <= z_info->max_obj_depth);
	assert(tval >= 0 && tval < TV_MAX);
	if (good) {
		objects = obj_alloc_great + level * (z_info->k_max + 1);
		total = obj_total_tval_great[level * TV_MAX + tval];
	}
	if (!total) {
		objects = obj_alloc + level * (z_info->k_max + 1);
		total = obj_total_tval[level * TV_MAX + tval];
	}

	/* No appropriate items of that tval */
	if (!total) return NULL;

	/* Pick an object */
	value = Rand_double(total);

	/*
	 * Find it.  Having a loop to calculate the cumulative probability
	 * here with only the tval and applying a binary search was slower
	 * for a test of getting a TV_SWORD from 4.2's available objects.
	 * So continue to use the O(N) search.
	 */
	for (item = 0; item < z_info->k_max; item++) {
		if (objkind_byid(item)->tval == tval) {
			double prob = objects[item + 1] - objects[item];

			if (value < prob) break;
			value -= prob;
		}
	}

	/* Return the item index */
	return objkind_byid(item);
}

/**
 * Choose an object kind given a dungeon level to choose it for.
 * If tval = 0, we can choose an object of any type.
 * Otherwise we can only choose one of the given tval.
 */
struct object_kind *get_obj_num(int level, bool good, int tval)
{
	const double *objects;
	double value;
	int item;

	/* Occasional level boost */
	if ((level > 0) && one_in_(z_info->great_obj))
		/* What a bizarre calculation */
		level = 1 + (level * z_info->max_obj_depth / randint1(z_info->max_obj_depth));

	/* Paranoia */
	level = MIN(level, z_info->max_obj_depth);
	level = MAX(level, 0);

	if (tval)
		return get_obj_num_by_kind(level, good, tval);

	objects = (good ? obj_alloc_great : obj_alloc) + (level * (z_info->k_max + 1));

	if ((!objects[z_info->k_max]) && good)
		objects = obj_alloc + (level * (z_info->k_max + 1));

	/* Pick an object. */
	if (!objects[z_info->k_max]) {
		return NULL;
	}

	value = Rand_double(objects[z_info->k_max]);

	/* Find it with a binary search. */
	item = binary_search_probtable(objects, z_info->k_max + 1, value);

	/* Return the item index */
	return objkind_byid(item);
}

static double artifact_prob(double depth)
{
	/* The following weird math is a combination of two ease curves (S shape, 3x^2-2x^3).
	 * 'Full' gives the overall shape.
	 * 'Mid' gives some boost at midlevels centered at 'midpoint'.
	 *  Finally there is a small constant addition to make things more generous at low levels.
	 */
	double x = MIN(depth, 100) * 0.01;
	double full = ((x*x)*3)-((x*x*x)*2);
	double midpoint = 0.1;
	double midlin = (x < midpoint) ? (x / midpoint) : (1.0 - x) / (1.0 - midpoint);
	double mid = ((midlin*midlin)*3)-((midlin*midlin*midlin)*2);
	double chance = (full + (mid * 0.5)) / 140;
	chance += 0.0002;
	if (player_has(player, PF_GREEDY))
		chance *= 1.5;
	return chance;
}

/* Chance that an object is a multiple ego */
static double multiego_prob(double depth, bool good, bool great)
{
	if (player_has(player, PF_GREEDY))
		depth += 10.0;
	if (great)
		/* 1% at level 0, 40% at level 30, 20% at level 95+ */
		return (depth <= 30) ? (0.01 + ((MIN(depth, 30) / 30.0) * 4.0 * 0.09)) : ((((95 - MIN(depth, 95)) / 65.0) * 0.2) + 0.2);
	else if (good)
		return 0.001 + ((MIN(depth, 45) / 45.0) * 0.099);	/* 0.1% at level 0, 10% at level 45+ */
	else
		return ((MIN(depth, 65) / 65.0) * 0.06);			/* 0 at level 0, 6% at level 65+ */
}

static double ego_prob(double depth, bool good, bool great)
{
	/* Debug: print the generation probability table */
	static bool first = false;
	if (first) {
		first = false;
		for(int i=1;i<=100;i++) {
			double d = ego_prob(i, false, false);
			double e = multiego_prob(i, false, false);
			double good_d = ego_prob(i, true, false);
			double good_e = multiego_prob(i, true, false);
			double great_d = ego_prob(i, true, true);
			double great_e = multiego_prob(i, true, true);
			double f = artifact_prob(i);
			fprintf(stderr,"l%d, NORMAL(ego %lf multi %lf art %lf), GOOD(ego %lf multi %lf), GREAT(ego %lf multi %lf)\n", i, d, e, f, good_d, good_e, great_d, great_e);
		}
	}

	if (player_has(player, PF_GREEDY))
		depth += 10.0;
	/* Chance of being `good` and `great` */
	/* This has changed over the years:
	 * 3.0.0:   good = MIN(75, lev + 10);      great = MIN(20, lev / 2);
	 * 3.3.0:   good = (lev + 2) * 3;          great = MIN(lev / 4 + lev, 50);
	 * 3.4.0:   good = (2 * lev) + 5
	 * 3.4 was in between 3.0 and 3.3, 3.5 attempts to keep the same
	 * area under the curve as 3.4, but make the generation chances
	 * flatter.  This depresses good items overall since more items
	 * are created deeper.
	 * This change is meant to go in conjunction with the changes
	 * to ego item allocation levels. (-fizzix)
	 *
	 * 4.2.x:   great = 30, great = 10% at level 0 .. 30% at level 66+;
	 *
	 * (MS) New ego generation needs lower chances, as every ego asked for
	 * is obtained. This also means that great = 1.0 would disqualify
	 * any item that can't be made into an ego item - including !oExp
	 * and similar valuables.
	 *
	 * To mimic the original odds would need to reduce to 0.274 at level 1,
	 * 0.280 at level 33, 0.342 at level 70, 0.377 at level 98. But this
	 * is only a rough guideline (it will change with the ego and item
	 * lists, and is based on egos-per-depth generated, not actual levels.
	 * The original is also probably too ego heavy later on.)
	 *
	 * Using 'diving' instead:
	 * Level	Original	New 1/.3/.1	Ratio	.8/.125/.025	.8/.15/.0325
	 * 5		0.765		2.165		2.83		0.568		0.765
	 * 10		1.202		3.117		2.593		0.786		1.074
	 * 20		2.230		5.399		2.421		1.712		2.087
	 * 40		5.254		13.584		2.459		4.190		5.474
	 * 70		16.56		33.25		2.007		8.752		10.560
	 * 95		36.70		65.86		1.794		15.076		18.997
	 * 
	 * What proportion of 'great' items should be 'ego' should probably depend
	 * on what the character is likely to want at that point.
	 * 	- at level 1, any ego item is a big win
	 *  - at level 15, you might have something better already, but chances are still good.
	 *  - at level 30, you have a few artifacts and the rest mostly egos. And you want stat gain pills.
	 *  - at level 50, you have mostly artifacts. There are only a few ego items which would be an improvement. But you want gain/heal pills.
	 *  - at level 90, you have probably all good artifacts. Almost all ego items are chaff. But you want endgame consumables.
	 * 
	 * The same may apply to some extent for good and normal treasures - but this is going against wanting less egos early.
	 * (The majority of early egos are from the floor. The majority of late ones are from monsters.)
	 * 
	 * The following chances are a close match to original results based on diving sims below level 40.
	 * They then reduce, until being only about half as generous below level 95.
	 * Both these are (probably) Good Things.
	 */
	if (great)
		return ((1.0 - ((MIN(depth, 95.0)) / 95.0)) * 0.6) + 0.2; /* 80% at level 0, 20% at level 95+ */
	else if (good)
		return 0.15;										/* 15% */
	else
		return 0.0325 + ((MIN(depth, 40) / 40.0) * 0.0675);	/* 3.25% at level 0, 10% at level 40+ */
}

/* Anonymous Gregorian Algorithm (Nature, 1876).
 * This calculates the date of Easter Sunday.
 * The function treats the whole long weekend as Easter, so -2 to +1 day.
 **/
static bool its_easter(void)
{
	struct tm *t;
	time_t now;
	time(&now);
	t = gmtime(&now);

	int y = t->tm_year + 1900;
	int a = y % 19;
	int b = y / 100;
	int c = y % 100;
	int d = b / 4;
	int e = b % 4;
	int f = (b + 8) / 25;
	int g = (b - f + 1) / 3;
	int h = ((19 * a) + b - d - g + 15) % 30;
	int i = c / 4;
	int k = c % 4;
	int l = (32 + (2 * e) + (2 * i) - h - k) % 7;
	int m = (a + (11 * h) + (22 * l)) / 451;
	int month = (h + l - (7 * m) + 114) / 31;
	int day = ((h + l - (7 * m) + 114) % 31) + 1;

	/* Date of Easter Sunday as an offset from March 1 */
	int easter = day;
	if (month == 4)
		easter += 31;

	/* Current date as an offset from March 1 */
	int today = t->tm_mday;
	if (t->tm_mon == 3)
		today += 31;
	else if (t->tm_mon != 2)
		return false;

	int offset = today - easter;

	return ((offset <= 1) && (offset >= -2));
}

/* Called for non-artifact items with the SPECIAL_GEN kind flag set.
 * These can't always be generated, with the conditions varying per object.
 * Returns true if it is OK to generate the item.
 */
bool special_item_can_gen(struct object_kind *kind)
{
	/* Seasonal silliness */
	if (strstr(kind->name, "chocolate egg")) {
		return its_easter();
	}

	/* Default to OK */
	return true;
}

/* Look up a multiego combination in the table, with given maximum value */
static struct multiego_entry *multiego_find(struct multiego_entry *table, double total)
{
	int i;
	do {
		double random = Rand_double(total);
		i = 0;
		do {
			if (table[i].prob >= random) {
				break;
			}
		} while (table[++i].prob != 0.0);
	} while (table[i].prob == 0.0);
	return table+i;
}

/* Returns true if the multiego combination is allowed to be generated.
 * Forbidden combinations may be because they duplicate another ego.
 * Define only as ego_item.txt lines?
 * as forbid:all or forbid:<name>|<name> - and this persists to all following names?
 * 	would then needforbid:none 
 */
bool multiego_allow(u16b *ego)
{
	for(int i=0;i<MAX_EGOS;i++) {
		for(int j=0;j<MAX_EGOS;j++) {
			if (i != j) {
				if (multiego_forbid[ego[i] + (z_info->e_max * ego[j])]) {
					return false;
				}
			}
		}
	}
	return true;
}

/* Build a table of multiego combinations, given a level and tval.
 **/
static struct multiego_entry *multiego_table(int genlevel, int tval, double *ptotal)
{
	double total = 0.0;
	bool match[z_info->e_max][z_info->k_max];
	int entries = 0;

	/* Build a 'ego can use kind' table */
	memset(match, 0, sizeof(match));
	for(int i=0;i<z_info->e_max;i++) {
		struct poss_item *item = e_info[i].poss_items;
		while (item) {
			if ((!tval) || (tval == k_info[item->kidx].tval)) {
				match[i][item->kidx] = true;
			}
			item = item->next;
		}
	}

	/* Count size */
	for(int i=0;i<z_info->e_max;i++) {
		for(int j=i+1;j<z_info->e_max;j++) {
			struct poss_item *item = e_info[j].poss_items;
			while (item) {
				if ((!tval) || (tval == k_info[item->kidx].tval)) {
					if (match[i][item->kidx]) {
						entries++;
						break;
					}
				}
				item = item->next;
			}
		}
	}

	/* Allocate a probability table (+1 for the terminator) */
	struct multiego_entry *table = mem_zalloc(sizeof(struct multiego_entry) * (entries + 1));
	struct multiego_entry *entry = table;

	/* Fill the table */
	double lev = MIN(127, genlevel);
	for(int i=0;i<z_info->e_max;i++) {
		for(int j=i+1;j<z_info->e_max;j++) {
			struct poss_item *item = e_info[j].poss_items;
			while (item) {
				if (match[i][item->kidx]) {
					if ((!tval) || (tval == k_info[item->kidx].tval)) {
						/* Find the combined level */
						int level = e_info[i].alloc_min + e_info[j].alloc_min;
						double meprob = e_info[i].alloc_prob * e_info[j].alloc_prob;
						double min = (level + 5.0) * 1.2;
						if (min < 10.0)
							min = 10.0;
						double max = e_info[i].alloc_max + e_info[j].alloc_max;
						if (max > 127.0)
							max = 127.0;
						if (max < (min + 10.0))
							max = (min + 10.0);
						double scale = 0.0;

						if (lev > max) {
							;
						} else if (lev >= min) {
							scale = 1.0 - ((lev - min) / (1 + max - min));
						} else {
							scale = 1.0 / (1.0 + (min - lev));
						}

						meprob *= scale;
						if (meprob > 0.0) {
							entry->ego[0] = i;
							entry->ego[1] = j;
							if (multiego_allow(entry->ego)) {
								total += meprob;
								entry->prob = total;
								entry++;
							}
						}
						break;
					}
				}
				item = item->next;
			}
		}
	}

	*ptotal = total;
	return table;
}


/**
 * Attempt to make an object
 *
 * \param c is the current dungeon level.
 * \param lev is the creation level of the object (not necessarily == depth).
 * \param good is whether the object is to be good
 * \param great is whether the object is to be great
 * \param extra_roll is whether we get an extra roll in apply_magic()
 * \param value is the value to be returned to the calling function
 * \param tval is the desired tval, or 0 if we allow any tval
 * \param name is the desired object's name, or NULL if we allow any item (of that tval)
 *
 * \return a pointer to the newly allocated object, or NULL on failure.
 */
struct object *make_object_named(struct chunk *c, int lev, bool good, bool great,
						   bool extra_roll, s32b *value, int tval, const char *name)
{
	int base, tries = 3;
	struct object_kind *kind = NULL;
	struct object *new_obj = NULL;
	bool makeego = false;
	bool multiego = false;
	double chance = 0.0;
	double random = Rand_double(1.0);
	double me_total = 0.0;
	struct multiego_entry *me_table = NULL;
	struct multiego_entry *me_entry = NULL;

	/* Try to make an artifact.
	 * There are no artifacts in the town, more are generated at depth and
	 * the chances go up for good, great or extra.
	 **/
	if ((!name) && (player->depth)) {
		double depth = lev;
		if (extra_roll)
			depth = (depth * 1.15) + 20;
		else if (great)
			depth = (depth * 1.1) + 15;
		else if (good)
			depth = (depth * 1.05) + 5;
		chance = artifact_prob(depth);

		if (random < chance) {
			/* This will always create an artifact if there are any artifacts
			 * left to create - or if tval is specified, any artifacts left of
			 * that tval.
			 **/
			new_obj = make_artifact(lev, tval);
			if (!new_obj) {
				good = true;
				great = true;
			} else {
				kind = new_obj->kind;
			}
		}
	}

	/* If an artifact hasn't been generated and a named item wasn't
	 * specified, try to create an ego item
	 **/
	if ((!name) && (!new_obj)) {
		double multiegochance = multiego_prob(lev, good, great);
		random -= chance;

		if (random < multiegochance) {
			/* Make a multiple ego item */
			multiego = true;
			makeego = true;
		} else {

			double egochance = ego_prob(lev, good, great);
			random -= multiegochance;
			if (random < egochance) {
				/* Make an ego item */
				makeego = true;
			}
		}
	}

	if (makeego || multiego) {
		/* Occasionally boost the generation level of an ego item */
		if (lev > 0 && one_in_(z_info->great_ego)) {
			lev = 1 + (lev * z_info->max_depth / randint1(z_info->max_depth));

			/* Ensure valid allocation level */
			if (lev >= z_info->max_depth)
				lev = z_info->max_depth - 1;
		}
	}

	if (!new_obj) {
		struct ego_item *ego = NULL;
		int selected = -1;

		/* Base level for the object */
		base = (good ? (lev + 10) : lev);

		if (name) {
			/* Use the given name, and either the given tval or try all */
			int sval = -1;

			if (tval) {
				sval = lookup_sval(tval, name);
			} else {
				for(tval=0; tval<TV_MAX; tval++) {
					sval = lookup_sval(tval, name);
					if (sval >= 0)
						break;
				}
			}
			if (sval >= 0) {
				kind = lookup_kind(tval, sval);
			}
		} else {
			/* Generate an ego and kind.
			 * If multiple egos are wanted, do so repeatedly until one is found.
			 * There is a limit of 20 retries though, mainly because some tvals
			 * may not have any acceptable combinations.
			 */
			if (multiego) {
				/* Get the multiego selection table */
				me_table = multiego_table(lev, tval, &me_total);

				/* Are there any possible combinations of egos available at this level/tval? */
				if (me_total > 0.0) {
					/* Select randomly from it */
					me_entry = multiego_find(me_table, me_total);

					/* Select a kind for that combination */
					kind = select_multiego_kind(me_entry, lev, tval);
				} else {
					/* Nothing, so revert to single ego */
					multiego = false;
				}
			}

			if (!multiego) {
				/* Not a multiego, may be a single ego */
				if (makeego) {
					/* Select an ego item. This might fail */
					ego = find_ego_item(lev, tval);
				}
				if (ego) {
					/* Choose from the ego's allowed kinds */
					kind = select_ego_kind(ego, lev, tval);
				} else {
					/* Try to choose an object kind */
					tries = 3;
					while (tries--) {
						kind = get_obj_num(base, good || great, tval);
						if (kind) break;
					}
				}
			}
		}
		if (!kind) {
			mem_free(me_table);
			return NULL;
		}

		/* Discard special cases */
		if (((kf_has(kind->kind_flags, KF_SPECIAL_GEN)) && (!special_item_can_gen(kind))) ||
			(kf_has(kind->kind_flags, KF_QUEST_ART))) {
			mem_free(me_table);
			return NULL;
		}

		/* Make the object, prep it and apply magic */
		new_obj = object_new();
		object_prep(new_obj, kind, lev, RANDOMISE);

		/* Actually apply the ego template to the item */
		assert(!new_obj->ego[0]);
		if (multiego) {
			for(int i=0;i<MAX_EGOS;i++) {
				new_obj->ego[i] = e_info + me_entry->ego[i];
			}
		} else {
			new_obj->ego[0] = ego;
		}
		if (ego && multiego && (selected >= 0)) {
			assert(new_obj->ego[0]);
			assert(!new_obj->ego[1]);
			new_obj->ego[1] = &e_info[selected];
		}
		ego_apply_magic(new_obj, lev);
		apply_magic(new_obj, lev, true, good, great, extra_roll);

		/* Generate multiple items */
		if (!new_obj->artifact && kind->gen_mult_prob >= randint1(100))
			new_obj->number = randcalc(kind->stack_size, lev, RANDOMISE);
	}

	if (new_obj->number > new_obj->kind->base->max_stack)
		new_obj->number = new_obj->kind->base->max_stack;

	/* Get the value */
	if (value)
		*value = object_value_real(new_obj, new_obj->number);

	/* Boost of 20% per level OOD for non-faulty objects */
	if ((!new_obj->faults) && (kind->alloc_min > c->depth)) {
		if (value) *value += (kind->alloc_min - c->depth) * (*value / 5);
	}

	assert((new_obj) && (new_obj->kind));
	mem_free(me_table);
	return new_obj;
}


/**
 * Attempt to make an object
 *
 * \param c is the current dungeon level.
 * \param lev is the creation level of the object (not necessarily == depth).
 * \param good is whether the object is to be good
 * \param great is whether the object is to be great
 * \param extra_roll is whether we get an extra roll in apply_magic()
 * \param value is the value to be returned to the calling function
 * \param tval is the desired tval, or 0 if we allow any tval
 *
 * \return a pointer to the newly allocated object, or NULL on failure.
 */
struct object *make_object(struct chunk *c, int lev, bool good, bool great,
						   bool extra_roll, s32b *value, int tval)
{
	return make_object_named(c, lev, good, great, extra_roll, value, tval, NULL);
}

/**
 * Scatter some objects near the player
 */
void do_acquirement(struct loc grid, int level, int num, bool good, bool great)
{
	struct object *nice_obj;

	/* Acquirement */
	while (num) {
		/* Make a good (or great) object (if possible) */
		nice_obj = make_object(cave, level, good, great, true, NULL, 0);
		if (!nice_obj) continue;

		num--;
		nice_obj->origin = ORIGIN_ACQUIRE;
		nice_obj->origin_depth = player->depth;

		/* Drop the object */
		drop_near(cave, &nice_obj, 0, grid, true, false);
	}
}

/**
 * Scatter some objects near the player
 */
void acquirement(struct loc grid, int level, int num, bool great)
{
	do_acquirement(grid, level, num, true, great);
}


/*** Make a gold item ***/

/**
 * Get a money kind by name, or level-appropriate 
 */
struct object_kind *money_kind(const char *name, int value)
{
	int rank = num_money_types;
	int max_gold_drop = (3 + z_info->max_depth + ((z_info->max_depth * z_info->max_depth) / 25)) * 10;

	/* Check for specified treasure variety */
	if (name) {
		for (rank = 0; rank < num_money_types; rank++)
			if (streq(name, money_type[rank].name))
				break;
	}

	/* Pick a treasure variety scaled by level */
	if (rank == num_money_types) {
		double lv = log(value);
		double lm = log(max_gold_drop);
		double maxrrank = ((lv / lm) * (num_money_types + 8.0)) - 8.0;
		int maxrank = maxrrank;

		/* Do not create illegal treasure types */
		if (maxrank >= num_money_types) maxrank = num_money_types - 1;
		if (maxrank < 0) maxrank = 0;

		int minrank = maxrank - (maxrank / 4);

		rank = minrank + randint0(1 + maxrank - minrank);

		while ((randint0(rank+4) <= 2) && (rank < num_money_types - 1))
			rank++;
	}
	return lookup_kind(TV_GOLD, money_type[rank].type);
}

/**
 * Make a money object
 *
 * \param lev the dungeon level
 * \param coin_type the name of the type of money object to make
 * \return a pointer to the newly minted cash (cannot fail)
 */
struct object *make_gold(int lev, const char *coin_type)
{
	struct object *new_gold = mem_zalloc(sizeof(*new_gold));
	int value;

	/* Repeat until a value below z_info->cash_max is found (as the roll is open ended, and while pvals are 32 bit now
	 * we still don't want any instant billionaires)
	 **/
	do {
		int avg = 3 + lev + ((lev * lev) / 25);
		if (player_has(player, PF_GREEDY))
			avg *= 2;
		int spread = avg;
		do {
			value = rand_spread(avg, spread);
		} while (value <= 0);

		/* Increase the range to infinite.
		 * Don't do this in the town.
		 * Be more generous if no-selling
		 **/
		int exploder = 25000 / (lev + 3);

		if (OPT(player, birth_no_selling))
			exploder = (5000 / (lev + 7))+150;
		
		if (exploder < 20)
			exploder = 20;

		if (player->depth) {
			while ((randint0(exploder) < 100) && value <= (INT_MAX / 10))
				value *= 10;
		}
	} while (value >= z_info->cash_max);
	/* Prepare a gold object */
	object_prep(new_gold, money_kind(coin_type, value), lev, RANDOMISE);

	new_gold->pval = value;

	return new_gold;
}

struct init_module obj_make_module = {
	.name = "object/obj-make",
	.init = init_obj_make,
	.cleanup = cleanup_obj_make
};
