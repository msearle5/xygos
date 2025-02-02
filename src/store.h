/**
 * \file store.h
 * \brief Store stocking
 *
 * Copyright (c) 1997 Robert A. Koeneke, James E. Wilson, Ben Harrison
 * Copyright (c) 2007 Andi Sidwell
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

#ifndef INCLUDED_STORE_H
#define INCLUDED_STORE_H

#include "cave.h"
#include "cmd-core.h"
#include "datafile.h"
#include "object.h"

/**
 * List of store indices
 */
enum {
	STORE_NONE      = -1,
	STORE_GENERAL	= 0,
	STORE_ARMOR		= 1,
	STORE_WEAPON	= 2,
	STORE_TEMPLE	= 3,
	STORE_ALCHEMY	= 4,
	STORE_MAGIC		= 5,
	STORE_B_MARKET	= 6,
	STORE_HOME		= 7,
	STORE_HQ		= 8,
	STORE_AIR		= 9,
	STORE_CYBER		= 10,
	MAX_STORES		= 11
};

struct object_buy {
	struct object_buy *next;
	size_t tval;
	size_t sval;
	size_t flag;
};

struct owner {
	struct owner *next;
	char *name;
	unsigned int oidx;
	int32_t max_cost;
	int32_t greed;
	bool male;
};

struct store_entry {
	struct object_kind *kind;
	random_value rarity;
	int tval;
};

struct store {
	struct store *next;
	struct owner *owners;
	struct owner *owner;
	unsigned int sidx;
	const char *name;

	unsigned int bandays;
	const char *banreason;
	int32_t layaway_idx;
	int32_t layaway_day;
	int32_t income;
	int32_t max_danger;			/* Mark for destruction when danger hits this level */
	int32_t low_danger;
	int32_t high_danger;
	bool destroy;				/* Destroy when next entering the town */
	bool open;					/* Is currently open (has an entrance). Destroyed stores must be closed (unless you want an entrance in the ruin!) */
	uint8_t quest_status;
	uint16_t x;						/* Position in the level, this should be valid even if closed or destroyed */
	uint16_t y;

	uint16_t stock_num;				/* Stock -- Number of entries */
	int16_t stock_size;			/* Stock -- Total Size of Array */

	struct object *stock;		/* Stock -- Actual stock items */
	struct object *stock_k;		/* Stock -- Stock as known by the character */

	/* Always stock these items */
	size_t always_size;
	size_t always_num;
	struct store_entry *always_table;

	/* Select a number of these items to stock */
	size_t normal_size;
	size_t normal_num;
	struct store_entry *normal_table;

	/* Buy these items */
	struct object_buy *buy;

	int turnover;
	int normal_stock_min;
	int normal_stock_max;
};

extern struct store *stores;
extern struct store *stores_init;

/**
 * The first name arrays
 */
extern struct hint *firstnames;
extern struct hint *firstnames_male;
extern struct hint *firstnames_female;

/**
 * The second name array
 */
extern struct hint *secondnames;

/**
 * Responses
 */
extern const char **comment_worthless;
extern int n_comment_worthless;

extern const char **comment_bad;
extern int n_comment_bad;

extern const char **comment_accept;
extern int n_comment_accept;

extern const char **comment_good;
extern int n_comment_good;

extern const char **comment_great;
extern int n_comment_great;

struct store *get_store_by_idx(int idx);
struct store *get_store_by_name(const char *name);
bool you_own(struct store *store);
struct store *store_at(struct chunk *c, struct loc grid);
void store_init(void);
void store_maint(struct store *s);
void free_stores(void);
void store_stock_list(struct store *store, struct object **list, int n);
void home_carry(struct store *store, struct object *obj);
struct object *store_carry(struct store *store, struct object *obj, bool maintain);
void store_reset(void);
void store_shuffle(struct store *store);
void store_update(void);
void store_delete(struct store *s, struct object *obj, int amt);
int price_item(struct store *store, const struct object *obj,
			   bool store_buying, int qty);
bool store_will_buy_tester(const struct object *obj);
bool store_check_num(struct store *store, const struct object *obj);
int find_inven(const struct object *obj);
void stores_copy(struct store *src);
int store_faction(struct store *store);
int store_cyber_rank(void);
int store_cyber_install_price(struct object *obj);
int store_roundup(int);

extern struct owner *store_ownerbyidx(struct store *s, unsigned int idx);

struct parser *init_parse_stores(void);
extern struct parser *store_parser_new(void);
extern struct parser *store_owner_parser_new(struct store *stores);

extern void do_cmd_sell(struct command *cmd);
extern void do_cmd_stash(struct command *cmd);
extern void do_cmd_install(struct command *cmd);
extern void do_cmd_buy(struct command *cmd);
extern void do_cmd_retrieve(struct command *cmd);

#endif /* INCLUDED_STORE_H */
