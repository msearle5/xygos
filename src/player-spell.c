/**
 * \file player-spell.c
 * \brief Spell and prayer casting/praying
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
#include "cave.h"
#include "cmd-core.h"
#include "effects.h"
#include "game-world.h"
#include "init.h"
#include "monster.h"
#include "obj-gear.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-ability.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "target.h"

/**
 * Stat Table (Casting Stat) -- Minimum failure rate (percentage)
 */
byte adj_mag_fail[STAT_RANGE];

/**
 * Stat Table (Casting Stat) -- failure rate adjustment
 */
s16b adj_mag_stat[STAT_RANGE];

/**
 * Initialise player spells
 */
void player_spells_init(struct player *p)
{
	int i, num_spells = total_spells;

	/* None */
	if (!num_spells) return;

	/* Allocate */
	p->spell_flags = mem_zalloc(num_spells * sizeof(byte));
	p->spell_order = mem_zalloc(num_spells * sizeof(byte));

	/* None of the spells have been learned yet */
	for (i = 0; i < num_spells; i++)
		p->spell_order[i] = 99;
}

/**
 * Free player spells
 */
void player_spells_free(struct player *p)
{
	mem_free(p->spell_flags);
	mem_free(p->spell_order);
}

/**
 * Mark all player spells as unfolded (visible)
 */
void spell_unfold_all(struct player *p)
{
	for (int i = 0; i < total_spells; i++)
		p->spell_flags[i] &= ~PY_SPELL_FOLDED;
}

/**
 * Mark all player spells in a book as folded (invisible)
 */
void spell_fold_book(struct player *p, int book)
{
	for (int i = 0; i < total_spells; i++) {
		const struct class_spell *s = spell_by_index(p, i);
		if (s && s->bidx == book) {
			p->spell_flags[i] |= PY_SPELL_FOLDED;
		}
	}
}

/**
 * Count all player spells in a book which aren't folded (or otherwise invisible)
 */
int spell_count_visible(const struct player *p)
{
	int n_spells = 0;

	/* Count the spells */
	combine_books(p, &n_spells, NULL, NULL, NULL, NULL, NULL);

	return n_spells;
}

/**
 * Collect spells from a book into the spells[] array (if spells is non-null).
 * If spells is null, it just counts how many are needed.
 */
struct class_spell *combine_book(const struct player *p, const struct class_book *src, int *count, int *spells, int *maxidx, struct class_spell **spellps, int *books, struct class_book ***book)
{
	struct class_spell *first = NULL;
	bool bookused = false;
	int index = *count;
	for (int i = 0; i < src->num_spells; i++) {
		/* Ignore folded spells */
		if (p->spell_flags[src->spells[i].sidx] & PY_SPELL_FOLDED) {
			/* If at least one folded spell is available, add this book */
			if ((book) && (books) && (!bookused)) {
				(*books)++;
				*book = mem_realloc(*book, sizeof(struct class_book *) * (*books));
				(*book)[(*books)-1] = (struct class_book *)src;
				bookused = true;
			}

			continue;
		}
		/* Ignore out of level spells.
		 * For a book with the same name as your class, this is your level in that class.
		 * Otherwise it is your character level.
		 **/
		struct player_class *sc = get_class_partial_name(src->name);
		int level = p->lev;
		if (sc)
			level = levels_in_class(sc->cidx);
		if (src->spells[i].slevel > level) {
			continue;
		}

		/* Ignore duplicate spells */
		bool ignore = false;
		if ((spellps) && (spells)) {
			for(int j=0; j < index; j++) {
				if (streq(src->spells[i].name, spellps[spells[j]]->name)) {
					ignore = true;
					break;
				}
			}
		}
		if (ignore) {
			continue;
		}

		/* Spell is OK to add */
		if (spells) {
			/* Second pass, fill in the spells array */
			spells[index] = src->spells[i].sidx;
		}
		if (spellps) {
			spellps[src->spells[i].sidx] = &src->spells[i];
		}
		if (!first) {
			first = &src->spells[i];
		}
		if (maxidx)
			if ((src->spells[i].sidx + 1) > *maxidx)
				*maxidx = src->spells[i].sidx + 1;
		index++;
	}
	*count = index;

	return first;
}

/**
 * Collect spells from all books of a class-magic into the spells[] array.
 */
struct class_spell *combine_class_books(const struct player *p, const struct class_magic *cmagic, int *count, int *spells, int *maxidx, struct class_spell **spellps, int *books, struct class_book ***book)
{
	struct class_spell *first = NULL;
	for(int i=0;i<cmagic->num_books;i++) {
		struct class_spell *sbook = combine_book(p, &cmagic->books[i], count, spells, maxidx, spellps, books, book);
		if (!first)
			first = sbook;
	}
	return first;
}

/**
 * Collect spells from all books into the spells[] array.
 * This looks at all classes, race, shapechange, abilities, and equipment.
 * It currently assumes that shapechange prevents everything not derived from the change.
 * Returns the first spell (whether or not a spell-pointer was passed in).
 */
struct class_spell *combine_books(const struct player *p, int *count, int *spells, int *maxidx, struct class_spell **spellps, int *books, struct class_book ***book)
{
	struct class_spell *first = NULL;
	struct class_spell *sbook;

	if (player_is_shapechanged(player)) {
		sbook =  combine_class_books(p, &p->shape->magic, count, spells, maxidx, spellps, books, book);
	} else {
		for (struct player_class *c = classes; c; c = c->next) {
			int levels = levels_in_class(c->cidx);
			if (levels) {
				sbook =  combine_class_books(p, &c->magic, count, spells, maxidx, spellps, books, book);
				if (!first)
					first = sbook;
			}
		}
		sbook =  combine_class_books(p, &p->race->magic, count, spells, maxidx, spellps, books, book);
		if (!first)
			first = sbook;
		sbook =  combine_class_books(p, &p->extension->magic, count, spells, maxidx, spellps, books, book);
		if (!first)
			first = sbook;
		sbook =  combine_class_books(p, &p->personality->magic, count, spells, maxidx, spellps, books, book);
		if (!first)
			first = sbook;
		if (p->split_p) {
			sbook =  combine_class_books(p, &personalities->magic, count, spells, maxidx, spellps, books, book);
			if (!first)
				first = sbook;
		}

		for(int i=0;i<PF_MAX;i++) {
			if (ability[i] && player_has(player, i)) {
				/* If this ability has the flying flag, the first book is for use when
				 * not flying, the second for use when flying and any further books
				 * can be used at any time.
				 */
				if (ability[i]->flags & AF_FLYING) {
					struct class_magic *cmagic = &ability[i]->magic;
					if ((cmagic->num_books > 0) && (p->flying == false)) {
						sbook =  combine_book(p, &cmagic->books[0], count, spells, maxidx, spellps, books, book);
						if (!first)
							first = sbook;
					}
					if ((cmagic->num_books > 1) && (p->flying == true)) {
						sbook =  combine_book(p, &cmagic->books[1], count, spells, maxidx, spellps, books, book);
						if (!first)
							first = sbook;
					}
					for(int i=2;i<cmagic->num_books;i++) {
						sbook =  combine_book(p, &cmagic->books[i], count, spells, maxidx, spellps, books, book);
						if (!first)
							first = sbook;
					}
				} else {
					sbook =  combine_class_books(p, &ability[i]->magic, count, spells, maxidx, spellps, books, book);
					if (!first)
						first = sbook;
				}
			}
		}
		for (struct object *obj = p->gear; obj; obj = obj->next) {
			if (object_is_equipped(p->body, obj)) {
				sbook =  combine_class_books(p, &obj->kind->magic, count, spells, maxidx, spellps, books, book);
				if (!first)
					first = sbook;
				for(int i=0;i<MAX_EGOS;i++) {
					if (obj->ego[i]) {
						sbook =  combine_class_books(p, &obj->ego[i]->magic, count, spells, maxidx, spellps, books, book);
						if (!first)
							first = sbook;
					}
				}
				if (obj->artifact) {
					sbook =  combine_class_books(p, &obj->artifact->magic, count, spells, maxidx, spellps, books, book);
					if (!first)
						first = sbook;
				}
			}
		}
	}

	return first;
}

/**
 * Collect spells from a book into the spells[] array, allocating
 * appropriate memory (by calling combine_books twice, first to
 * get the size then again to fill the newly allocated array).
 */
static int collect_from_book(const struct player *p, int **spells, struct class_spell ***spellps, int *n_i, int *books, struct class_book ***book)
{
	int n_spells = 0;

	if (n_i)
		*n_i = 0;

	/* Count the spells */
	combine_books(p, &n_spells, NULL, n_i, NULL, NULL, NULL);
	/* Exit early if there are none */
	if (!n_spells) {
		if (spells)
			*spells = NULL;
		if (spellps)
			*spellps = NULL;
		return 0;
	}

	/* Allocate the array(s) */
	if (spells)
		*spells = mem_zalloc(n_spells * sizeof(*spells));
	if (spellps && n_i)
		*spellps = mem_zalloc(*n_i * sizeof(*spellps));

	/* Write the spells */
	n_spells = 0;
	if (n_i)
		*n_i = 0;

	combine_books(p, &n_spells, spells ? *spells : NULL, n_i, spellps ? *spellps : NULL, books, book);

	return n_spells;
}

const struct class_spell *spell_by_index(const struct player *p, int index)
{
	struct class_spell **spellps = NULL;
	int maxidx = 0;
	collect_from_book(p, NULL, &spellps, &maxidx, NULL, NULL);

	/* Check index validity */
	if (index < 0 || index >= maxidx) {
		mem_free(spellps);
		return NULL;
	}

	/* Find the spell */
	struct class_spell *ret = spellps[index];
	mem_free(spellps);
	return ret;
}

int spell_collect_from_book(struct player *p, int **spells, int *books, struct class_book ***book)
{
	return collect_from_book(p, spells, NULL, NULL, books, book);
}

/**
 * True if at least one spell in spells[] is OK according to spell_test.
 */
bool spell_okay_list(const struct player *p,
		bool (*spell_test)(const struct player *p, int spell),
		const int spells[], int n_spells)
{
	int i;
	bool okay = false;

	if (!spell_test)
		return true;

	for (i = 0; i < n_spells; i++)
		if (spell_test(p, spells[i]))
			okay = true;

	return okay;
}

/**
 * True if the spell is castable.
 */
bool spell_okay_to_cast(const struct player *p, int spell)
{
	return (p->spell_flags[spell] & PY_SPELL_LEARNED);
}

/**
 * True if the spell is browsable.
 */
bool spell_okay_to_browse(const struct player *p, int spell_index)
{
	const struct class_spell *spell = spell_by_index(p, spell_index);
	return spell && spell->slevel < 99;
}

/**
 * Spell failure adjustment by casting stat level
 */
static int fail_adjust(struct player *p, const struct class_spell *spell)
{
	int stat = spell->stat;
	return adj_mag_stat[p->state.stat_ind[stat]];
}

/**
 * Spell minimum failure by casting stat level
 */
static int min_fail(struct player *p, const struct class_spell *spell)
{
	int stat = spell->stat;
	return adj_mag_fail[p->state.stat_ind[stat]];
}

/**
 * Returns chance of failure for a spell
 */
s16b spell_chance(int spell_index)
{
	int chance = 100, minfail;
	int addfail = 0;

	const struct class_spell *spell;

	/* Get the spell */
	spell = spell_by_index(player, spell_index);
	if (!spell) return chance;

	/* Extract the base spell failure rate */
	chance = spell->sfail;

	/* Reduce failure rate by "effective" level adjustment */
	chance -= 3 * (player->lev - spell->slevel);

	/* Reduce failure rate by casting stat level adjustment */
	chance -= fail_adjust(player, spell);

	/* Get the minimum failure rate for the casting stat level */
	minfail = min_fail(player, spell);

	/* Non zero-fail characters never get better than 5 percent */
	if (!player_has(player, PF_ZERO_FAIL) && minfail < 5) {
		minfail = 5;
	}

	/* Necromancers are punished by being on lit squares */
	if (player_has(player, PF_UNLIGHT) && square_islit(cave, player->grid)) {
		addfail += 25;
	}

	/* Fear makes spells harder (before minfail) */
	/* Note that spells that remove fear have a much lower fail rate than
	 * surrounding spells, to make sure this doesn't cause mega fail */
	if (player_of_has(player, OF_AFRAID))
		addfail += 20;

	/* Minimal and maximal failure rate */
	if (chance < minfail) chance = minfail;

	/* Stunning makes spells harder (after minfail) */
	if (player->timed[TMD_STUN]) {
		addfail += 15 + ((MIN(player->timed[TMD_STUN], 100)) / 5);
	}

	/* Amnesia makes spells very difficult */
	if (player->timed[TMD_AMNESIA]) {
		addfail += 50;
	}

	/* Apply additional fail. This should be equivalent to an extra percentage at
	 * 0% fail but scale down towards the 95% limit.
	 */
	if (chance < 95) {
		chance += (addfail * (95 - chance)) / 95;
	}

	/* Always a 5 percent chance of working */
	if (chance > 95) {
		chance = 95;
	}

	/* Return the chance */
	return (chance);
}

static int beam_chance(void)
{
	int plev = player->lev;
	return (player_has(player, PF_BEAM) ? plev : (plev / 2));
}

/**
 * Cast the specified spell
 */
bool spell_cast(int spell_index, int dir, struct command *cmd)
{
	int chance;
	bool *ident = mem_zalloc(sizeof(*ident));
	int beam  = beam_chance();

	/* Get the spell */
	const struct class_spell *spell = spell_by_index(player, spell_index);

	/* Spell failure chance */
	chance = spell_chance(spell_index);

	/* Fail or succeed */
	if (randint0(100) < chance) {
		event_signal(EVENT_INPUT_FLUSH);

		/* Default per casting stat */
		static const char *deffailmsg[STAT_MAX] = {
			"Your strength proves insufficient!" , // STR
			"You failed to concentrate hard enough!",	// INT
			"You failed to concentrate hard enough!",	// WIS
			"You fumble!", // DEX
			"Your stamina fails you!", // CON
			"Your confidence falters!", //CHR
			"You fail to move quickly enough!", //SPD
		};

		/* But only use them if the spell doesn't have a message of its own */
		const char *failmsg = deffailmsg[spell->stat];
		if (spell->failmsg)
			failmsg = spell->failmsg;
		msg(failmsg);
	} else {
		/* Cast the spell */
		if (!effect_do(spell->effect, source_player(), NULL, ident, true, dir,
					   beam, 0, cmd, 0)) {
			mem_free(ident);
			return false;
		}

		/* The cost - HP, cooldown */
		if (randcalc(spell->hp, 0, AVERAGE) != 0) {
			take_hit(player, randcalc(spell->hp, 0, RANDOMISE), "overexertion");
		}
		if (randcalc(spell->turns, 0, AVERAGE) != 0) {
			player->spell[spell_index].cooldown += randcalc(spell->turns, 0, RANDOMISE);
		}

		/* A spell was cast */
		sound(MSG_SPELL);

		/* for the first time */
		if (!(player->spell_flags[spell_index] & PY_SPELL_WORKED)) {
			int e = spell->sexp;

			/* The spell worked */
			player->spell_flags[spell_index] |= PY_SPELL_WORKED;

			/* Gain experience */
			player_exp_gain(player, e * spell->slevel);

			/* Redraw object recall */
			player->upkeep->redraw |= (PR_OBJECT);
		}

		/* Track the number of spells cast, and the most recent time */
		player->spell[spell_index].uses++;
		player->spell[spell_index].turn = turn;
	}

	mem_free(ident);
	return true;
}


bool spell_needs_aim(int spell_index)
{
	const struct class_spell *spell = spell_by_index(player, spell_index);
	assert(spell);
	return effect_aim(spell->effect);
}

size_t append_random_value_string(char *buffer, size_t size, const random_value *rv)
{
	size_t offset = 0;

	if (rv->base > 0) {
		offset += strnfmt(buffer + offset, size - offset, "%d", rv->base);

		if (rv->dice > 0 && rv->sides > 0) {
			offset += strnfmt(buffer + offset, size - offset, "+");
		}
	}

	if (rv->dice == 1 && rv->sides > 0) {
		offset += strnfmt(buffer + offset, size - offset, "d%d", rv->sides);
	} else if (rv->dice > 1 && rv->sides > 0) {
		offset += strnfmt(buffer + offset, size - offset, "%dd%d", rv->dice,
						  rv->sides);
	}

	return offset;
}

static void spell_effect_append_value_info(const struct effect *effect,
										   char *p, size_t len)
{
	random_value rv = {0, 0, 0, 0};
	const char *type = NULL;
	const char *special = NULL;
	size_t offset = strlen(p);

	type = effect_info(effect);

	if (effect->dice != NULL)
		dice_roll(effect->dice, &rv);

	/* Handle some special cases where we want to append some additional info */
	switch (effect->index) {
		case EF_HEAL_HP:
			/* Append percentage only, as the fixed value is always displayed */
			if (rv.m_bonus) special = format("/%d%%", rv.m_bonus);
			break;
		case EF_TELEPORT:
			/* m_bonus means it's a weird random thing */
			if (rv.m_bonus) special = "random";
			break;
		case EF_SPHERE:
			/* Append radius */
			if (effect->radius) {
				int rad = effect->radius;
				special = format(", rad %d", rad);
			} else {
				special = ", rad 2";
			}
			break;
		case EF_BALL:
			/* Append radius */
			if (effect->radius) {
				int rad = effect->radius;
				if (effect->other) {
					rad += player->lev / effect->other;
				}
				special = format(", rad %d", rad);
			} else {
				special = "rad 2";
			}
			break;
		case EF_STRIKE:
			/* Append radius */
			if (effect->radius) {
				special = format(", rad %d", effect->radius);
			}
			break;
		case EF_SHORT_BEAM: {
			/* Append length of beam */
			int beam_len = effect->radius;
			if (effect->other) {
				beam_len += player->lev / effect->other;
				beam_len = MIN(beam_len, z_info->max_range);
			}
			special = format(", len %d", beam_len);
			break;
		}
		case EF_SWARM:
			/* Append number of projectiles. */
			special = format("x%d", rv.m_bonus);
			break;
		default:
			break;
	}

	if (type == NULL)
		return;

	if (offset) {
		offset += strnfmt(p + offset, len - offset, ";");
	}

	if ((rv.base > 0) || (rv.dice > 0 && rv.sides > 0)) {
		offset += strnfmt(p + offset, len - offset, " %s ", type);
		offset += append_random_value_string(p + offset, len - offset, &rv);

		if (special != NULL)
			strnfmt(p + offset, len - offset, "%s", special);
	}
}

void get_spell_info(int spell_index, char *p, size_t len)
{
	struct effect *effect = spell_by_index(player, spell_index)->effect;

	p[0] = '\0';

	while (effect) {
		spell_effect_append_value_info(effect, p, len);
		effect = effect->next;
	}
}

static int spell_value_base_spell_power(void)
{
	int power = 0;

	/* Check the reference race first */
	if (ref_race)
	   power = ref_race->spell_power;
	/* Otherwise the current monster if there is one */
	else if (cave->mon_current > 0)
		power = cave_monster(cave, cave->mon_current)->race->spell_power;

	return power;
}

static int spell_value_base_player_level(void)
{
	return player->lev;
}

static int spell_value_base_dungeon_level(void)
{
	return cave->depth;
}

static int spell_value_base_max_sight(void)
{
	return z_info->max_sight;
}

static int spell_value_base_weapon_damage(void)
{
	struct object *obj = player->body.slots[slot_by_name(player, "weapon")].obj;
	if (!obj) {
		return 0;
	}
	return (damroll(obj->dd, obj->ds) + obj->to_d);
}

static int spell_value_base_player_hp(void)
{
	return player->chp;
}

static int spell_value_base_monster_percent_hp_gone(void)
{
	/* Get the targeted monster, fail horribly if none */
	struct monster *mon = target_get_monster();

	return mon ? (((mon->maxhp - mon->hp) * 100) / mon->maxhp) : 0;
}

expression_base_value_f spell_value_base_by_name(const char *name)
{
	static const struct value_base_s {
		const char *name;
		expression_base_value_f function;
	} value_bases[] = {
		{ "SPELL_POWER", spell_value_base_spell_power },
		{ "PLAYER_LEVEL", spell_value_base_player_level },
		{ "DUNGEON_LEVEL", spell_value_base_dungeon_level },
		{ "MAX_SIGHT", spell_value_base_max_sight },
		{ "WEAPON_DAMAGE", spell_value_base_weapon_damage },
		{ "PLAYER_HP", spell_value_base_player_hp },
		{ "MONSTER_PERCENT_HP_GONE", spell_value_base_monster_percent_hp_gone },
		{ NULL, NULL },
	};
	const struct value_base_s *current = value_bases;

	while (current->name != NULL && current->function != NULL) {
		if (my_stricmp(name, current->name) == 0)
			return current->function;

		current++;
	}

	return NULL;
}
