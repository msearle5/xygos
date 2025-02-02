/**
 * \file obj-util.h
 * \brief Object utilities
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

#ifndef OBJECT_UTIL_H
#define OBJECT_UTIL_H

#include "obj-ignore.h"

/* Perfect resistance, given by this res_level */
#define IMMUNITY	127

/* An item's pval (for charges, amount of cash, etc) is limited to s32b,
 * and is half that range to make it easier to avoid overflow.
 **/
#define MAX_PVAL  1073741823

void flavor_init(void);
void flavor_set_all_aware(void);
bool obj_cyber_can_takeoff(const struct object *obj);
void strip_punct(char *in);
void object_flags(const struct object *obj, bitflag flags[OF_SIZE]);
void object_flags_known(const struct object *obj, bitflag flags[OF_SIZE]);
void object_carried_flags(const struct object *obj, bitflag flags[OF_SIZE]);
void object_carried_flags_known(const struct object *obj, bitflag flags[OF_SIZE]);
bool object_test(item_tester tester, const struct object *o);
bool item_test(item_tester tester, int item);
bool is_unknown(const struct object *obj);
unsigned check_for_inscrip(const struct object *obj, const char *inscrip);
unsigned check_for_inscrip_with_int(const struct object *obj, const char *insrip, int *ival);
struct object_kind *lookup_kind(int tval, int sval);
struct object_kind *objkind_byid(int kidx);
struct object *wish(const char *in, int level, bool limited);
bool make_wish(const char *prompt, int level, bool limited);
const struct ego_item *lookup_ego_name_fuzzy(const char *name, const struct object_kind *kind, const struct ego_item **ego, int *fuzz);
const struct ego_item *lookup_ego_name(const char *name);
const struct object_kind *lookup_kind_name_fuzzy(const char *name, int *fuzz);
const struct object_kind *lookup_kind_name(const char *name);
const struct artifact *lookup_artifact_name_fuzzy(const char *name, int *fuzz);
const struct artifact *lookup_artifact_name(const char *name);
struct ego_item *lookup_ego_item(const char *name, int tval, int sval);
int lookup_sval(int tval, const char *name);
int lookup_sval_ego(int tval, const char *name, const struct ego_item **ego);
void object_short_name(char *buf, size_t max, const char *name);
int compare_items(const struct object *o1, const struct object *o2);
bool obj_has_charges(const struct object *obj);
bool obj_can_zap(const struct object *obj);
bool obj_is_activatable(const struct object *obj);
bool obj_can_activate(const struct object *obj);
bool obj_can_refill(const struct object *obj);
bool obj_can_takeoff(const struct object *obj);
bool obj_can_wear(const struct object *obj);
bool obj_can_fire(const struct object *obj);
bool obj_is_throwing(const struct object *obj);
const struct object_material *obj_material(const struct object *obj);
bool obj_is_metal(const struct object *obj);
bool obj_is_known_artifact(const struct object *obj);
bool obj_has_inscrip(const struct object *obj);
bool obj_has_flag(const struct object *obj, int flag);
bool obj_is_useable(const struct object *obj);
struct effect *object_effect(const struct object *obj);
bool obj_needs_aim(struct object *obj);
bool obj_can_fail(const struct object *o);
bool obj_has_ego(const struct object *obj, const char *name);
int get_use_device_chance(const struct object *obj);
bool obj_is_pack_activatable(const struct object *obj);
void distribute_charges(struct object *source, struct object *dest, int amt);
int number_charging(const struct object *obj);
bool recharge_timeout(struct object *obj);
bool verify_object(const char *prompt, const struct object *obj,
		const struct player *p);
char *format_custom_message(const struct object *obj, const char *string, char *buf, int len,
		const struct player *p);
void print_custom_message(struct object *obj, const char *string, int msg_type,
		const struct player *p);
bool is_artifact_created(const struct artifact *art);
bool is_artifact_seen(const struct artifact *art);
bool is_artifact_everseen(const struct artifact *art);
void mark_artifact_created(const struct artifact *art, bool created);
void mark_artifact_seen(const struct artifact *art, bool seen);
void mark_artifact_everseen(const struct artifact *art, bool seen);

#endif /* OBJECT_UTIL_H */
