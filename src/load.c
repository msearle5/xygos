/**
 * \file load.c
 * \brief Individual loading functions
 *
 * Copyright (c) 1997 Ben Harrison, and others
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
#include "effects.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-group.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "mon-mutant.h"
#include "mon-spell.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-fault.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-init.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-randart.h"
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-quest.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "savefile.h"
#include "store.h"
#include "trap.h"
#include "ui-term.h"
#include "world.h"

/**
 * Setting this to 1 and recompiling gives a chance to recover a savefile 
 * where the object list has become corrupted.  Don't forget to reset to 0
 * and recompile again as soon as the savefile is viable again.
 */
#define OBJ_RECOVER 0

/**
 * Dungeon constants
 */
static uint8_t square_size = 0;

/**
 * Player constants
 */
static uint8_t hist_size = 0;

/**
 * Object constants
 */
static uint8_t obj_mod_max = 0;
static uint8_t of_size = 0;
static uint8_t elem_max = 0;
static uint8_t brand_max;
static uint8_t slay_max;
static uint8_t fault_max;

/**
 * Monster constants
 */
static uint8_t mflag_size = 0;

/**
 * Trap constants
 */
static uint8_t trf_size = 0;

/**
 * Shorthand function pointer for rd_item version
 */
typedef struct object *(*rd_item_t)(void);

/**
 * Read an object.
 */
static struct object *rd_item(void)
{
	struct object *obj = object_new();

	uint8_t tmp8u;
	uint16_t tmp16u;
	int16_t effect;

	size_t i;
	char buf[128];
	uint8_t ver = 1;

	rd_u16b(&tmp16u);
	rd_byte(&ver);
	if (tmp16u != 0xffff)
		return NULL;

	rd_u16b(&obj->oidx);

	/* Location */
	rd_byte(&tmp8u);
	obj->grid.y = tmp8u;
	rd_byte(&tmp8u);
	obj->grid.x = tmp8u;

	/* Type/Subtype */
	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->tval = tval_find_idx(buf);
	}
	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->sval = lookup_sval(obj->tval, buf);
	}
	rd_s32b(&obj->pval);

	rd_byte(&obj->number);
	rd_s32b(&obj->weight);

	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->artifact = lookup_artifact_name(buf);
		if (!obj->artifact) {
			assert(player->artifact);
			if (streq(buf, player->artifact)) {
				obj->artifact = lookup_artifact_name("of You");
				if (obj->artifact->name)
					string_free(obj->artifact->name);
				((struct artifact *)(obj->artifact))->name = string_make(buf);
			}
		}
		if (!obj->artifact) {
			note(format("Couldn't find artifact %s!", buf));
			return NULL;
		}
	}

	for(int i=0;i<MAX_EGOS;i++) {
		rd_string(buf, sizeof(buf));
		if (buf[0]) {
			obj->ego[i] = lookup_ego_item(buf, obj->tval, obj->sval);
			if (!obj->ego[i]) {
				note(format("Couldn't find ego item %s!", buf));
				return NULL;
			}
		}
	}
	rd_s16b(&effect);

	rd_s32b(&obj->timeout);

	rd_s16b(&obj->to_h);
	rd_s16b(&obj->to_d);
	rd_s16b(&obj->to_a);

	rd_s16b(&obj->ac);

	rd_byte(&obj->dd);
	rd_byte(&obj->ds);

	rd_byte(&obj->origin);
	rd_byte(&obj->origin_depth);
	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->origin_race = lookup_monster(buf);
	}
	rd_byte(&obj->notice);

	for (i = 0; i < of_size; i++) {
		rd_byte(&obj->flags[i]);
		rd_byte(&obj->carried_flags[i]);
	}

	for (i = 0; i < PF_SIZE; i++) {
		rd_byte(&obj->pflags[i]);
	}

	for (i = 0; i < obj_mod_max; i++) {
		rd_s16b(&obj->modifiers[i]);
	}

	/* Read brands */
	rd_byte(&tmp8u);
	if (tmp8u) {
		obj->brands = mem_zalloc(z_info->brand_max * sizeof(bool));
		for (i = 0; i < brand_max; i++) {
			rd_byte(&tmp8u);
			obj->brands[i] = tmp8u ? true : false;
		}
	}

	/* Read slays */
	rd_byte(&tmp8u);
	if (tmp8u) {
		obj->slays = mem_zalloc(z_info->slay_max * sizeof(bool));
		for (i = 0; i < slay_max; i++) {
			rd_byte(&tmp8u);
			obj->slays[i] = tmp8u ? true : false;
		}
	}

	/* Read faults */
	rd_byte(&tmp8u);
	if (tmp8u) {
		obj->faults = mem_zalloc(z_info->fault_max * sizeof(struct fault_data));
		for (i = 0; i < fault_max; i++) {
			rd_byte(&tmp8u);
			obj->faults[i].power = tmp8u;
			rd_u16b(&tmp16u);
			obj->faults[i].timeout = tmp16u;
		}
	}

	for (i = 0; i < elem_max; i++) {
		rd_s16b(&obj->el_info[i].res_level);
		rd_byte(&obj->el_info[i].flags);
	}

	/* Monster holding object */
	rd_s16b(&obj->held_m_idx);

	rd_s16b(&obj->mimicking_m_idx);

	/* Activation */
	rd_u16b(&tmp16u);
	if (tmp16u)
		obj->activation = &activations[tmp16u];
	rd_u16b(&tmp16u);
	obj->time.base = tmp16u;
	rd_u16b(&tmp16u);
	obj->time.dice = tmp16u;
	rd_u16b(&tmp16u);
	obj->time.sides = tmp16u;

	/* Save the inscription */
	rd_string(buf, sizeof(buf));
	if (buf[0]) obj->note = quark_add(buf);

	/* Lookup item kind */
	obj->kind = lookup_kind(obj->tval, obj->sval);

	/* Check we have a kind */
	if ((!obj->tval && !obj->sval) || !obj->kind) {
		object_delete(NULL, NULL, &obj);
		return NULL;
	}

	/* Set effect */
	obj->effect = NULL;
	if (effect < 0) {
		if (effect == SHRT_MIN) {
			obj->effect = obj->kind->effect;
		} else if (effect < -MAX_EGOS) {
			note(format("Bad effect %d!", effect));
		} else if (!obj->ego[-effect]) {
			note(format("Bad effect %d, no ego!", effect));
		} else {
			obj->effect = obj->ego[-effect]->effect;
		}
	}

	/* Success */
	return obj;
}


/**
 * Read a monster
 */
static bool rd_monster(struct chunk *c, struct monster *mon)
{
	uint8_t tmp8u;
	uint16_t tmp16u;
	char race_name[80];
	size_t j;
	bool delete = false;

	/* Read the monster race */
	rd_u16b(&tmp16u);
	mon->midx = tmp16u;
	rd_string(race_name, sizeof(race_name));
	mon->race = lookup_monster(race_name);
	if (!mon->race) {
		mon->race = get_mutant_race_by_name(race_name);
		if (!mon->race) {
			note(format("Monster race %s no longer exists!", race_name));
			return false;
		}
	}
	rd_string(race_name, sizeof(race_name));
	if (streq(race_name, "none")) {
		mon->original_race = NULL;
	} else {
		mon->original_race = lookup_monster(race_name);
	}

	/* Read the other information */
	rd_byte(&tmp8u);
	mon->grid.y = tmp8u;
	rd_byte(&tmp8u);
	mon->grid.x = tmp8u;
	rd_s16b(&mon->hp);
	rd_s16b(&mon->maxhp);
	rd_byte(&mon->mspeed);
	rd_byte(&mon->energy);

	rd_byte(&tmp8u);

	for (j = 0; j < tmp8u; j++)
		rd_s16b(&mon->m_timed[j]);

	/* Read and extract the flag */
	for (j = 0; j < mflag_size; j++)
		rd_byte(&mon->mflag[j]);

	for (j = 0; j < of_size; j++)
		rd_byte(&mon->known_pstate.flags[j]);

	for (j = 0; j < elem_max; j++)
		rd_s16b(&mon->known_pstate.el_info[j].res_level);

	rd_u16b(&tmp16u);

	if (tmp16u) {
		/* Find and set the mimicked object */
		struct object *square_obj = square_object(c, mon->grid);

		/* Try and find the mimicked object; if we fail, delete the monster */
		while (square_obj) {
			if (square_obj->mimicking_m_idx == tmp16u) break;
			square_obj = square_obj->next;
		}
		if (square_obj) {
			mon->mimicked_obj = square_obj;
		} else {
			delete = true;
		}
	}

	/* Read all the held objects (order is unimportant) */
	while (true) {
		struct object *obj = rd_item();
		if (!obj)
			break;

		pile_insert(&mon->held_obj, obj);
		assert(obj->oidx);
		assert(obj->oidx < c->obj_max);
		assert(c->objects[obj->oidx] == NULL);
		c->objects[obj->oidx] = obj;
	}

	/* Read group info */
	rd_u16b(&tmp16u);
	mon->group_info[PRIMARY_GROUP].index = tmp16u;
	rd_byte(&tmp8u);
	mon->group_info[PRIMARY_GROUP].role = tmp8u;
	rd_u16b(&tmp16u);
	mon->group_info[SUMMON_GROUP].index = tmp16u;
	rd_byte(&tmp8u);
	mon->group_info[SUMMON_GROUP].role = tmp8u;

	/* Now delete the monster if necessary */
	if (delete) {
		delete_monster(mon->grid);
	}

	return true;
}


/**
 * Read a trap record
 */
static void rd_trap(struct trap *trap)
{
	unsigned i;
	uint8_t tmp8u;
	char buf[80];

	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		trap->kind = lookup_trap(buf);
		trap->t_idx = trap->kind->tidx;
	}
	rd_byte(&tmp8u);
	trap->grid.y = tmp8u;
	rd_byte(&tmp8u);
	trap->grid.x = tmp8u;
	rd_byte(&trap->power);
	rd_byte(&trap->timeout);

	for (i = 0; i < TRF_SIZE; i++) {
		if (i >= trf_size) {
			trap->flags[i] = 0;
		} else {
			rd_byte(&trap->flags[i]);
		}
	}
}

/**
 * Read RNG state
 *
 * There were originally 64 bytes of randomizer saved. Now we only need
 * 32 + 5 bytes saved, so we'll read an extra 27 bytes at the end which won't
 * be used.
 */
int rd_randomizer(void)
{
	int i;
	uint32_t noop;

	/* current value for the simple RNG */
	rd_u32b(&Rand_value);

	/* state index */
	rd_u32b(&state_i);

	/* for safety, make sure state_i < RAND_DEG */
	state_i = state_i % RAND_DEG;
    
	/* RNG variables */
	rd_u32b(&z0);
	rd_u32b(&z1);
	rd_u32b(&z2);
    
	/* RNG state */
	for (i = 0; i < RAND_DEG; i++)
		rd_u32b(&STATE[i]);

	/* NULL padding */
	for (i = 0; i < 59 - RAND_DEG; i++)
		rd_u32b(&noop);

	Rand_quick = false;

	return 0;
}


/**
 * Read options.
 */
int rd_options(void)
{
	uint8_t b;

	/*** Special info */

	/* Read "delay_factor" */
	rd_byte(&b);
	player->opts.delay_factor = b;

	/* Read "hitpoint_warn" */
	rd_byte(&b);
	player->opts.hitpoint_warn = b;

	/* Read lazy movement delay */
	rd_byte(&b);
	player->opts.lazymove_delay = b;

	/* Read autosave delay */
	rd_s32b(&player->opts.autosave_delay);

	/* Read sidebar mode (if it's an actual game) */
	if (angband_term[0]) {
		rd_byte(&b);
		if (b >= SIDEBAR_MAX) b = SIDEBAR_LEFT;
		SIDEBAR_MODE = b;
	} else {
		strip_bytes(1);
	}


	/* Read options */
	while (1) {
		uint8_t value;
		char name[40];
		rd_string(name, sizeof name);

		if (!name[0])
			break;

		rd_byte(&value);
		option_set(name, !!value);
	}

	return 0;
}

/**
 * Read the saved messages
 */
int rd_messages(void)
{
	int i;
	char buf[128];
	uint16_t tmp16u;

	int16_t num;

	/* Total */
	rd_s16b(&num);

	/* Read the messages */
	for (i = 0; i < num; i++) {
		/* Read the message */
		rd_string(buf, sizeof(buf));

		/* Read the message type */
		rd_u16b(&tmp16u);

		/* Save the message */
		message_add(buf, tmp16u);
	}

	return 0;
}

/**
 * Read monster memory.
 */
int rd_monster_memory(void)
{
	uint16_t nkill, ntheft;
	char buf[128];
	int i;

	/* Monster temporary flags */
	rd_byte(&mflag_size);

	/* Incompatible save files */
	if (mflag_size > MFLAG_SIZE) {
	        note(format("Too many (%u) monster temporary flags!", mflag_size));
		return (-1);
	}

	/* Reset maximum numbers per level */
	for (i = 1; z_info && i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];
		race->max_num = 100;
		if (rf_has(race->flags, RF_UNIQUE))
			race->max_num = 1;
	}

	rd_string(buf, sizeof(buf));
	while (!streq(buf, "No more monsters")) {
		struct monster_race *race = lookup_monster(buf);

		/* Get the kill and theft counts, skip if monster invalid */
		rd_u16b(&nkill);
		rd_u16b(&ntheft);
		if (!race) continue;

		/* Store the kill count, ensure dead uniques stay dead */
		l_list[race->ridx].pkills = nkill;
		if (rf_has(race->flags, RF_UNIQUE) && nkill)
			race->max_num = 0;

		/* Store the theft count */
		l_list[race->ridx].thefts = ntheft;

		/* Look for the next monster */
		rd_string(buf, sizeof(buf));
	}

	return 0;
}


int rd_object_memory(void)
{
	size_t i;
	uint16_t tmp16u;

	/* Object Memory */
	rd_u16b(&tmp16u);
	if (tmp16u > z_info->k_max) {
		note(format("Too many (%u, not %u) object kinds!", tmp16u, z_info->k_max));
		return (-1);
	}

	/* Object flags */
	rd_byte(&of_size);
	if (of_size > OF_SIZE) {
	        note(format("Too many (%u) object flags!", of_size));
		return (-1);
	}

	/* Object modifiers */
	rd_byte(&obj_mod_max);
	if (obj_mod_max > OBJ_MOD_MAX) {
	        note(format("Too many (%u) object modifiers allowed!",
						obj_mod_max));
		return (-1);
	}

	/* Elements */
	rd_byte(&elem_max);
	if (elem_max > ELEM_MAX) {
	        note(format("Too many (%u) elements allowed!", elem_max));
		return (-1);
	}

	/* Brands */
	rd_byte(&brand_max);
	if (brand_max > z_info->brand_max) {
	        note(format("Too many (%u) brands allowed!", brand_max));
		return (-1);
	}

	/* Slays */
	rd_byte(&slay_max);
	if (slay_max > z_info->slay_max) {
	        note(format("Too many (%u) slays allowed!", slay_max));
		return (-1);
	}

	/* Faults */
	rd_byte(&fault_max);
	if (fault_max > z_info->fault_max) {
	        note(format("Too many (%u) faults allowed!", fault_max));
		return (-1);
	}

	/* Read the kind knowledge */
	for (i = 0; i < tmp16u; i++) {
		uint8_t tmp8u;
		struct object_kind *kind = &k_info[i];

		rd_byte(&tmp8u);

		kind->aware = (tmp8u & 0x01) ? true : false;
		kind->tried = (tmp8u & 0x02) ? true : false;
		kind->everseen = (tmp8u & 0x08) ? true : false;

		if (tmp8u & 0x04) kind_ignore_when_aware(kind);
		if (tmp8u & 0x10) kind_ignore_when_unaware(kind);
	}

	return 0;
}



int rd_quests(void)
{
	uint16_t tmp16u;

	/* Load the Quests */
	rd_u16b(&tmp16u);
	if (tmp16u > z_info->quest_max) {
		note(format("Too many (%u) quests!", tmp16u));
		return (-1);
	}

	/* Load the Quests */
	player_quests_reset(player);
	rdwr_quests();

	return 0;
}

int rd_world(void)
{
	world_cleanup_towns();
	rd_u16b(&z_info->town_max);
	t_info = mem_zalloc(sizeof(*t_info) * z_info->town_max);
	rdwr_world();
	world_init_dungeons();
	world_build_distances();
	Rand_quick = false;
	return 0;
}

void rdwr_player_levels(void)
{
	rdwr_s32b(&player->au);

	/* XP */
	rdwr_s32b(&player->max_exp);
	rdwr_s32b(&player->exp);
	rdwr_u16b(&player->exp_frac);

	/* HP */
	rdwr_s16b(&player->mhp);
	rdwr_s16b(&player->chp);
	rdwr_u16b(&player->chp_frac);

	/* Talents */
	rdwr_u16b(&player->talent_points);

	/* Get an empty array of talent gain counts */
	int n_classes = 0;
	for (struct player_class *c = classes; c; c = c->next)
		if ((int)c->cidx > n_classes)
			n_classes = c->cidx;
	n_classes++;
	if (!player->talent_gain)
		player->talent_gain = mem_alloc(PY_MAX_LEVEL * n_classes);
	for(int i=0;i<PY_MAX_LEVEL * n_classes;i++)
		rdwr_byte(&player->talent_gain[i]);

	/* Level by class */
	for(int i=0;i<(int)sizeof(player->lev_class);i++)
		rdwr_byte(&player->lev_class[i]);
	rdwr_byte(&player->switch_class);

	/* Max Player and Dungeon Levels */
	rdwr_s16b(&player->max_lev);
	rdwr_s16b(&player->max_depth);
	rdwr_s16b(&player->danger);
	rdwr_s16b(&player->danger_reduction);
}

/**
 * Read the player information
 */
int rd_player(void)
{
	int i;
	uint8_t num;
	uint8_t stat_max = 0;
	char buf[80];
	struct player_shape *s;
	struct player_class *c;

	rd_string(player->full_name, sizeof(player->full_name));
	rd_string(player->died_from, 80);
	player->history = mem_zalloc(250);
	rd_string(player->history, 250);

	/* Player race */
	rd_string(buf, sizeof(buf));
	player->race = get_race_by_name(buf);

	/* Verify player race */
	if (!player->race) {
		note(format("Invalid player race (%s).", buf));
		return -1;
	}

	/* Player ext */
	rd_string(buf, sizeof(buf));
	player->extension = get_race_by_name(buf);

	/* Verify player extension */
	if (!player->extension) {
		note(format("Invalid player extension (%s).", buf));
		return -1;
	}

	/* Player personality */
	rd_string(buf, sizeof(buf));
	player->personality = get_race_by_name(buf);

	/* Verify player personality */
	if (!player->personality) {
		note(format("Invalid player personality (%s).", buf));
		return -1;
	}

	/* Player shape */
	rd_string(buf, sizeof(buf));
	for (s = shapes; s; s = s->next) {
		if (streq(s->name, buf)) {
			player->shape = s;
			break;
		}
	}

	/* If no player shape recorded, set to normal and hope for the best */
	if (!player->shape) {
		note(format("Invalid player shape (%s).", buf));
		return -1;
	}

	/* Player class */
	rd_string(buf, sizeof(buf));
	for (c = classes; c; c = c->next) {
		if (streq(c->name, buf)) {
			player->class = c;
			break;
		}
	}

	if (!player->class) {
		note(format("Invalid player class (%s).", buf));
		return -1;
	}

	/* Numeric name suffix */
	rd_byte(&player->opts.name_suffix);

	/* Special Race/Class info */
	rd_u16b(&player->expfact_low);
	rd_u16b(&player->expfact_high);

	/* Age/Height/Weight */
	rd_s16b(&player->age);
	rd_s16b(&player->ht);
	rd_s32b(&player->wt);

	/* Read the stat info */
	rd_byte(&stat_max);
	if (stat_max > STAT_MAX) {
		note(format("Too many stats (%d).", stat_max));
		return -1;
	}

	for (i = 0; i < stat_max; i++) rd_s16b(&player->stat_max[i]);
	for (i = 0; i < stat_max; i++) rd_s16b(&player->stat_cur[i]);
	for (i = 0; i < stat_max; i++) rd_s16b(&player->stat_map[i]);
	for (i = 0; i < stat_max; i++) rd_s16b(&player->stat_birth[i]);

	rd_s16b(&player->ht_birth);
	rd_s32b(&player->wt_birth);
	rd_s32b(&player->au_birth);

	/* Player body */
	rd_string(buf, sizeof(buf));
	player->body.name = string_make(buf);
	rd_u16b(&player->body.count);
	if (player->body.count > z_info->equip_slots_max) {
		note(format("Too many (%u) body parts!", player->body.count));
		return (-1);
	}

	player->body.slots = mem_zalloc(player->body.count *
									sizeof(struct equip_slot));
	for (i = 0; i < player->body.count; i++) {
		rd_u16b(&player->body.slots[i].type);
		rd_string(buf, sizeof(buf));
		player->body.slots[i].name = string_make(buf);
	}

	strip_bytes(4);

	rd_s16b(&player->lev);

	/* Verify player level */
	if ((player->lev < 1) || (player->lev > PY_MAX_LEVEL)) {
		note(format("Invalid player level (%d).", player->lev));
		return (-1);
	}

	rdwr_player_levels();
	set_primary_class();

	/* Player town */
	RDWR_PTR(&player->town, t_info);
	world_change_town(player->town);

	/* Hack -- Repair maximum player level */
	if (player->max_lev < player->lev) player->max_lev = player->lev;

	/* Hack -- Repair maximum dungeon level */
	if (player->max_depth < 0) player->max_depth = 1;
	if ((player->town) && (player->town->recall_depth <= 0))
		player->town->recall_depth = player->max_depth;

	/* Hack -- Reset cause of death */
	if (player->chp >= 0)
		my_strcpy(player->died_from, "(alive and well)",
				  sizeof(player->died_from));

	/* More info */
	strip_bytes(7);
	rd_byte(&player->unignoring);
	rd_s16b(&player->deep_descent);

	/* Read the flags */
	rd_s16b(&player->energy);
	rd_s16b(&player->word_recall);

	if (!player->spell)
		player->spell = mem_zalloc(sizeof(*player->spell) * total_spells);
	for (i = 0; i < total_spells; i++)
		rdwr_spell_state(&player->spell[i]);

	/* Find the number of timed effects */
	rd_byte(&num);

	if (num <= TMD_MAX) {
		/* Read all the effects */
		for (i = 0; i < num; i++)
			rd_s16b(&player->timed[i]);

		/* Initialize any entries not read */
		if (num < TMD_MAX)
			memset(player->timed + num, 0, (TMD_MAX - num) * sizeof(int16_t));
	} else {
		/* Probably in trouble anyway */
		for (i = 0; i < TMD_MAX; i++)
			rd_s16b(&player->timed[i]);

		/* Discard unused entries */
		strip_bytes(2 * (num - TMD_MAX));
		note("Discarded unsupported timed effects");
	}

	if (rdwr_player() == false)
		return -1;

	/* Player flags */
	for(i=0; i < (int)PF_SIZE; i++)
		rd_byte(&player->ability_pflags[i]);

	/* Class/race specific */
	player_hook(loadsave, false);

	/* Future use */
	strip_bytes(32);

	return 0;
}


/**
 * Read ignore and autoinscription submenu for all known objects
 */
int rd_ignore(void)
{
	size_t i, j;
	uint8_t tmp8u = 24;
	uint16_t file_e_max;
	uint16_t itype_size;
	uint16_t inscriptions;

	/* Read how many ignore bytes we have */
	rd_byte(&tmp8u);

	/* Check against current number */
	if (tmp8u != ignore_size) {
		strip_bytes(tmp8u);
	} else {
		for (i = 0; i < ignore_size; i++)
			rd_byte(&ignore_level[i]);
	}

	/* Read the number of saved ego-item */
	rd_u16b(&file_e_max);
	rd_u16b(&itype_size);
	if (itype_size > ITYPE_SIZE) {
		note(format("Too many (%u) ignore bytes!", itype_size));
		return (-1);
	}

	for (i = 0; i < file_e_max; i++) {
		if (i < z_info->e_max) {
			bitflag flags, itypes[ITYPE_SIZE];
			
			/* Read and extract the everseen flag */
			rd_byte(&flags);
			e_info[i].everseen = (flags & 0x02) ? true : false;

			/* Read and extract the ignore flags */
			for (j = 0; j < itype_size; j++)
				rd_byte(&itypes[j]);

			/* If number of ignore types has changed, don't set anything */
			if (itype_size == ITYPE_SIZE) {
				for (j = ITYPE_NONE; j < ITYPE_MAX; j++)
					if (itype_has(itypes, j))
						ego_ignore_toggle(i, j);
			}
		}
	}

	/* Read the current number of aware object auto-inscriptions */
	rd_u16b(&inscriptions);

	/* Read the aware object autoinscriptions array */
	for (i = 0; i < inscriptions; i++) {
		char tmp[80];
		uint8_t tval, sval;
		struct object_kind *k;

		rd_string(tmp, sizeof(tmp));
		tval = tval_find_idx(tmp);
		rd_string(tmp, sizeof(tmp));
		sval = lookup_sval(tval, tmp);
		k = lookup_kind(tval, sval);
		if (!k)
			quit_fmt("lookup_kind(%d, %d) failed", tval, sval);
		rd_string(tmp, sizeof(tmp));
		k->note_aware = quark_add(tmp);
	}

	/* Read the current number of unaware object auto-inscriptions */
	rd_u16b(&inscriptions);

	/* Read the unaware object autoinscriptions array */
	for (i = 0; i < inscriptions; i++) {
		char tmp[80];
		uint8_t tval, sval;
		struct object_kind *k;

		rd_string(tmp, sizeof(tmp));
		tval = tval_find_idx(tmp);
		rd_string(tmp, sizeof(tmp));
		sval = lookup_sval(tval, tmp);
		k = lookup_kind(tval, sval);
		if (!k)
			quit_fmt("lookup_kind(%d, %d) failed", tval, sval);
		rd_string(tmp, sizeof(tmp));
		k->note_unaware = quark_add(tmp);
	}

	/* Read the current number of icon auto-inscriptions */
	rd_u16b(&inscriptions);

	/* Read the icon autoinscriptions array */
	for (i = 0; i < inscriptions; i++) {
		char tmp[80];
		int16_t iconid;

		rd_s16b(&iconid);
		rd_string(tmp, sizeof(tmp));
		icon_set_note(iconid, tmp);
	}

	return 0;
}


int rd_misc(void)
{
	size_t i;
	uint8_t tmp8u;
	
	/* Read the randart seed */
	rd_u32b(&seed_randart);

	/* Read the flavors seed */
	rd_u32b(&seed_flavor);
	flavor_init();

	/* Special stuff */
	rd_u16b(&player->total_winner);
	rd_u16b(&player->noscore);


	/* Read "death" */
	rd_byte(&tmp8u);
	player->is_dead = tmp8u;

	/* Current turn */
	rd_s32b(&turn);

	//if (player->is_dead)
	//	return 0;

	/* Restore the standard artifacts (randarts may have been loaded) */
	cleanup_parser(&randart_parser);
	deactivate_randart_file();
	run_parser(&artifact_parser);

	select_artifact_max();

	/* Now only randomize the artifacts if required */
	if (OPT(player, birth_randarts)) {
		do_randart(seed_randart, false, false);
		deactivate_randart_file();
	}

	/* Property knowledge */
	/* Flags */
	for (i = 0; i < OF_SIZE; i++)
		rd_byte(&player->obj_k->flags[i]);

	/* Modifiers */
	for (i = 0; i < OBJ_MOD_MAX; i++) {
		rd_s16b(&player->obj_k->modifiers[i]);
	}

	/* Elements */
	for (i = 0; i < ELEM_MAX; i++) {
		rd_s16b(&player->obj_k->el_info[i].res_level);
		rd_byte(&player->obj_k->el_info[i].flags);
	}

	/* Read brands */
	for (i = 0; i < brand_max; i++) {
		rd_byte(&tmp8u);
		player->obj_k->brands[i] = tmp8u ? true : false;
	}

	/* Read slays */
	for (i = 0; i < slay_max; i++) {
		rd_byte(&tmp8u);
		player->obj_k->slays[i] = tmp8u ? true : false;
	}

	/* Read faults */
	for (i = 0; i < fault_max; i++) {
		rd_byte(&tmp8u);
		player->obj_k->faults[i].power = tmp8u;
	}

	/* Combat data */
	rd_s16b(&player->obj_k->ac);
	rd_s16b(&player->obj_k->to_a);
	rd_s16b(&player->obj_k->to_h);
	rd_s16b(&player->obj_k->to_d);
	rd_byte(&player->obj_k->dd);
	rd_byte(&player->obj_k->ds);
	return 0;
}

int rd_artifacts(void)
{
	int i;
	uint16_t tmp16u;

	/* Load the Artifacts */
	rd_u16b(&tmp16u);
	if (tmp16u > z_info->a_base + z_info->rand_art) {
		note(format("Too many (%u) artifacts!", tmp16u));
		return (-1);
	}

	/* Read the artifact flags */
	for (i = 0; i < tmp16u; i++) {
		uint8_t tmp8u;

		rd_byte(&tmp8u);
		aup_info[i].created = tmp8u ? true : false;
		rd_byte(&tmp8u);
		aup_info[i].seen = tmp8u ? true : false;
		rd_byte(&tmp8u);
		aup_info[i].everseen = tmp8u ? true : false;
		rd_byte(&tmp8u);
	}

	/* Determine number of artifacts to use */
	z_info->a_max = z_info->a_base;
	if (OPT(player, birth_randarts))
		z_info->a_max += z_info->rand_art;

	return 0;
}



int rd_player_hp(void)
{
	int i;
	uint16_t tmp16u;

	/* Read the player_hp array */
	rd_u16b(&tmp16u);
	if (tmp16u != PY_MAX_LEVEL * (classes->cidx + 1)) {
		note(format("Wrong (%u, not %u) hitpoint entries!", tmp16u, PY_MAX_LEVEL * (classes->cidx + 1)));
		return (-1);
	}
	if (!player->player_hp)
		player->player_hp = mem_alloc(sizeof(int16_t ) * tmp16u);

	/* Read the player_hp array */
	for (i = 0; i < tmp16u; i++)
		rd_s16b(&player->player_hp[i]);

	return 0;
}


/**
 * Read the player spells
 */
int rd_player_spells(void)
{
	int i;
	uint16_t tmp16u;
	
	int cnt;
	
	/* Read the number of spells */
	rd_u16b(&tmp16u);
	if (tmp16u > total_spells) {
		note(format("Too many player spells (%d).", tmp16u));
		return (-1);
	}

	/* Initialise */
	player_spells_init(player);
	
	/* Read the spell flags */
	for (i = 0; i < tmp16u; i++)
		rd_byte(&player->spell_flags[i]);
	
	/* Read the spell order */
	for (i = 0, cnt = 0; i < tmp16u; i++, cnt++)
		rd_byte(&player->spell_order[cnt]);
	
	/* Success */
	return (0);
}




/**
 * Read the player gear
 */
static int rd_gear_aux(rd_item_t rd_item_version, struct object **gear)
{
	uint8_t code;
	struct object *last_gear_obj = NULL;

	/* Get the first item code */
	rd_byte(&code);

	/* Read until done */
	while (code != FINISHED_CODE) {
		struct object *obj = (*rd_item_version)();

		/* Read the item */
		if (!obj) {
			note("Error reading item");
			return (-1);
		}

		/* Append the object */
		obj->prev = last_gear_obj;
		if (last_gear_obj)
			last_gear_obj->next = obj;
		else
			*gear = obj;
		last_gear_obj = obj;

		/* If it's equipment, wield it */
		if (code < player->body.count) {
			player->body.slots[code].obj = obj;
			player->upkeep->equip_cnt++;
		}

		/* Get the next item code */
		rd_byte(&code);
	}

	/* Success */
	return (0);
}

/**
 * Read the player gear - wrapper functions
 */
int rd_gear(void)
{
	struct object *obj, *known_obj;

	/* Get real gear */
	if (rd_gear_aux(rd_item, &player->gear))
		return -1;

	/* Get known gear */
	if (rd_gear_aux(rd_item, &player->gear_k))
		return -1;

	/* Align the two, add weight */
	for (obj = player->gear, known_obj = player->gear_k; obj;
		 obj = obj->next, known_obj = known_obj->next) {
		obj->known = known_obj;
		player->upkeep->total_weight += (obj->number * obj->weight);
	}

	calc_inventory(player);

	return 0;
}


/**
 * Read store contents
 */
static int rd_stores_aux(rd_item_t rd_item_version)
{
	int i;
	uint16_t tmp16u;

	/* Read the stores */
	rd_u16b(&tmp16u);
	assert(z_info->town_max);
	for(int t=0; t<z_info->town_max; t++) {
		if (t_info[t].stores)
			mem_free(t_info[t].stores);
		t_info[t].stores = mem_zalloc(MAX_STORES * sizeof(*t_info[t].stores));
		memcpy(t_info[t].stores, stores_init, MAX_STORES * sizeof(*t_info[t].stores));
	}

	if (player->town)
		stores = player->town->stores;
	else
		stores = NULL;

	stores_copy(stores_init);

	for(int t=0; t<z_info->town_max; t++) {
		for (i = 0; i < tmp16u; i++) {
			struct store *store = &t_info[t].stores[i];

			uint8_t own;
			uint16_t num;
			uint8_t n_owners;

			/* Read the basic info */
			rd_byte(&own);

			if (t == 0) {
				/* Load the number of owners */
				rd_byte(&n_owners);

				/* Load the owners' names */
				struct owner *o = store->owners;
				for(int i=0;i<n_owners;i++) {
					char buf[32];
					rd_string(buf, sizeof(buf));
					if (o->name) {
						string_free(o->name);
					}
					o->name = string_make(buf);
					rd_bool(&o->male);
					o = o->next;
				}
			}

			rd_u16b(&num);
			rd_s16b(&store->stock_size);

			/* XXX: refactor into store.c */
			store->owner = store_ownerbyidx(store, own);

			/* Read the items */
			for (; num; num--) {
				/* Read the known item */
				struct object *obj, *known_obj = (*rd_item_version)();
				if (!known_obj) {
					note("Error reading known item");
					return (-1);
				}

				/* Read the item */
				obj = (*rd_item_version)();
				if (!obj) {
					note("Error reading item");
					return (-1);
				}
				obj->known = known_obj;

				/* Accept any valid items */
				if (store->stock_num < z_info->store_inven_max && obj->kind) {
					if (store->sidx == STORE_HOME)
						home_carry(store, obj);
					else
						store_carry(store, obj, false);
				}
			}

			/* Read the entrance position */
			rd_u16b(&store->x);
			rd_u16b(&store->y);

			/* Read the ban days and reason */
			rd_u32b(&store->bandays);
			{
				char buf[128];
				rd_string(buf, sizeof(buf));
				store->banreason = buf[0] ? strdup(buf) : NULL;
			}

			/* Read the layaway index and day */
			rd_s32b(&store->layaway_idx);
			rd_s32b(&store->layaway_day);

			/* Destroyed flag and danger */
			rd_bool(&store->destroy);
			rd_bool(&store->open);
			rd_byte(&store->quest_status);
			rd_s32b(&store->max_danger);
		}
	}

	return 0;
}

/**
 * Read the stores - wrapper functions
 */
int rd_stores(void) { return rd_stores_aux(rd_item); }


/**
 * Read the dungeon
 *
 * The monsters/objects must be loaded in the same order
 * that they were stored, since the actual indexes matter.
 *
 * Note that the size of the dungeon is now the currrent dimensions of the
 * cave global variable.
 *
 * Note that dungeon objects, including objects held by monsters, are
 * placed directly into the dungeon, using "object_copy()", which will
 * copy "iy", "ix", and "held_m_idx", leaving "next_o_idx" blank for
 * objects held by monsters, since it is not saved in the savefile.
 *
 * After loading the monsters, the objects being held by monsters are
 * linked directly into those monsters.
 */
static int rd_dungeon_aux(struct chunk **c)
{
	struct chunk *c1;
	int i, n, y, x;

	uint16_t height, width;

	uint8_t count;
	uint8_t tmp8u;
	uint16_t tmp16u;
	char name[100];

	/* Header info */
	rd_string(name, sizeof(name));
	if (streq(name, "arena")) {
		player->upkeep->arena_level = true;
	}
	rd_u16b(&height);
	rd_u16b(&width);

	/* We need a cave struct */
	c1 = cave_new(height, width);
	c1->name = string_make(name);

    /* Run length decoding of cave->squares[y][x].info */
	for (n = 0; n < square_size; n++) {
		/* Load the dungeon data */
		for (x = y = 0; y < c1->height; ) {
			/* Grab RLE info */
			rd_byte(&count);
			rd_byte(&tmp8u);

			/* Apply the RLE info */
			for (i = count; i > 0; i--) {
				/* Extract "info" */
				c1->squares[y][x].info[n] = tmp8u;

				/* Advance/Wrap */
				if (++x >= c1->width) {
					/* Wrap */
					x = 0;

					/* Advance/Wrap */
					if (++y >= c1->height) break;
				}
			}
		}
	}

	/* Run length decoding of dungeon data */
	for (x = y = 0; y < c1->height; ) {
		/* Grab RLE info */
		rd_byte(&count);
		rd_byte(&tmp8u);

		/* Apply the RLE info */
		for (i = count; i > 0; i--) {
			/* Extract "feat" */
			square_set_feat(c1, loc(x, y), tmp8u);

			/* Advance/Wrap */
			if (++x >= c1->width) {
				/* Wrap */
				x = 0;

				/* Advance/Wrap */
				if (++y >= c1->height) break;
			}
		}
	}


	/* Read "feeling" */
	rd_byte(&tmp8u);
	c1->feeling = tmp8u;
	rd_u16b(&tmp16u);
	c1->feeling_squares = tmp16u;
	rd_s32b(&c1->turn);

	/* Read connector info */
	if (OPT(player, birth_levels_persist)) {
		rd_byte(&tmp8u);
		while (tmp8u != 0xff) {
			struct connector *current = mem_zalloc(sizeof *current);
			current->info = mem_zalloc(square_size * sizeof(bitflag));
			current->grid.x = tmp8u;
			rd_byte(&tmp8u);
			current->grid.y = tmp8u;
			rd_byte(&current->feat);
			for (n = 0; n < square_size; n++) {
				rd_byte(&current->info[n]);
			}
			current->next = c1->join;
			c1->join = current;
			rd_byte(&tmp8u);
		}
	}

	/* Assign */
	*c = c1;

	return 0;
}

/**
 * Read the floor object list
 */
static int rd_objects_aux(rd_item_t rd_item_version, struct chunk *c)
{
	int i;

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	/* Make the object list */
	rd_u16b(&c->obj_max);
	c->objects = mem_realloc(c->objects,
							 (c->obj_max + 1) * sizeof(struct object*));
	for (i = 0; i <= c->obj_max; i++)
		c->objects[i] = NULL;

	/* Read the dungeon items until one isn't returned */
	while (true) {
		struct object *obj = (*rd_item_version)();
		if (!obj)
			break;
#if OBJ_RECOVER
		if (square_in_bounds_fully(c, obj->grid) && c == cave) {
#else
		if (square_in_bounds_fully(c, obj->grid)) {
#endif
			pile_insert_end(&c->squares[obj->grid.y][obj->grid.x].obj, obj);
		}
		assert(obj->oidx);
		assert(c->objects[obj->oidx] == NULL);
		c->objects[obj->oidx] = obj;
	}

	return 0;
}

int rdwr_bitflag(bitflag *flags, int size)
{
	for(int i=0;i<size;i++)
		rdwr_byte(&flags[i]);
	return 0;
}

void rdwr_random(random_value *dice)
{
	rdwr_s32b(&dice->base);
	rdwr_s32b(&dice->dice);
	rdwr_s32b(&dice->sides);
	rdwr_s32b(&dice->m_bonus);
}

void rdwr_monster_blow(struct monster_blow *b)
{
	RDWR_PTR(&b->method, blow_methods);
	RDWR_PTR(&b->effect, blow_effects);
	rdwr_random(&b->dice);
	rdwr_s32b(&b->times_seen);
}

void rdwr_monster_drop(struct monster_drop *d)
{
	RDWR_PTR(&d->kind, k_info);
	rdwr_string_null((char **)(&d->art));
	rdwr_u32b(&d->tval);
	rdwr_u32b(&d->percent_chance);
	rdwr_u32b(&d->min);
	rdwr_u32b(&d->max);
}

void rdwr_monster_friends(struct monster_friends *f)
{
	rdwr_string(&f->name);
	RDWR_PTR(&f->race, r_info);
	RDWR_AS(&f->role, uint32_t);
	rdwr_u32b(&f->percent_chance);
	rdwr_u32b(&f->number_dice);
	rdwr_u32b(&f->number_side);
}

void rdwr_monster_friends_base(struct monster_friends_base *f)
{
	RDWR_PTR(&f->base, rb_info);
	RDWR_AS(&f->role, uint32_t);
	rdwr_u32b(&f->percent_chance);
	rdwr_u32b(&f->number_dice);
	rdwr_u32b(&f->number_side);
}

void rdwr_monster_mimic(struct monster_mimic *m)
{
	RDWR_PTR(&m->kind, k_info);
}

void rdwr_monster_shape(struct monster_shape *s)
{
	rdwr_string(&s->name);
	RDWR_PTR(&s->race, r_info);
	RDWR_PTR(&s->base, rb_info);
}

/**
 * Read/write a race
 * Used for custom monsters (shop owner)
 */
int rdwr_race(struct monster_race *r)
{
	rdwr_u32b(&r->ridx);
	rdwr_string(&r->name);
	rdwr_string(&r->text);
	rdwr_string_null(&r->plural);			/* Optional pluralized name */
	rdwr_string_null(&r->grow);

	RDWR_NPTR(&r->base, rb_info);
	assert(r->base);
	RDWR_PTR(&r->pain, pain_messages);					/* Pain messages */

	rdwr_s32b(&r->avg_hp);				/* Average HP for this creature */

	rdwr_s32b(&r->ac);					/* Armour Class */

	rdwr_s32b(&r->sleep);				/* Inactive counter (base) */
	rdwr_s32b(&r->hearing);				/* Monster sense of hearing (1-100, standard 20) */
	rdwr_s32b(&r->smell);				/* Monster sense of smell (0-50, standard 20) */
	rdwr_s32b(&r->speed);				/* Speed (normally 110) */
	rdwr_s32b(&r->light);				/* Light intensity */

	rdwr_s32b(&r->mexp);				/* Exp value for kill */

	rdwr_s32b(&r->freq_innate);			/* Innate spell frequency */
	rdwr_s32b(&r->freq_spell);			/* Other spell frequency */
	rdwr_s32b(&r->spell_power);			/* Power of spells */

	rdwr_bitflag(r->flags, RF_SIZE);         		/* Flags */
	rdwr_bitflag(r->spell_flags, RSF_SIZE);  		/* Spell flags */
	rdwr_bitflag(r->death_spell_flags, RSF_SIZE);	/* Spell flags (to use on death) */

	rdwr_byte(&r->mut_chance);						/* Chance of mutation */

	RDWR_APTR(&r->blow, monster_blow, z_info->mon_blows_max); 				/* Melee blows */
	RDWR_APTR(&r->passive, monster_blow, z_info->mon_passive_max); 			/* Melee passive blows */

	rdwr_s32b(&r->level);				/* Level of creature */
	rdwr_s32b(&r->rarity);				/* Rarity of creature */

	rdwr_byte(&r->d_attr);				/* Default monster attribute */
	RDWR_AS(&r->d_char, uint32_t);				/* Default monster character */

	rdwr_byte(&r->max_num);				/* Maximum population allowed per level */
	rdwr_s32b(&r->cur_num);				/* Monster population on current level */

	RDWR_LPTR(&r->drops, monster_drop);
    RDWR_LPTR(&r->friends, monster_friends);
    RDWR_LPTR(&r->friends_base, monster_friends_base);
	RDWR_LPTR(&r->mimic_kinds, monster_mimic);

	rdwr_s32b(&r->num_shapes);
	RDWR_APTR(&r->shapes, monster_shape, r->num_shapes);

	return 0;
}

/**
 * Read monsters
 */
static int rd_monsters_aux(struct chunk *c, bool arena)
{
	int i;
	uint16_t limit;

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	/* Read the monster count */
	rd_u16b(&limit);
	if (limit > z_info->level_monster_max) {
		note(format("Too many (%d) monster entries!", limit));
		return (-1);
	}

	/* Read custom monster(s) */
	if (rdwr_race(&r_info[1]))
		return (-1);

	/* Clear for arenas */
	if (arena && player->upkeep->arena_level) {
		health_untrack_all(player->upkeep);
	}

	/* Read the monsters */
	for (i = 1; i < limit; i++) {
		struct monster *mon;
		struct monster monster_body;

		/* Get local monster */
		mon = &monster_body;
		memset(mon, 0, sizeof(*mon));

		/* Read the monster */
		if (!rd_monster(c, mon)) {
			note(format("Cannot read monster %d", i));
			return (-1);
		}

		/* Place monster in dungeon */
		if (place_monster(c, mon->grid, mon, 0) != i) {
			note(format("Cannot place monster %d", i));
			return (-1);
		}

		/* Handle arenas */
		if (arena && player->upkeep->arena_level) {
			assert(square_monster(c, mon->grid));
			health_track_add(player->upkeep, square_monster(c, mon->grid));
		}
	}

	return 0;
}

static int rd_traps_aux(struct chunk *c)
{
	struct loc grid;
	struct trap *trap;

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	rd_byte(&trf_size);

	/* Read traps until one has no location */
	while (true) {
		trap = mem_zalloc(sizeof(*trap));
		rd_trap(trap);
		grid = trap->grid;
		if (loc_is_zero(grid))
			break;
		else {
			/* Put the trap at the front of the grid trap list */
			trap->next = square_trap(c, grid);
			square_set_trap(c, grid, trap);

			/* Set decoy if appropriate */
			if ((trap->kind == lookup_trap("decoy")) &&
			    (c == cave)) {
				c->decoy = grid;
			}
		}
	}

	mem_free(trap);
	return 0;
}

int rd_dungeon(void)
{
	uint16_t depth;
	uint16_t py, px;

	/* Header info */
	rd_u16b(&depth);
	rd_u16b(&daycount);
	rd_u16b(&py);
	rd_u16b(&px);
	rd_s32b(&player->grid_last_1.y);
	rd_s32b(&player->grid_last_1.x);
	rd_s32b(&player->grid_last_2.y);
	rd_s32b(&player->grid_last_2.x);
	rd_u16b(&player->momentum);
	rd_byte(&square_size);

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	/* Ignore illegal dungeons */
	if (depth >= z_info->max_depth) {
		note(format("Ignoring illegal dungeon depth (%d)", depth));
		return (0);
	}

	if (rd_dungeon_aux(&cave))
		return 1;

	/* Ignore illegal dungeons */
	if ((px >= cave->width) || (py >= cave->height)) {
		note(format("Ignoring illegal player location (%d,%d).", py, px));
		return (1);
	}

	/* Load player depth */
	player->depth = depth;
	cave->depth = depth;

	/* Place player in dungeon */
	player_place(cave, player, loc(px, py));

	/* The dungeon is ready */
	character_dungeon = true;

	/* Read known cave */
	if (rd_dungeon_aux(&player->cave)) {
		return 1;
	}
	player->cave->depth = depth;

	return 0;
}


/**
 * Read the objects - wrapper functions
 */
int rd_objects(void)
{
	if (rd_objects_aux(rd_item, cave))
		return -1;
	if (rd_objects_aux(rd_item, player->cave))
		return -1;

	return 0;
}

/**
 * Read the monster list - wrapper functions
 */
int rd_monsters(void)
{
	int i;

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	if (rd_monsters_aux(cave, true))
		return -1;
	if (rd_monsters_aux(player->cave, false))
		return -1;

#if OBJ_RECOVER
	player->cave->objects = mem_zalloc((cave->obj_max + 1) * sizeof(struct object*));
	player->cave->obj_max = cave->obj_max;
	for (i = 0; i <= cave->obj_max; i++) {
		struct object *obj = cave->objects[i], *known_obj;
		if (!obj) continue;
		known_obj = object_new();
		obj->known = known_obj;
		object_copy(known_obj, obj);
		player->cave->objects[i] = known_obj;
	}
#else
	/* Associate known objects */
	for (i = 0; i < player->cave->obj_max; i++)
		if (cave->objects[i] && player->cave->objects[i])
			cave->objects[i]->known = player->cave->objects[i];
#endif
	return 0;
}

/**
 * Read the traps - wrapper functions
 */
int rd_traps(void)
{
	if (rd_traps_aux(cave))
		return -1;
	if (rd_traps_aux(player->cave))
		return -1;
	return 0;
}

/**
 * Read the chunk list
 */
int rd_chunks(void)
{
	int j;
	uint16_t chunk_max;

	if (player->is_dead)
		return 0;

	rd_u16b(&chunk_max);
	for (j = 0; j < chunk_max; j++) {
		struct chunk *c;

		/* Read the dungeon */
		if (rd_dungeon_aux(&c))
			return -1;

		/* Read the objects */
		if (rd_objects_aux(rd_item, c))
			return -1;

		/* Read the monsters */
		if (rd_monsters_aux(c, false))
			return -1;

		/* Read traps */
		if (rd_traps_aux(c))
			return -1;

		/* Read other chunk info */
		if (OPT(player, birth_levels_persist)) {
			char buf[80];
			int i;
			uint8_t tmp8u;
			uint16_t tmp16u;

			rd_string(buf, sizeof(buf));
			string_free(c->name);
			c->name = string_make(buf);
			rd_s32b(&c->turn);
			rd_u16b(&tmp16u);
			c->depth = tmp16u;
			rd_byte(&c->feeling);
			rd_u32b(&c->obj_rating);
			rd_u32b(&c->mon_rating);
			rd_byte(&tmp8u);
			c->good_item  = tmp8u ? true : false;
			rd_u16b(&tmp16u);
			c->height = tmp16u;
			rd_u16b(&tmp16u);
			c->width = tmp16u;
			rd_u16b(&c->feeling_squares);
			for (i = 0; i < z_info->f_max + 1; i++) {
				rd_u16b(&tmp16u);
				c->feat_count[i] = tmp16u;
			}
		} else if (c->name) {
			struct level *lev = level_by_name(c->name);

			if (lev) {
				c->depth = lev->depth;
			} else if (suffix(c->name, " known")) {
				size_t offset = strlen(c->name) -
					strlen(" known");
				c->name[offset] = '\0';
				lev = level_by_name(c->name);
				if (lev) {
					c->depth = lev->depth;
				}
				c->name[offset] = ' ';
			}
		}

		chunk_list_add(c);
	}

#if OBJ_RECOVER
	for (j = 0; j < chunk_max; j++) {
		if (j == 0 && streq(chunk_list[j].name, "Town")) continue;
		chunk_list[j] = 0;
	}
	if (streq(chunk_list[0].name, "Town")) {
		chunk_list_max = 1;
	} else {
		chunk_list_max = 0;
	}
#endif

	return 0;
}


int rd_history(void)
{
	uint32_t tmp32u;
	size_t i, j;
	
	history_clear(player);

	/* History type flags */
	rd_byte(&hist_size);
	if (hist_size > HIST_SIZE) {
	        note(format("Too many (%u) history types!", hist_size));
		return (-1);
	}

	rd_u32b(&tmp32u);
	for (i = 0; i < tmp32u; i++) {
		int32_t turnno;
		int16_t dlev, clev;
		bitflag type[HIST_SIZE];
		const struct artifact *art = NULL;
		int aidx = 0;
		char name[80];
		char text[80];

		for (j = 0; j < hist_size; j++)		
			rd_byte(&type[j]);
		rd_s32b(&turnno);
		rd_s16b(&dlev);
		rd_s16b(&clev);
		rd_string(name, sizeof(name));
		if (name[0]) {
			art = lookup_artifact_name(name);
			if (art) {
				aidx = art->aidx;
			}
		}
		rd_string(text, sizeof(text));
		if (name[0] && !art) {
			note(format("Couldn't find artifact %s!", name));
			continue;
		}

		history_add_full(player, type, aidx, dlev, clev, turnno, text);
	}

	return 0;
}

/**
 * For blocks that don't need loading anymore.
 */
int rd_null(void) {
	return 0;
}
