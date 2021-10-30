/**
 * \file savefile.h
 * \brief Savefile loading and saving main routines
 *
 * Copyright (c) 2009 Andi Sidwell <andi@takkaria.org>
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
#ifndef INCLUDED_SAVEFILE_H
#define INCLUDED_SAVEFILE_H

#define FINISHED_CODE 255
#define ITEM_VERSION	5
#define EGO_ART_KNOWN 0xffffffff

/**
 * ------------------------------------------------------------------------
 * Savefile API
 * ------------------------------------------------------------------------ */

/**
 * Global "we've just saved" variable
 */
extern bool character_saved;

/**
 * Save to the given location.  Returns true on success, false otherwise.
 */
bool savefile_save(const char *path);

/**
 * Load the savefile given.  Returns true on succcess, false otherwise.
 */
bool savefile_load(const char *path, bool cheat_death);

/**
 * Try to get a description for this savefile.
 */
const char *savefile_get_description(const char *path);

extern bool saving;

/**
 * ------------------------------------------------------------------------
 * Detailed saving and loading functions
 * ------------------------------------------------------------------------ */


/* Utility */
void note(const char *msg);

/* Read/Writing bits */
void rdwr_bool(bool *v);
void rdwr_byte(byte *v);
void rdwr_u16b(u16b *v);
void rdwr_s16b(s16b *v);
void rdwr_u32b(u32b *v);
void rdwr_s32b(s32b *v);
void rdwr_double(double *v);
void rdwr_string(char **str);
void rdwr_string_null(char **str);
int rdwr_bitflag(bitflag *flags, int size);

/* Read/Write pointer. Give a pointer to pointer, and the base of the array (it converts it to an offset to this base) */
#define RDWR_PTR(P, B) if (saving) { if (*(P) == NULL) { wr_s32b(-1); } else { wr_s32b((*(P)) - ((B))); }  } else { s32b offset; rd_s32b(&offset); if (offset < 0) { *(P) = NULL; } else { *(P) = ((B)) + offset; } }

/* Read/Write pointer which is present in a linked list (next) by converting it to an index - may not be null or not in the list */
#define RDWR_NPTR(P, B) { s32b index = -1; assert(B); assert(*P); \
							if (saving) { \
								while (B) { \
									index++; \
									if (*(P) == B) \
										break; \
									B = B->next; \
								}; \
								assert(index >= 0); \
								wr_s32b(index); \
							} else { \
								rd_s32b(&index); assert(index >= 0); \
								while (index) { \
									index--; \
									B = B->next; \
								}; \
								*(P) = B; \
							} \
						}

/* Read/Write and allocate pointer to array length N of type T, without base. Calls rdwr_T on each element. */
#define RDWR_APTR(P, T, N) { int num = (N); if (!saving) { *(P) = mem_zalloc(sizeof(struct T) * num); } for(int i=0; i<num; i++) { rdwr_##T(&((*P)[i])); } }

/* Read/Write and allocate a list of unknown length with next pointers of type T. */
#define RDWR_LPTR(P, T) { int length = 0; struct T **base = (P); \
							if (saving) { struct T *elem = *base; while (elem) { length++; elem = elem->next; }; } \
							rdwr_s32b(&length); \
							if (saving) { \
								struct T *elem = *base; \
								for (int i=0; i<length; i++) { \
									rdwr_##T(elem); \
									elem = elem->next; \
								} \
							} \
							else { \
								if (!length) { *base = NULL; } \
								else { \
									struct T *elem = mem_zalloc(sizeof(struct T)); \
									*base = elem; \
									for (int i=0; i<length; i++) { \
										rdwr_##T(elem); \
										if (i < length-1) { \
											elem->next = mem_zalloc(sizeof(struct T)); \
											elem = elem->next; \
										} else { \
											elem->next = NULL; \
										} \
									} \
								} \
							} \
						}
/* Type cast - mainly for enums */
#define RDWR_AS(P, T) { T tmp; if (saving) { tmp = *(P); } rdwr_##T(&tmp); if (!saving) { *(P) = tmp; } }

/* Writing bits */
void wr_bool(bool v);
void wr_byte(byte v);
void wr_u16b(u16b v);
void wr_s16b(s16b v);
void wr_u32b(u32b v);
void wr_s32b(s32b v);
void wr_double(double v);
void wr_string(const char *str);
void pad_bytes(int n);

/* Reading bits */
void rd_bool(bool *ip);
void rd_byte(byte *ip);
void rd_u16b(u16b *ip);
void rd_s16b(s16b *ip);
void rd_u32b(u32b *ip);
void rd_s32b(s32b *ip);
void rd_double(double *ip);
void rd_string(char *str, int max);
void strip_bytes(int n);



/* load.c */
int rd_randomizer(void);
int rd_options(void);
int rd_messages(void);
int rd_monster_memory(void);
int rd_object_memory(void);
int rd_quests(void);
int rd_artifacts(void);
int rd_player(void);
void rdwr_player_levels(void);
int rdwr_race(struct monster_race *r);
int rd_world(void);
int rd_ignore(void);
int rd_misc(void);
int rd_player_hp(void);
int rd_player_spells(void);
int rd_gear(void);
int rd_stores(void);
int rd_dungeon(void);
int rd_chunks(void);
int rd_objects(void);
int rd_monsters(void);
int rd_monster_groups(void);
int rd_history(void);
int rd_traps(void);
int rd_null(void);

/* save.c */
struct spell_state;

void wr_description(void);
void wr_randomizer(void);
void wr_options(void);
void wr_messages(void);
void wr_monster_memory(void);
void wr_object_memory(void);
void rdwr_quests(void);
void wr_quests(void);
void rdwr_world(void);
void wr_world(void);
void wr_artifacts(void);
void wr_player(void);
void wr_ignore(void);
void wr_misc(void);
void rdwr_spell_state(struct spell_state *spell);
void rdwr_player(void);
void wr_player_hp(void);
void wr_player_spells(void);
void wr_randarts(void);
void wr_gear(void);
void wr_stores(void);
void wr_dungeon(void);
void wr_chunks(void);
void wr_objects(void);
void wr_monsters(void);
void wr_monster_groups(void);
void wr_ghost(void);
void wr_history(void);
void wr_traps(void);


#endif /* INCLUDED_SAVEFILE_H */
