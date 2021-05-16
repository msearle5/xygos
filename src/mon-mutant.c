/**
 * \file mon-mutant.c
 * \brief Monster mutations
 *
 * Monster mutations - adding, removing etc.
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
 */

#include "init.h"
#include "mon-init.h"
#include "mon-mutant.h"
#include "mon-spell.h"
#include "mon-util.h"
#include "monster.h"

/** Mutated races.
 * These are created as needed.
 */

static struct monster_race ***mutant_races;

/** Allocate (but do not fill out) a new mutant race combination,
 * or return a pointer to the old one if it already exists
 **/
static struct monster_race *get_mutant_race(struct monster_race *base, struct monster_mutation *mutation)
{
	if (!mutant_races)
		mutant_races = mem_zalloc(sizeof(struct monster_race **) * z_info->r_max);

	int b_idx = base->ridx;
	int m_idx = mutation->race.ridx;

	if (!mutant_races[b_idx])
		mutant_races[b_idx] = mem_zalloc(sizeof(struct monster_race *) * z_info->mm_max);

	if (!mutant_races[b_idx][m_idx])
		mutant_races[b_idx][m_idx] = mem_zalloc(sizeof(struct monster_race));

	return mutant_races[b_idx][m_idx];
}

/** Combine a base race and mutation. The combination is stored in the mutant_races table,
 * and returned.
 */
static struct monster_race *add_mutation_to_race(struct monster_race *base, struct monster_mutation *mut)
{
	struct monster_race *mr = get_mutant_race(base, mut);
	struct monster_race *mutation = &(mut->race);

	/* No name, so it's empty. Combine the base and mutation. */
	if (!mr->name) {
		char buf[1024];

		/* Start from the base */
		memcpy(mr, base, sizeof(*base));

		/* Combined name */
		strnfmt(buf, sizeof(buf), "%s %s", mutation->name, base->name);
		mr->name = string_make(buf);

		/* Add: speed, AC, depth, light */
		mr->ac += mutation->ac;
		mr->speed += mutation->speed;
		mr->level += mutation->level;
		mr->light += mutation->light;

		/* Scaled multipliers: HP, exp */
		mr->avg_hp = (mr->avg_hp * mutation->avg_hp) / 100;
		mr->mexp = (mr->mexp * mutation->mexp) / 100;

		/* OR: flags, spellflags, death spellflags */
		rf_union(mr->flags, mutation->flags);
		rsf_union(mr->spell_flags, mutation->spell_flags);
		rsf_union(mr->death_spell_flags, mutation->death_spell_flags);

		/* Set mutation flag */
		if (rf_has(mr->flags, RF_NONLIVING))
			rf_on(mr->flags, RF_MODIFIED);
		else
			rf_on(mr->flags, RF_MUTANT);
	}
	return mr;
}

/** Return true if a monster race is a mutated combination */
static bool race_is_mutated(struct monster_race *race)
{
	if (rf_has(race->flags, RF_MUTANT))
		return true;
	if (rf_has(race->flags, RF_MODIFIED))
		return true;
	return false;
}

/**
 * Return true if the given mutation is an acceptable mutation for this base race.
 * If this is occurring as the monster is being created, birth = true.
 */
static bool acceptable_mutation(struct monster_race *base, struct monster_mutation *mm, bool birth, int level)
{
	struct monster_race *mutation = &mm->race;

	/* Is it out of level? */
	if (base->level + mutation->level > level)
		return false;

	/* Is it not applicable to this race? */
	if ((base->level < mm->min_level) || (base->level > mm->max_level))
		return false;

	/* Is it a unique or special */
	if (rf_has(base->flags, RF_UNIQUE))
		return false;

	if (rf_has(base->flags, RF_SPECIAL_GEN))
		return false;

	/* Is it radiation resistant? */
	if (rf_has(base->flags, RF_IM_RADIATION))
		return false;

	/* Is it already mutated? */
	if (race_is_mutated(base))
		return false;

	/* Is it non-living, and not being created? */
	if ((!birth) && (rf_has(base->flags, RF_NONLIVING)))
		return false;

	/* If there is overlap of flags, it isn't acceptable */
	if (rf_is_inter(base->flags, mutation->flags))
		return false;
	if (rsf_is_inter(base->spell_flags, mutation->spell_flags))
		return false;
	if (rsf_is_inter(base->death_spell_flags, mutation->death_spell_flags))
		return false;

	/* Avoid out of bounds */
	if ((base->level + mutation->level) < 1)
		return false;
	if (((base->avg_hp * mutation->avg_hp) / 100) < 1)
		return false;
	if (((base->mexp * mutation->mexp) / 100) < 1)
		return false;

	return true;
}

/** Find a mutation which can be applied to the base. 
 * If this is occurring as the monster is being created, birth = true.
 * If none can be applied, return NULL.
 **/
struct monster_race *select_mutation(struct monster_race *race, bool birth, int level)
{
	/* Check all combinations */
	bool ok = false;
	struct monster_race *combo = NULL;
	bool *acceptable = mem_zalloc(z_info->mm_max);
	for(int i=0; i<z_info->mm_max; i++) {
		acceptable[i] = acceptable_mutation(race, &mm_info[i], birth, level);
		if (acceptable[i])
			ok = true;
	}

	/* None are possible: exit */
	if (ok) {
		/* Select at random */
		do {
			int m = randint0(z_info->mm_max);
			if (acceptable[m]) {
				if (one_in_(mm_info[m].race.rarity)) {
					combo = add_mutation_to_race(race, &mm_info[m]);
				}
			}
		} while (!combo);
	}

	/* Clean up and return */
	mem_free(acceptable);
	return combo;
}

/** Extract a mutated combo race from a name, as used by the savefile code.
 * Allocates (if needed) and returns a monster_race *, or NULL if it is not recognised. 
 */
struct monster_race *get_mutant_race_by_name(const char *name)
{
	/* Assumes all mutations are described by one leading word. Monsters may have more. */
	char *end = strchr(name, ' ');
	if (!end)
		return NULL;

	/* Check that the rest is a known monster */
	struct monster_race *base = lookup_monster(end+1);
	if (!base)
		return NULL;

	/* Extract the mutation */
	for(int i=0; i<z_info->mm_max; i++) {
		if (!strncmp(name, mm_info[i].race.name, end - name)) {
			/* Mutation matches - don't check possibility, as we don't want to
			 * reject a savefile just because the acceptability rules have changed.
			 **/
			return add_mutation_to_race(base, mm_info+i);
		}
	}

	/* Not a mutation */
	return NULL;
}

/** Mutate a monster (or race) - replace the race with a mutated version.
 * (Polymorphed monsters can be ignored as it only affects the current, not original race.)
 * Returns the mutation used, or NULL if no mutation occurred.
 * If this is occurring as the monster is being created, birth = true.
 */
struct monster_race *mutate_monster(struct monster_race **race, bool birth, int level)
{
	struct monster_race *mut = select_mutation(*race, birth, level);
	if (!mut)
		return NULL;
	*race = mut;
	return mut;
}
