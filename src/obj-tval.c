/**
 * \file obj-tval.c
 * \brief Wrapper functions for tvals.
 *
 * Copyright (c) 2014 Ben Semmler
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
#include "obj-tval.h"
#include "z-type.h"
#include "z-util.h"

bool tval_is_legs(const struct object *obj)
{
	return obj->tval == TV_LEGS;
}

bool tval_is_arms(const struct object *obj)
{
	return obj->tval == TV_ARMS;
}

bool tval_is_chip(const struct object *obj)
{
	return obj->tval == TV_CHIP;
}

bool tval_is_implant(const struct object *obj)
{
	return ((tval_is_arms(obj)) || (tval_is_legs(obj)) || (tval_is_chip(obj)));
}

int tval_random_implant(void)
{
	const int tv[3] = { TV_LEGS, TV_ARMS, TV_CHIP };
	return tv[randint0(3)];
}

bool tval_is_device(const struct object *obj)
{
	return obj->tval == TV_DEVICE;
}

bool tval_is_wand(const struct object *obj)
{
	return obj->tval == TV_WAND;
}

bool tval_is_battery(const struct object *obj)
{
	return obj->tval == TV_BATTERY;
}

bool tval_is_rod(const struct object *obj)
{
	return obj->tval == TV_GADGET;
}

bool tval_is_pill(const struct object *obj)
{
	return obj->tval == TV_PILL;
}

bool tval_is_card(const struct object *obj)
{
	return obj->tval == TV_CARD;
}

bool tval_is_food(const struct object *obj)
{
	return obj->tval == TV_FOOD;
}

bool tval_is_food_k(const struct object_kind *kind)
{
	return kind->tval == TV_FOOD;
}

bool tval_is_mushroom(const struct object *obj)
{
	return obj->tval == TV_MUSHROOM;
}

bool tval_is_mushroom_k(const struct object_kind *kind)
{
	return kind->tval == TV_MUSHROOM;
}

bool tval_is_light(const struct object *obj)
{
	return obj->tval == TV_LIGHT;
}

bool tval_is_blunt(const struct object *obj)
{
	return obj->tval == TV_HAFTED;
}

bool tval_is_light_k(const struct object_kind *kind)
{
	return kind->tval == TV_LIGHT;
}

bool tval_is_chest(const struct object *obj)
{
	return obj->tval == TV_CHEST;
}

bool tval_is_fuel(const struct object *obj)
{
	return obj->tval == TV_BATTERY;
}

bool tval_is_money(const struct object *obj)
{
	return obj->tval == TV_GOLD;
}

bool tval_is_money_k(const struct object_kind *kind)
{
	return kind->tval == TV_GOLD;
}

bool tval_is_digger(const struct object *obj)
{
	return obj->tval == TV_DIGGING;
}

bool tval_can_have_nourishment(const struct object *obj)
{
	return obj->tval == TV_FOOD || obj->tval == TV_PILL ||
			obj->tval == TV_MUSHROOM;
}

bool tval_can_have_charges(const struct object *obj)
{
	return obj->tval == TV_DEVICE || obj->tval == TV_WAND;
}

bool tval_can_have_timeout(const struct object *obj)
{
	return obj->tval == TV_GADGET || obj->tval == TV_BATTERY || obj->tval == TV_LIGHT;
}

bool tval_is_body_armor(const struct object *obj)
{
	switch (obj->tval) {
		case TV_SOFT_ARMOR:
		case TV_HARD_ARMOR:
			return true;
		default:
			return false;
	}
}

bool tval_is_head_armor(const struct object *obj)
{
	return (obj->tval == TV_HELM);
}

static bool tv_is_ammo(int tval)
{
	switch (tval) {
		case TV_AMMO_6:
		case TV_AMMO_9:
		case TV_AMMO_12:
			return true;
		default:
			return false;
	}
}

bool tval_is_ammo(const struct object *obj)
{
	return tv_is_ammo(obj->tval);
}

bool kind_tval_is_ammo(const struct object_kind *kind)
{
	return tv_is_ammo(kind->tval);
}

bool tval_is_launcher(const struct object *obj)
{
	return obj->tval == TV_GUN;
}

bool tval_is_printer(const struct object *obj)
{
	return obj->tval == TV_PRINTER;
}

bool tval_is_useable(const struct object *obj)
{
	switch (obj->tval) {
		case TV_GADGET:
		case TV_WAND:
		case TV_DEVICE:
		case TV_CARD:
		case TV_PILL:
		case TV_FOOD:
		case TV_MUSHROOM:
		case TV_PRINTER:
			return true;
		default:
			return false;
	}
}

bool tval_can_have_failure(const struct object *obj)
{
	switch (obj->tval) {
		case TV_DEVICE:
		case TV_WAND:
		case TV_GADGET:
			return true;
		default:
			return false;
	}
}

static bool tv_is_weapon(int tv)
{
	switch (tv) {
		case TV_SWORD:
		case TV_HAFTED:
		case TV_POLEARM:
		case TV_DIGGING:
		case TV_GUN:
		case TV_AMMO_12:
		case TV_AMMO_9:
		case TV_AMMO_6:
			return true;
		default:
			return false;
	}
}

bool tval_is_weapon(const struct object *obj)
{
	return tv_is_weapon(obj->tval);
}

bool kind_tval_is_weapon(const struct object_kind *kind)
{
	return tv_is_weapon(kind->tval);
}

static bool tv_is_armor(int tv)
{
	switch (tv) {
		case TV_HARD_ARMOR:
		case TV_SOFT_ARMOR:
		case TV_SHIELD:
		case TV_CLOAK:
		case TV_BELT:
		case TV_HELM:
		case TV_BOOTS:
		case TV_GLOVES:
			return true;
		default:
			return false;
	}
}

bool kind_tval_is_armor(const struct object_kind *kind)
{
	return tv_is_armor(kind->tval);
}

bool tval_is_armor(const struct object *obj)
{
	return tv_is_armor(obj->tval);
}

bool tval_is_melee_weapon(const struct object *obj)
{
	switch (obj->tval) {
		case TV_SWORD:
		case TV_HAFTED:
		case TV_POLEARM:
		case TV_DIGGING:
			return true;
		default:
			return false;
	}
}

bool tval_has_variable_power(const struct object *obj)
{
	if (tval_is_wearable(obj))
		return true;
	if (tval_is_ammo(obj))
		return true;
	if (tval_is_implant(obj))
		return true;
	return false;
}

bool tval_is_wearable(const struct object *obj)
{
	return kind_tval_is_wearable(obj->tval);
}

bool kind_tval_is_wearable(int tval)
{
	switch (tval) {
		case TV_GUN:
		case TV_DIGGING:
		case TV_HAFTED:
		case TV_POLEARM:
		case TV_SWORD:
		case TV_BOOTS:
		case TV_GLOVES:
		case TV_HELM:
		case TV_SHIELD:
		case TV_CLOAK:
		case TV_BELT:
		case TV_SOFT_ARMOR:
		case TV_HARD_ARMOR:
		case TV_LIGHT:
			return true;
		default:
			return false;
	}
}

bool tval_is_food_or_mushroom(const struct object *obj)
{
	switch (obj->tval) {
		case TV_FOOD:
		case TV_MUSHROOM:
			return true;
		default:
			return false;
	}
}

bool tval_is_edible(const struct object *obj)
{
	switch (obj->tval) {
		case TV_FOOD:
		case TV_MUSHROOM:
		case TV_PILL:
			return true;
		default:
			return false;
	}
}

bool tval_can_have_flavor_k(const struct object_kind *kind)
{
	switch (kind->tval) {
		case TV_DEVICE:
		case TV_WAND:
		case TV_GADGET:
		case TV_PILL:
		case TV_MUSHROOM:
		case TV_CARD:
			return true;
		case TV_LIGHT:
			return (kind->flavor);
		default:
			return false;
	}
}

bool tval_is_zapper(const struct object *obj)
{
	return obj->tval == TV_WAND || obj->tval == TV_DEVICE;
}

/**
 * List of { tval, name } pairs.
 */
static const grouper tval_names[] =
{
	#define TV(a, b, c) { TV_##a, b },
	#include "list-tvals.h"
	#undef TV
};

/**
 * Small hack to allow both spellings of armer
 */
static char *de_armour(const char *name)
{
	char newname[40];
	char *armour;

	my_strcpy(newname, name, sizeof(newname));
	armour = strstr(newname, "armour");
	if (armour)
		my_strcpy(armour + 4, "r", 2);

	return string_make(newname);
}

/**
 * Returns the numeric equivalent tval of the textual tval `name`.
 */
int tval_find_idx(const char *name)
{
	size_t i = 0;
	unsigned int r;
	char *mod_name;

	mod_name = de_armour(name);

	for (i = 0; i < N_ELEMENTS(tval_names); i++) {
		if (!my_stricmp(mod_name, tval_names[i].name)) {
			string_free(mod_name);
			return tval_names[i].tval;
		}
	}

	string_free(mod_name);

	if (sscanf(name, "%u", &r) == 1)
		return r;

	return -1;
}

/**
 * Returns the textual equivalent tval of the numeric tval `name`.
 */
const char *tval_find_name(int tval)
{
	size_t i = 0;

	for (i = 0; i < N_ELEMENTS(tval_names); i++)
	{
		if (tval == tval_names[i].tval)
			return tval_names[i].name;
	}

	return "unknown";
}

/**
 * Counts the svals (from object.txt) of a given non-null tval
 */
int tval_sval_count(const char *name)
{
	size_t i, num = 0;
	int tval = tval_find_idx(name);

	if (tval < 0) return 0;

	for (i = 0; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		if (!kind->tval) continue;
		if (kind->tval != tval) continue;
		num++;
	}

	return num;
}

/**
 * Lists up to max_size svals (from object.txt) of a given non-null tval
 * Assumes list has allocated space for at least max_size elements
 */
int tval_sval_list(const char *name, int *list, int max_size)
{
	size_t i;
	int num = 0;
	int tval = tval_find_idx(name);

	if (tval < 0) return 0;

	for (i = 0; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		if (!kind->tval) continue;
		if (kind->tval != tval) continue;
		if (num >= max_size) break;
		list[num++] = kind->sval;
	}

	return num;
}
