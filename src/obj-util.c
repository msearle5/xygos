/**
 * \file obj-util.c
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

#include "angband.h"
#include "cave.h"
#include "cmd-core.h"
#include "effects.h"
#include "game-input.h"
#include "game-world.h"
#include "generate.h"
#include "grafmode.h"
#include "init.h"
#include "mon-make.h"
#include "monster.h"
#include "obj-fault.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-init.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-spell.h"
#include "player-util.h"
#include "randname.h"
#include "z-queue.h"

struct object_base *kb_info;
struct object_kind *k_info;
struct artifact *a_info;
struct artifact_upkeep *aup_info;
struct ego_item *e_info;
struct flavor *flavors;

/**
 * Hold the titles of cards, up to 31 characters each.
 * Longest "Simple" prefix is 6
 * Longest "Creator" suffix is 7
 * Longest "-" infix is 1
 * Longest " XX 1.2.3" version is 9
 */
static char **card_adj;
static int n_card_adj;

/**
 * Hold the titles of pills, 6 to 28 characters each, plus quotes.
 */
static char **pill_adj;
static int n_pill_adj;

static void flavor_assign_fixed(void)
{
	int i;
	struct flavor *f;

	for (f = flavors; f; f = f->next) {
		if (f->sval == SV_UNKNOWN)
			continue;

		for (i = 0; i < z_info->k_max; i++) {
			struct object_kind *k = &k_info[i];
			if (k->tval == f->tval && k->sval == f->sval)
				k->flavor = f;
		}
	}
}

/* Wish parser debug prints */
//#define WISH_DPF(...) fprintf(stderr,__VA_ARGS__)
#define WISH_DPF(...) { ; }

#define WISH_WORDS 256

/* Remove nonalphanumeric characters, convert to lower case, in place */
void strip_punct(char *in)
{
	char *out = in;
	char c;
	while ((c = *in++)) {
		c = tolower(c);
		if (isalnum(c))
			*out++ = c;
	}
	*out= 0;
}

/**
 * Make a wish.
 *
 * Takes a string resembling an item description and builds an item to fit,
 * given certain restrictions (which vary depending on whether this is used
 * as a debug option, generic item creation routine or item activation effect.
 *
 * This has the grammar:
 * {count} [{prefixes}] {base} [{suffixes}] [{dice}] [{melee}] {armor}] [{pval}]
 * 
 * count:	[<number> | a | an | the]
 * prefix:	Any artifact name, or set of ego names, with a leading <
 * base:	Any object name (or something kinda like)
 * suffix:	"of " + any artifact name, or
 * 			' <artifact name> ', or
 * 			( <any set of ego names> )
 * dice:	(<x>d<y> or x<n>)
 * melee:	(<+hit>,<+dam>)
 * armor:	[<base-ac>,<to-ac>]
 * pval:	< <+pval>,+ >
 * 
 * However, not all of this is useful information - and for a user given string it
 * might contain different bracketing or ordering. So this wish parser ignores
 * ordering where possible.
 * 
 * What needs to be extracted is:
 * Count (although this may get reduced later)
 * 		from any entry which is a number, otherwise 1
 * Artifact or Egos (independent of position)
 * Base item
 * 
 * This is done with fuzzy matching to names - it should make a good guess at
 * something that is nearly right.
 **/
struct object *wish(const char *in, int level)
{
	char buf[256];
	strncpy(buf, in, sizeof(buf));
	buf[sizeof(buf)-1] = 0;

	int count = 1;

	const struct artifact *art = NULL;
	const struct ego_item *ego[MAX_EGOS] = { NULL };
	const struct object_kind *kind = NULL;

	const char *name[WISH_WORDS] = { NULL };
	int fuzz[WISH_WORDS] = { 0 };
	const struct object_kind *kinds[WISH_WORDS] = { NULL };
	int names = 0;
	bool valid = false;

	const char *word[WISH_WORDS] = { NULL };
	int words = 0;

	/* Pass over all space separated fields */
	char *tok = strtok(buf, " ");
	while (tok) {
		char *end;
		errno = 0;
		long num = strtol(tok, &end, 10);

		/* If it's a number *only*, set the count */
		if ((num > 1) && (num <= 99) && (errno == 0) && (*end == 0))
			count = num;
		else {
			/* Strip out anything that's not alphanumeric or -,*,internal ' */
			char *c = tok;
			while (*c) {
				if ((!isalnum(*c)) && (*c != '-') && (*c != '*')) {
					if ((*c == '\'') && (c != tok) && (*(c+1))) {
					} else {
						*c = 0;
						if (c == tok) {
							tok++;
						}
					}
				}
				c++;
			}

			/* Ignore anything entirely nonalpha, or only one alpha character
			 * This includes (+0,+0), [0,+1], <+4> (0-alpha) and 3d4, x4 (1-alpha) */
			if ((*tok) && (*(tok+1))) {
				/* Remove leading "of " */
				if (!strncmp(tok, "of ", 3)) {
					tok += 3;
				}
				if ((*tok) && (words < WISH_WORDS)) {
					valid = true;
					word[words++] = tok;
				}
			}
		}
		tok = strtok(NULL, " ");
	};

	/* If there's nothing that looks at all like an object (empty input, or
	 * just some non-alphabetic fragments) try treating the whole input as
	 * a single word.
	 */
	if (!valid) {
		WISH_DPF("Nothing recognizable - treating as a single word\n");
		strncpy(buf, in, sizeof(buf));
		buf[sizeof(buf)-1] = 0;
		word[0] = buf;
		words = 1;
	}

	/* Make pairs */
	int pairs = words;
	for(int i=0;i<words-1;i++) {
		char pair[256];
		strnfmt(pair, sizeof(pair), "%s %s", word[i], word[i+1]);
		word[pairs++] = string_make(pair);
		if (pairs == WISH_WORDS) {
			break;
		}
	}

	/* and threes */
	for(int i=0;i<words-2;i++) {
		char pair[256];
		strnfmt(pair, sizeof(pair), "%s %s %s", word[i], word[i+1], word[i+2]);
		word[pairs++] = string_make(pair);
		if (pairs == WISH_WORDS) {
			break;
		}
	}

	/* and fours */
	for(int i=0;i<words-3;i++) {
		char pair[256];
		strnfmt(pair, sizeof(pair), "%s %s %s %s", word[i], word[i+1], word[i+2], word[i+3]);
		word[pairs++] = string_make(pair);
		if (pairs == WISH_WORDS) {
			break;
		}
	}

	/* Search words and combinations */
	for(int i=0;i<pairs;i++) {
		/* Search for base item */
		int distance = strlen(word[i]);
		const struct object_kind *k = lookup_kind_name_fuzzy(word[i], &distance);
		if (k) {
			/* Even as a debug option, as it will otherwise produce the artifact instead of the base item
			 * for INSTA_ART, while SPECIAL_GEN items may fail to generate */
			if ((!kf_has(k->kind_flags, KF_INSTA_ART)) && (!kf_has(k->kind_flags, KF_SPECIAL_GEN))) {
				kinds[names] = k;
				fuzz[names] = distance;
			}
		}
		/* Defer search for artifacts, egos */
		name[names] = word[i];
		names++;
	}

	/* Extract a base item or artifact - the least fuzzy match */
	int best_fuzz = sizeof(buf);
	int best_idx = -1;
	for(int i=names-1;i>=0;i--) {
		int distance = sizeof(buf);
		const struct artifact *a = lookup_artifact_name_fuzzy(name[i], &distance);
		if (a) {
			WISH_DPF("From %s, found %s, dist %d\n", name[i], a->name, distance);
			if (distance < best_fuzz) {
				WISH_DPF("   -- using\n");
				best_fuzz = distance;
				best_idx = i;
				art = a;
			}
		}
		if ((kinds[i]) && (fuzz[i] < best_fuzz)) {
			WISH_DPF("From %s, using %s in place of %s, dist %d\n", name[i], name[best_idx], kinds[i]->name, fuzz[i]);
			best_fuzz = fuzz[i];
			kind = kinds[i];
			best_idx = i;
			art = NULL;
		}
	}

	int kind_fuzz = best_fuzz;
	/* Now that the base item is known (if possible), look for egos.
	 * Find single egos first - the name which has lowest distance.
	 * Only if there are two names which both have a lower distance to compatible egos should it fall back
	 * to a multiego.
	 **/
	int ego_fuzz = sizeof(buf);
	if (best_fuzz > 1) {
		for(int i=0; i<names; i++) {
			const char *n = name[i];
			if (n) {
				/* Best single ego */
				int distance = sizeof(buf);
				const struct ego_item *e = lookup_ego_name_fuzzy(n, NULL, NULL, &distance);
				if ((e) && (distance <= ego_fuzz) && ((!art) || (distance < best_fuzz))) {
					WISH_DPF("ph1: From %s, ego %s, dist %d - using\n", name[i], e->name, distance);
					ego[0] = e;
					ego_fuzz = distance;
					art = NULL;
				} else {
					WISH_DPF("ph1: From %s, ego %s, dist %d, egofuzz %d, best fuzz %d - NOT using\n", name[i], e ? e->name : "(NULL)", distance, ego_fuzz, best_fuzz);
				}
			} else {
				WISH_DPF("ph1: Skipping %d (%s)\n", i, n ? n : "none");
			}
		}
	}

	/* Don't use the name picked for the base item again for ego items.
	 * Avoid components of it (e.g. "combat" from "combat ready") also.
	 **/
	bool reject[WISH_WORDS];
	memset(reject, 0, WISH_WORDS);
	if (best_idx >= 0) {
		for(int i=0;i<best_idx;i++) {
			if (strstr(name[best_idx], name[i])) {
				WISH_DPF("Rejecting %s as a substring of %s\n", name[i], name[best_idx]);
				reject[i] = true;
			} else {
				WISH_DPF("Accepting %s as NOT a substring of %s\n", name[i], name[best_idx]);
			}
		}
		reject[best_idx] = true;
	}

	/* Now that the base item is known (if possible), look for egos.
	 * Find single egos first - the name which has lowest distance.
	 * Only if there are two names which both have a lower distance to compatible egos should it fall back
	 * to a multiego.
	 **/
	if (!ego[0]) {
		ego_fuzz = sizeof(buf);
		if ((!art) || (best_fuzz > 1)) {
			for(int i=0;i<names;i++) {
				const char *n = name[i];
				if (n) {
					/* Best single ego */
					int distance = sizeof(buf);
					if (reject[i]) {
						if (kind) {
							continue;
						}
						distance *= 2;
						distance += names;
						distance -= i;
					}
					const struct ego_item *e = lookup_ego_name_fuzzy(n, NULL, NULL, &distance);
					if ((e) && (distance <= ego_fuzz) && ((!art) || (distance < best_fuzz))) {

						WISH_DPF("ph2: From %s, ego %s, dist %d - using\n", name[i], e->name, distance);
						ego[0] = e;
						best_fuzz = ego_fuzz = distance;
						art = NULL;
					} else {
						WISH_DPF("ph2: From %s, ego %s, dist %d, egofuzz %d, best fuzz %d - NOT using\n", name[i], e ? e->name : "(NULL)", distance, ego_fuzz, best_fuzz);
					}
				}
			}
		}
	}

	/* Check whether the ego matches the kind.
	 * If not, see if there is another ego with the same name that does apply.
	 * If not, determine (using which is a less fuzzy match) whether to keep the
	 * ego or the base item.
	 * If no base item was given, or if the ego was a closer match, select a new
	 * base item.
	 */ 
	if ((kind) && (ego[0])) {
		const struct ego_item *e = ego[0];
		bool match = false;
		WISH_DPF("ph3: existing kind %s\n", kind->name);
		for (struct poss_item *poss = e->poss_items; poss; poss = poss->next) {
			if (poss->kidx == (unsigned)(kind-k_info)) {
				WISH_DPF("ph3: existing kind is a match\n");
				match = true;
				break;
			}
		}
		if (!match) {
			/* Is there an ego of the same name that applies? */
			char ename[256];
			obj_desc_name_format(ename, sizeof(ename), 0, e->name, NULL, false);
			strip_punct(ename);
			WISH_DPF("ph3: searching for duplicate ego names to %s\n", ename);
			for (int dupi = 0; dupi<z_info->e_max; dupi++) {
				struct ego_item *dup = e_info+dupi;
				if ((dup->name) && (dup != e)) {
					char dupname[256];
					obj_desc_name_format(dupname, sizeof(dupname), 0, dup->name, NULL, false);
					strip_punct(dupname);
					if (streq(dupname, ename)) {
						for (struct poss_item *poss = dup->poss_items; poss; poss = poss->next) {
							if (poss->kidx == (unsigned)(kind-k_info)) {
								WISH_DPF("ph3: duplicate ego name is a match\n");
								match = true;
								e = ego[0] = dup;
								break;
							}
						}
					}
				}
			}
		}
		if (!match) {
			if (ego_fuzz < kind_fuzz) {
				WISH_DPF("ph3: existing kind is NOT a match. Choosing ego (%d) over kind (%d)\n", ego_fuzz, kind_fuzz);
				kind = NULL;
			} else {
				WISH_DPF("ph3: existing kind is NOT a match. Choosing kind (%d) over ego (%d)\n", kind_fuzz, ego_fuzz);
				ego[0] = NULL;
			}
		}
	}

	if ((!kind) && (ego[0])) {
		kind = select_ego_kind(ego[0], level, 0);
	}

	/* Do we have an item?
	 * Artifact (whether other stuff is present or not) => try to make that. It may fail, though.
	 * 		If so, fall through as if it wasn't present.
	 * Base and ego(s): => make that item with that ego(s). That may not be possible.
	 * 		If so, behave as if the egos weren't present.
	 * Base item only => make a random item of that type.
	 * Ego(s) only => make a random item with that ego(s).
	 * Nothing: => make a random item.
	 * 
	 * Count is honored if the item is not an artifact.
	 */
	struct object *obj = NULL;
	if (art) {
		/* Generate the base item */
		kind = lookup_kind(art->tval, art->sval);
		WISH_DPF("Attempting to create artifact %s\n", art->name);
		if (!(is_artifact_created(art))) {
			obj = object_new();
			object_prep(obj, (struct object_kind *)kind, art->alloc_min, RANDOMISE);

			/* Mark the item as an artifact */
			obj->artifact = art;
			copy_artifact_data(obj, obj->artifact);
			mark_artifact_created(obj->artifact, true);
		} else {
			WISH_DPF("Attempt to create artifact failed\n");
		}
	}

	/* Artifact creation failed, or an artifact wasn't called for */
	if (!obj) {
		if (kind) {
			/* Base item was mentioned */
			char buf[256];
			obj_desc_name_format(buf, sizeof(buf), 0, kind->name, NULL, false);
			obj = make_object_named(cave, level, false, false, false, NULL, kind->tval, buf);
			WISH_DPF("Creating named object %s\n", buf);
		}
		/* Base item creation failed, or none was called for. */
		if (!obj) {
			/* Should not get here if there is an ego.
			 * Random good item, or random item if that fails, or random level 1 item if that fails.
			 **/
			kind = get_obj_num(level, true, 0);
			if (!kind)
				kind = get_obj_num(level, false, 0);
			if (!kind)
				kind = get_obj_num(1, false, 0);
			/* Create the object from the kind (or NULL = any kind) */
			obj = make_object_named(cave, level, false, false, false, NULL, 0, kind ? kind->name : NULL);
			WISH_DPF("Creating random object %s\n", kind ? kind->name : "without specifying kind");
		}
	}

	/* Make it an ego or multiego, set the count.
	 * Should check whether this is permissible (because of random items).
	 **/
	if (obj) {
		if (!obj->artifact) {
			for(int i=0;i<MAX_EGOS;i++) {
				if (ego[i]) {
					obj->ego[i] = (struct ego_item *)(ego[i]);
					ego_apply_magic_from(obj, 0, i);
					WISH_DPF("Applying ego: %s\n", ego[i]->name);
				}
			}

			if (obj->number < count)
				obj->number = count;
			if (obj->number > obj->kind->base->max_stack)
				obj->number = obj->kind->base->max_stack;
		}

		/* Make it known */
		obj->origin = ORIGIN_CHEAT;
		obj->origin_depth = player->depth;
		object_know_all(obj);
		player_know_object(player, obj);
		obj->kind->everseen = true;
		WISH_DPF("IDENTIFIED new object\n");
	}

	/* Clean up */
	for(int i=words; i<pairs; i++)
		string_free((char *)word[i]);

	/* Return the item.
	 * This may be NULL if nothing can be produced.
	 */
	WISH_DPF("Wish complete, object %s created\n", obj ? obj->kind->name : "*NOT*");
	return obj;
}

/* Ask for an object name.
 * Try to create it and add it to inventory.
 * Takes level, returns true if successful.
 */
bool make_wish(const char *prompt, int level)
{
	char buf[256] = { 0 };
	if (!get_string_hook(prompt, buf, sizeof(buf)))
		return false;
	struct object *obj = wish(buf, level);
	if (obj) {
		inven_carry(player, obj, true, true);
		return true;
	}
	return false;
}

static void flavor_assign_random(byte tval)
{
	int i;
	int flavor_count = 0;
	int choice;
	struct flavor *f;

	/* Count the random flavors for the given tval */
	for (f = flavors; f; f = f->next)
		if (f->tval == tval && f->sval == SV_UNKNOWN)
			flavor_count++;
	for (i = 0; i < z_info->k_max; i++) {
		if (k_info[i].tval != tval || k_info[i].flavor || (tval == TV_LIGHT && kf_has(k_info[i].kind_flags, KF_EASY_KNOW)))
			continue;

		if (!flavor_count) {
			for (f = flavors; f; f = f->next)
				if (f->tval == tval && f->sval == SV_UNKNOWN)
					flavor_count++;
			int need_count = 0;
			for (i = 0; i < z_info->k_max; i++) {
				if (k_info[i].tval != tval || k_info[i].flavor || (tval == TV_LIGHT && kf_has(k_info[i].kind_flags, KF_EASY_KNOW)))
					continue;
				need_count++;
			}
			quit_fmt("Not enough flavors for tval %d (%s), found %d, need %d.", tval, tval_find_name(tval), flavor_count, need_count);
		}

		choice = randint0(flavor_count);

		for (f = flavors; f; f = f->next) {
			if (f->tval != tval || f->sval != SV_UNKNOWN)
				continue;

			if (choice == 0) {
				k_info[i].flavor = f;
				f->sval = k_info[i].sval;
				if (tval == TV_PILL)
					f->text = pill_adj[k_info[i].sval - 1];
				if (tval == TV_CARD)
					f->text = card_adj[k_info[i].sval - 1];
				flavor_count--;
				break;
			}

			choice--;
		}
	}
}

/**
 * Reset svals on flavors, effectively removing any fixed flavors.
 *
 * Mainly useful for randarts so that fixed flavors for standards aren't
 * predictable.
 */
static void flavor_reset_fixed(void)
{
	struct flavor *f;

	for (f = flavors; f; f = f->next)
		f->sval = SV_UNKNOWN;
}

static void clean_strings(char ***array, int *length)
{
	for(int i=0;i<*length;i++)
		string_free((*array)[i]);
	mem_free(*array);
	*array = NULL;
	*length = 0;
}

static void insert_string(char *buf, int i, char ***array, int *length)
{
	if (i >= *length) {
		int old = *length;
		*length = i+1;
		*array = mem_realloc(*array, sizeof(*array) * *length);
		for(int j=old; j<*length; j++) {
			(*array)[j] = NULL;
		}
	}
	(*array)[i] = string_make(buf);
}

/**
 * Prepare the "variable" part of the "k_info" array.
 *
 * The "color"/"metal"/"type" of an item is its "flavor".
 * For the most part, flavors are assigned randomly each game.
 *
 * Initialize descriptions for the "colored" objects, including:
 * Devices, Wands, Gadgets, Mushrooms, Pills, Cards.
 *
 * Hack -- make sure everything stays the same for each saved game
 * This is accomplished by the use of a saved "random seed", as in
 * "town_gen()".  Since no other functions are called while the special
 * seed is in effect, so this function is pretty "safe".
 */
void flavor_init(void)
{
	int i;

	/* Hack -- Use the "simple" RNG */
	Rand_quick = true;

	/* Hack -- Induce consistant flavors */
	Rand_value = seed_flavor;

	/* Scrub all flavors and re-parse for new players */
	if (turn == 1) {
		struct flavor *f;

		for (i = 0; i < z_info->k_max; i++) {
			k_info[i].flavor = NULL;
		}
		for (f = flavors; f; f = f->next) {
			f->sval = SV_UNKNOWN;
			f->text = NULL;
		}
		cleanup_parser(&flavor_parser);
	}
	run_parser(&flavor_parser);

	if (OPT(player, birth_randarts))
		flavor_reset_fixed();

	clean_strings(&pill_adj, &n_pill_adj);
	clean_strings(&card_adj, &n_card_adj);

	flavor_assign_fixed();

	flavor_assign_random(TV_LIGHT);
	flavor_assign_random(TV_DEVICE);
	flavor_assign_random(TV_WAND);
	flavor_assign_random(TV_GADGET);
	flavor_assign_random(TV_MUSHROOM);

	/* Pills (random titles, always magenta)
	 * The pills will be randomized again by flavor_assign_random.
	 * So this doesn't have to change the base names ("yadar" of "yadarine"), only the suffix.
	 * It also doesn't matter (much) whether all suffixes are used or some used more than once.
	 * so:
	 * For each output name:
	 * 		Copy from a sequential input basename
	 * 		+ a randomly chosen input suffix.
	 */
	char **suff = NULL;
	int n_suff = 0;
	
	/* Pull out suffixes into an array */
	int pills = 0;
	for (struct flavor *f = flavors; f; f = f->next) {
		if (f->tval == TV_PILL && f->sval == SV_UNKNOWN) {
			char *suffix = strchr(f->text, '|');
			if (suffix) {
				suffix++;
				char buf[16];
				strncpy(buf, suffix, sizeof(buf));
				buf[sizeof(buf)-1] = 0;
				insert_string(buf, pills, &suff, &n_suff);
				pills++;
			}
		}
	}

	/* And combine them */
	i = 0;
	if (pill_adj) {
		mem_free(pill_adj);
		pill_adj = NULL;
	}
	n_pill_adj = 0;
	for (struct flavor *f = flavors; f; f = f->next) {
		if (f->tval == TV_PILL && f->sval == SV_UNKNOWN) {
			char base[11];
			strncpy(base, f->text, sizeof(base));
			base[sizeof(base)-1] = 0;
			char *suffix = strchr(base, '|');
			assert(suffix);
			*suffix = 0;
			char buf[64];
			snprintf(buf, sizeof(buf), "%s%s", base, suff[randint0(pills)]);
			buf[sizeof(buf)-1] = 0;
			insert_string(buf, i, &pill_adj, &n_pill_adj);
			i++;
		}
	}
	flavor_assign_random(TV_PILL);

	clean_strings(&suff, &n_suff);

	/* Cards (random titles, always blue) */
	int cards = 0;
	for (struct flavor *f = flavors; f; f = f->next) {
		if (f->tval == TV_CARD && f->sval == SV_UNKNOWN) {
			char *suffix = strchr(f->text, '|');
			assert(suffix);
			suffix++;
			char buf[16];
			strncpy(buf, suffix, sizeof(buf));
			buf[sizeof(buf)-1] = 0;
			insert_string(buf, cards, &suff, &n_suff);
			cards++;
		}
	}

	/* And combine them */
	i = 0;
	for (struct flavor *f = flavors; f; f = f->next) {
		if (f->tval == TV_CARD && f->sval == SV_UNKNOWN) {
			char base[11];
			char ext[11];
			strncpy(base, f->text, sizeof(base));
			base[sizeof(base)-1] = 0;
			char *suffix = strchr(base, '|');
			assert(suffix);
			*suffix = 0;
			strncpy(ext, suff[randint0(cards)], sizeof(ext));
			/* Determine capitalization */
			titlecase(base);
			titlecase(ext);
			/* Determine separator */
			char *sep = "";
			switch(randint0(20)) {
				case 0:
					sep = ".";
					break;
				case 1:
					sep = "/";
					break;
				case 2:
					sep = ":";
					break;
				case 3:
					sep = ".";
					break;
				case 4:
				case 5:
				case 6:
					sep = "-";
					break;
				case 7:
				case 8:
				case 9:
					sep = " ";
					break;
			}
			/* Determine suffix */
			char suff[8];
			strcpy(suff, "");
			unsigned rn1 = randint1(9);
			assert(rn1 < 10);
			unsigned rn2 = randint0(randint0(9));
			assert(rn2 < 10);
			unsigned rn3 = randint0(randint0(randint0(9)));
			assert(rn3 < 10);
			switch(randint0(12)) {
				case 0:
					switch(randint0(7)) {
						default:
							strcpy(suff, " X");
							break;
						case 1:
							strcpy(suff, " Z");
							break;
						case 2:
							strcpy(suff, " ZX");
							break;
						case 3:
							strcpy(suff, " Q");
							break;
						case 4:
							strcpy(suff, " XX");
							break;
						case 5:
							strcpy(suff, " II");
							break;
					}
					if (!one_in_(3))
						break;
					sprintf(suff + strlen(suff), " %d", rn1 % 10);
					suff[sizeof(suff)-1] = 0;
					break;
				case 5:
					snprintf(suff, sizeof(suff), " %d", rn1);
					suff[sizeof(suff)-1] = 0;
					break;
				case 1:
				case 2:
				case 3:
					snprintf(suff, sizeof(suff), " %d.%d", rn1, rn2);
					suff[sizeof(suff)-1] = 0;
					break;
				case 4:
					snprintf(suff, sizeof(suff), " %d%d", rn1, rn2);
					suff[sizeof(suff)-1] = 0;
					break;
				case 6:
					snprintf(suff, sizeof(suff), " %d.%d.%d", rn1, rn2, rn3);
					suff[sizeof(suff)-1] = 0;
					break;
			}
			char buf[64];
			snprintf(buf, sizeof(buf), "%s%s%s%s", base, sep, ext, suff);
			buf[sizeof(buf)-1] = 0;
			insert_string(buf, i, &card_adj, &n_card_adj);
			i++;
		}
	}
	flavor_assign_random(TV_CARD);

	/* Hack -- Use the "complex" RNG */
	Rand_quick = false;

	/* Analyze every object */
	for (i = 0; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		/* Skip "empty" objects */
		if (!kind->name) continue;

		/* No flavor yields aware */
		if (!kind->flavor) kind->aware = true;
	}
}

/**
 * Set all flavors as aware
 */
void flavor_set_all_aware(void)
{
	int i;

	/* Analyze every object */
	for (i = 0; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		/* Skip empty objects */
		if (!kind->name) continue;

		/* Flavor yields aware */
		if (kind->flavor) kind->aware = true;
	}
}

/**
 * Obtain the flags for an item
 */
void object_flags(const struct object *obj, bitflag flags[OF_SIZE])
{
	of_wipe(flags);
	if (!obj) return;
	of_copy(flags, obj->flags);
}


/**
 * Obtain the flags for an item which are known to the player
 */
void object_flags_known(const struct object *obj, bitflag flags[OF_SIZE])
{
	object_flags(obj, flags);
	of_inter(flags, obj->known->flags);

	if (!obj->kind) {
		return;
	}

	if (object_flavor_is_aware(obj)) {
		of_union(flags, obj->kind->flags);
	}

	for(int i=0;i<MAX_EGOS;i++) {
		if (obj->ego[i] && easy_know(obj)) {
			of_union(flags, obj->ego[i]->flags);
			of_diff(flags, obj->ego[i]->flags_off);
		}
	}
}

/**
 * Obtain the carried flags for an item
 */
void object_carried_flags(const struct object *obj, bitflag flags[OF_SIZE])
{
	of_wipe(flags);
	if (!obj) return;
	of_copy(flags, obj->carried_flags);
}

/**
 * Obtain the carried flags for an item which are known to the player
 */
void object_carried_flags_known(const struct object *obj, bitflag flags[OF_SIZE])
{
	object_carried_flags(obj, flags);
	of_inter(flags, obj->known->carried_flags);

	if (!obj->kind) {
		return;
	}

	if (object_flavor_is_aware(obj)) {
		of_union(flags, obj->kind->carried_flags);
	}

	for(int i=0;i<MAX_EGOS;i++) {
		if (obj->ego[i] && easy_know(obj)) {
			of_union(flags, obj->ego[i]->carried_flags);
			of_diff(flags, obj->ego[i]->carried_flags_off);
		}
	}
}

/**
 * Apply a tester function, skipping all non-objects and gold
 */
bool object_test(item_tester tester, const struct object *obj)
{
	/* Require object */
	if (!obj) return false;

	/* Ignore gold */
	if (tval_is_money(obj)) return false;

	/* Pass without a tester, or tail-call the tester if it exists */
	return !tester || tester(obj);
}


/**
 * Return true if the item is unknown (has yet to be seen by the player).
 */
bool is_unknown(const struct object *obj)
{
	struct grid_data gd = {
		.m_idx = 0,
		.f_idx = 0,
		.first_kind = NULL,
		.trap = NULL,
		.multiple_objects = false,
		.unseen_object = false,
		.unseen_money = false,
		.lighting = LIGHTING_LOS,
		.in_view = false,
		.is_player = false,
		.hallucinate = false,
	};
	map_info(obj->grid, &gd);
	return gd.unseen_object;
}	


/**
 * Looks if "inscrip" is present on the given object.
 */
unsigned check_for_inscrip(const struct object *obj, const char *inscrip)
{
	unsigned i = 0;
	const char *s;

	if (!obj->note) return 0;

	s = quark_str(obj->note);

	/* Needing this implies there are bad instances of obj->note around,
	 * but I haven't been able to track down their origins - NRM */
	if (!s) return 0;

	do {
		s = strstr(s, inscrip);
		if (!s) break;

		i++;
		s++;
	} while (s);

	return i;
}

/**
 * Looks if "inscrip" immediately followed by a decimal integer without a
 * leading sign character is present on the given object.  Returns the number
 * of times such an inscription occurs and, if that value is at least one,
 * sets *ival to the value of the integer that followed the first such
 * inscription.
 */
unsigned check_for_inscrip_with_int(const struct object *obj, const char *inscrip, int* ival)
{
	unsigned i = 0;
	size_t inlen = strlen(inscrip);
	const char *s;

	if (!obj->note) return 0;

	s = quark_str(obj->note);

	/* Needing this implies there are bad instances of obj->note around,
	 * but I haven't been able to track down their origins - NRM */
	if (!s) return 0;

	do {
		s = strstr(s, inscrip);
		if (!s) break;
		if (isdigit(s[inlen])) {
			if (i == 0) {
				long inarg = strtol(s + inlen, 0, 10);

				*ival = (inarg < INT_MAX) ? (int) inarg : INT_MAX;
			}
			i++;
		}
		s++;
	} while (s);

	return i;
}

/*** Object kind lookup functions ***/

/**
 * Return the object kind with the given `tval` and `sval`, or NULL.
 */
struct object_kind *lookup_kind(int tval, int sval)
{
	int k;

	if (!z_info)
		return NULL;

	/* Look for it */
	for (k = 0; k < z_info->k_max; k++) {
		struct object_kind *kind = &k_info[k];
		if (kind->tval == tval && kind->sval == sval)
			return kind;
	}

	/* Failure */
	msg("No object: %d:%d (%s)", tval, sval, tval_find_name(tval));

	return NULL;
}

struct object_kind *objkind_byid(int kidx) {
	if (kidx < 0 || kidx >= z_info->k_max)
		return NULL;
	return &k_info[kidx];
}


/*** Textual<->numeric conversion ***/

/* Levenshtein distance (number of edits needed to transform one string into the other)
 * Source: https://rosettacode.org/wiki/Levenshtein_distance#C
 * Modified to avoid nesting functions
 */
 
static int leven_ls, leven_lt;
static const char *leven_s;
static const char *leven_t;
static int *leven_d;

static int leven_dist(int i, int j) {
	if (leven_d[(i * (leven_lt + 1)) + j] >= 0)
		return leven_d[(i * (leven_lt + 1)) + j];

	int x;
	if (i == leven_ls)
		x = leven_lt - j;
	else if (j == leven_lt)
		x = leven_ls - i;
	else if (leven_s[i] == leven_t[j])
		x = leven_dist(i + 1, j + 1);
	else {
		x = leven_dist(i + 1, j + 1);

		int y;
		if ((y = leven_dist(i, j + 1)) < x) x = y;
		if ((y = leven_dist(i + 1, j)) < x) x = y;
		x++;
	}
	leven_d[(i * (leven_lt + 1)) + j] = x;
	return x;
}

static int levenshtein(const char *s, const char *t)
{
	leven_ls = strlen(s);
	leven_lt = strlen(t);
	leven_s = s;
	leven_t = t;
	leven_d = mem_alloc((leven_ls + 1) * (leven_lt + 1) * sizeof(int));
 
	for (int i = 0; i < (leven_ls + 1) * (leven_lt + 1); i++)
		leven_d[i] = -1;
 
	int d = leven_dist(0, 0);
	mem_free(leven_d);
	leven_d = NULL;
	return d;
}

const struct object_kind *lookup_kind_name_fuzzy(const char *name, int *fuzz)
{
	char buf[64];
	int k;
	int k_idx = -1;
	int distance = fuzz ? *fuzz : (int)(strlen(name));

	if (!z_info)
		return NULL;

	/* Look for it */
	WISH_DPF("knf: name %s\n", name);
	for (k = 0; k < z_info->k_max; k++) {
		const struct object_kind *kind = &k_info[k];

		/* Test for close matches to the name or its plural */
		if (kind->name) {
			for(bool plural = false; plural != true; plural = true) {
				obj_desc_name_format(buf, sizeof(buf), 0, kind->name, NULL, plural);
				int ldist = (levenshtein(buf, name) * 3) + 1;
				if (ldist < distance) {
					WISH_DPF("knf: name %s, match %s, dist %d\n", name, buf, ldist);
					distance = ldist;
					k_idx = k;
					if (distance == 0) {
						k = z_info->k_max;
					}
				}
			}
		}
	}

	/* Return our best match */
	if (fuzz) {
		*fuzz = distance;
	}
	return k_idx > 0 ? &k_info[k_idx] : NULL;
}

const struct object_kind *lookup_kind_name(const char *name)
{
	int k;
	int k_idx = -1;

	if (!z_info)
		return NULL;

	/* Look for it */
	for (k = 0; k < z_info->k_max; k++) {
		const struct object_kind *kind = &k_info[k];
		/* Test for equality */
		if (kind->name && streq(name, kind->name))
			return kind;
		
		/* Test for close matches */
		if (strlen(name) >= 3 && kind->name && my_stristr(kind->name, name)
			&& k_idx == -1)
			k_idx = k;
	}

	/* Return our best match */
	return k_idx >= 0 ? &k_info[k_idx] : NULL;
}

const struct artifact *lookup_artifact_name_fuzzy(const char *name, int *fuzz)
{
	char in[64];
	char buf[64];
	int a_idx = -1;
	int distance = fuzz ? *fuzz : (int)(strlen(name));

	if (!z_info)
		return NULL;

	/* Convert to lower case - assumes there is no punctuation */
	char *inp = in;
	if (strlen(name) >= sizeof(in))
		return NULL;
	int in_lower = (islower(*name));

	while (*name) {
		*inp++ = tolower(*name++);
	}
	*inp = 0;

	/* Look for it */
	for (int i = 0; i < z_info->a_max; i++) {
		const struct artifact *art = &a_info[i];

		/* Skip existing artifacts */
		if (is_artifact_created(art))
			continue;

		/* Test for close matches to the name */
		if (art->name) {
			/* Convert to lower case, removing punctuation (but not spaces)
			 * and leading 'of '
			 */
			char *namep = art->name;
			char *bufp = buf;
			unsigned len = strlen(art->name);
			if ((len >= 3) && (len < sizeof(buf))) {
				if (!strncmp(namep, "of ", 3))
					namep += 3;
				int name_lower = -1;
				while (*namep) {
					char c = *namep++;
					if (!ispunct(c)) {
						if (name_lower < 0)
							name_lower = (islower(c));
						*bufp++ = tolower(c);
					}
				}
				*bufp = 0;

				/* Fuzzy match this */
				int ldist = (levenshtein(in, buf) * 3);
				if (in_lower != name_lower)
					ldist+=2;
				if (ldist < distance) {
					distance = ldist;
					a_idx = i;
					if (distance == 0) {
						break;
					}
				}
			}
		}
	}

	/* Return our best match */
	if (fuzz) {
		*fuzz = distance;
	}
	return a_idx >= 0 ? &a_info[a_idx] : NULL;
}

/**
 * Return the artifact with the given name
 */
const struct artifact *lookup_artifact_name(const char *name)
{
	int i;
	int a_idx = -1;

	/* Look for it */
	for (i = 0; i < z_info->a_max; i++) {
		const struct artifact *art = &a_info[i];

		/* Test for equality */
		if (art->name && streq(name, art->name))
			return art;
		
		/* Test for close matches */
		if (strlen(name) >= 3 && art->name && my_stristr(art->name, name)
			&& a_idx == -1)
			a_idx = i;
	}

	/* Return our best match */
	return a_idx >= 0 ? &a_info[a_idx] : NULL;
}

const struct ego_item *lookup_ego_name_fuzzy(const char *name, const struct object_kind *kind, const struct ego_item **ego, int *fuzz)
{
	char in[64];
	char buf[64];
	int e_idx = -1;
	int distance = fuzz ? *fuzz : (int)(strlen(name));

	if (!z_info)
		return NULL;

	/* Convert to lower case - assumes there is no punctuation */
	char *inp = in;
	if (strlen(name) >= sizeof(in))
		return NULL;
	while (*name) {
		*inp++ = tolower(*name++);
	}
	*inp = 0;

	u16b mego[MAX_EGOS] = { 0 };
	bool megocheck = false;
	int negos = 0;
	if (kind && ego && *ego) {
		megocheck = true;
		for(int i=0;i<MAX_EGOS;i++) {
			if (!ego[i]) {
				negos = i;
				break;
			}
			mego[i] = ego[i]->eidx;
		}
	}

	/* Look for it */
	for (int i = 0; i < z_info->e_max; i++) {
		const struct ego_item *e = &e_info[i];

		/* If 'kind' is available, avoid inapplicable egos */
		if (kind) {
			bool match = false;
			for (struct poss_item *poss = e->poss_items; poss; poss = poss->next) {
				if (poss->kidx == kind->kidx) {
					match = true;
					break;
				}
			}
			if (!match)
				continue;
		}

		/* Also avoid multi-egos that don't match, if there is at least one ego already */
		bool ok = true;
		if (megocheck) {
			mego[negos] = i;
			if (!multiego_allow(mego)) 
				continue;

			for(int i=0;i<negos;i++) {
				if (ego[i] == e)
					ok = false;
			}
		}
		if (!ok)
			continue;

		/* Test for close matches to the name */
		if (e->name) {
			/* Convert to lower case, removing punctuation (but not spaces)
			 * and leading 'of '
			 */
			char *namep = e->name;
			char *bufp = buf;
			unsigned len = strlen(e->name);
			if ((len >= 3) && (len < sizeof(buf))) {
				if (!strncmp(namep, "of ", 3))
					namep += 3;
				while (*namep) {
					char c = tolower(*namep++);
					if ((!ispunct(c)) || (c == '*') || (c == '-'))
						*bufp++ = c;
				}
				*bufp = 0;

				/* Fuzzy match this */
				int ldist = levenshtein(in, buf) * 3;
				if (ldist < distance) {
					distance = ldist;
					e_idx = i;
					if (distance == 0) {
						break;
					}
				}
			}
		}
	}

	/* Return our best match */
	if (fuzz) {
		*fuzz = distance;
	}
	return e_idx >= 0 ? &e_info[e_idx] : NULL;
}

/**
 * Return the ego item with the given name
 */
const struct ego_item *lookup_ego_name(const char *name)
{
	int i;
	int e_idx = -1;

	/* Look for it */
	for (i = 0; i < z_info->e_max; i++) {
		const struct ego_item *e = &e_info[i];

		/* Test for equality */
		if (e->name && streq(name, e->name))
			return e;
		
		/* Test for close matches */
		if (strlen(name) >= 3 && e->name && my_stristr(e->name, name)
			&& e_idx == -1)
			e_idx = i;
	}

	/* Return our best match */
	return e_idx >= 0 ? &e_info[e_idx] : NULL;
}

/**
 * \param name ego type name
 * \param tval object tval
 * \param sval object sval
 * \return eidx of the ego item type
 */
struct ego_item *lookup_ego_item(const char *name, int tval, int sval)
{
	struct object_kind *kind = lookup_kind(tval, sval);
	int i;
	if (!kind)
		return NULL;

	/* Look for it */
	for (i = 0; i < z_info->e_max; i++) {
		struct ego_item *ego = &e_info[i];
		struct poss_item *poss_item = ego->poss_items;

		/* Reject nameless and wrong names */
		if (!ego->name) continue;
		if (!my_stristr(name, ego->name)) continue;

		/* Check tval and sval */
		while (poss_item) {
			if (kind->kidx == poss_item->kidx) {
				return ego;
			}
			poss_item = poss_item->next;
		}
	}

	return NULL;
}

/**
 * Returns true if the object has an ego with name matching (as a case insensitive substring) the given name.
 * Multiple ego items will try to match any of them.
 * \param obj object
 * \param name ego type name
 */
bool obj_has_ego(const struct object *obj, const char *name) {
	for (int i=0;i<MAX_EGOS;i++) {
		struct ego_item *ego = obj->ego[i];
		if (ego) {
			if (my_stristr(ego->name, name))
				return true;
		}
	}
	return false;
}

/**
 * Return the numeric sval of the object kind with the given `tval` and
 * name `name`, and an ego item if that is also specified. Will find partial
 * matches ("combat boots" matching "pair of combat boots"), but only if no
 * exact match is found. If neither is found then it will look for a reverse
 * partial match ("hard hat (lamp)" matching "hard hat") - in that case it
 * will try to find an ego item ("hard hat (lamp)" matching "(lamp)").
 * The ego item pointer may be NULL, but if specified the resulting ego item
 * pointer (or NULL) will always be returned through it.
 */
int lookup_sval_ego(int tval, const char *name, const struct ego_item **ego)
{
	int k, e;
	unsigned int r;
	int length = 0;

	/* By default, no ego */
	if (ego)
		*ego = NULL;

	if ((sscanf(name, "%u%n", &r, &length) == 1) && (length == (int)strlen(name)))
		return r;

	/* Look for it */
	for (k = 0; k < z_info->k_max; k++) {
		struct object_kind *kind = &k_info[k];
		char cmp_name[1024];

		if (!kind || !kind->name) continue;

		obj_desc_name_format(cmp_name, sizeof cmp_name, 0, kind->name, 0,
							 false);

		/* Found a match */
		if (kind->tval == tval && !my_stricmp(cmp_name, name)) {
			return kind->sval;
		}
	}

	/* Try for a partial match */
	for (k = 0; k < z_info->k_max; k++) {
		struct object_kind *kind = &k_info[k];
		char cmp_name[1024];

		if (!kind || !kind->name) continue;

		obj_desc_name_format(cmp_name, sizeof cmp_name, 0, kind->name, 0,
							 false);

		/* Found a partial match */
		if (kind->tval == tval && my_stristr(cmp_name, name)) {
			return kind->sval;
		}
	}

	/* Try for a reverse partial match */
	for (k = 0; k < z_info->k_max; k++) {
		struct object_kind *kind = &k_info[k];
		char cmp_name[1024];

		if (!kind || !kind->name) continue;

		obj_desc_name_format(cmp_name, sizeof cmp_name, 0, kind->name, 0,
							 false);

		/* Found a reverse partial match */
		if (kind->tval == tval && my_stristr(name, cmp_name)) {
			if (ego) {
				/* Search for an ego item */
				for (e = 0; e < z_info->e_max; e++) {
					struct ego_item *eitem = &e_info[e];
					if (my_stristr(name, eitem->name)) {
						*ego = eitem;
						break;
					}
				}
			}
			return kind->sval;
		}
	}

	return -1;
}

/**
 * Return the numeric sval of the object kind with the given `tval` and
 * name `name`. Will find partial matches ("combat boots" matching "pair
 * of combat boots"), but only if no exact match is found.
 */
int lookup_sval(int tval, const char *name)
{
	return lookup_sval_ego(tval, name, NULL);
}

void object_short_name(char *buf, size_t max, const char *name)
{
	size_t j, k;
	/* Copy across the name, stripping modifiers & and ~) */
	size_t len = strlen(name);
	for (j = 0, k = 0; j < len && k < max - 1; j++) {
		if (j == 0 && name[0] == '&' && name[1] == ' ')
			j += 2;
		if (name[j] == '~')
			continue;

		buf[k++] = name[j];
	}
	buf[k] = 0;
}

/**
 * Sort comparator for objects using only tval and sval.
 * -1 if o1 should be first
 *  1 if o2 should be first
 *  0 if it doesn't matter
 */
static int compare_types(const struct object *o1, const struct object *o2)
{
	if (o1->tval == o2->tval)
		return CMP(o1->sval, o2->sval);
	else
		return CMP(o1->tval, o2->tval);
}	


/**
 * Sort comparator for objects
 * -1 if o1 should be first
 *  1 if o2 should be first
 *  0 if it doesn't matter
 *
 * The sort order is designed with the "list items" command in mind.
 */
int compare_items(const struct object *o1, const struct object *o2)
{
	/* unknown objects go at the end, order doesn't matter */
	if (is_unknown(o1)) {
		return (is_unknown(o2)) ? 0 : 1;
	} else if (is_unknown(o2)) {
		return -1;
	}

	/* known artifacts will sort first */
	if (object_is_known_artifact(o1) && object_is_known_artifact(o2))
		return compare_types(o1, o2);
	if (object_is_known_artifact(o1)) return -1;
	if (object_is_known_artifact(o2)) return 1;

	/* unknown objects will sort next */
	if (!object_flavor_is_aware(o1) && !object_flavor_is_aware(o2))
		return compare_types(o1, o2);
	if (!object_flavor_is_aware(o1)) return -1;
	if (!object_flavor_is_aware(o2)) return 1;

	/* if only one of them is worthless, the other comes first */
	if (o1->kind->cost == 0 && o2->kind->cost != 0) return 1;
	if (o1->kind->cost != 0 && o2->kind->cost == 0) return -1;

	/* otherwise, just compare tvals and svals */
	/* NOTE: arguably there could be a better order than this */
	return compare_types(o1, o2);
}


/**
 * Determine if an object has charges
 */
bool obj_has_charges(const struct object *obj)
{
	if (!tval_can_have_charges(obj)) return false;

	if (obj->pval <= 0) return false;

	return true;
}

/**
 * Determine if an object is zappable
 */
bool obj_can_zap(const struct object *obj)
{
	/* Any rods not charging? */
	if (tval_can_have_timeout(obj) && (!tval_is_light(obj)) && number_charging(obj) < obj->number)
		return true;

	return false;
}

/**
 * Determine if an object is activatable from inventory.
 * Most activatable items aren't, but printers (and any future unequippable activatable tools) must be.
 * Lights are less clunky if they can be activated in the pack.
 */
bool obj_is_pack_activatable(const struct object *obj)
{
	if ((tval_is_light(obj)) && (kf_has(obj->kind->kind_flags, KF_MIMIC_KNOW)))
		return true;

	if (obj_is_activatable(obj)) {
		/* Object has an activation */
		if (object_is_equipped(player->body, obj)) {
			/* If so, and you are wearing it, it's activatable */
			return true;
		} else {
			/* If not, it might still be activatable if it's the right tval */
			if (tval_is_printer(obj)) return true;
		}
	}
	return false;
}

/**
 * Determine if an object is activatable
 */
bool obj_is_activatable(const struct object *obj)
{
	return (object_effect(obj) && (!of_has(obj->flags, OF_NO_ACTIVATION))) ? true : false;
}

/**
 * Determine if an object can be activated now
 */
bool obj_can_activate(const struct object *obj)
{
	/* Candle type light sources can always be activated - it's equivalent to equipping and unequipping it.
	 * And not equivalent to running the effect (which happens on timeout).
	 */
	if ((tval_is_light(obj)) && (kf_has(obj->kind->kind_flags, KF_MIMIC_KNOW)))
		return true;
	if (obj_is_activatable(obj)) {
		/* Check the recharge */
		if (!obj->timeout) return true;
	}

	return false;
}

/**
 * Check if an object can be used to refuel other objects.
 */
bool obj_can_refill(const struct object *obj)
{
	const struct object *light = equipped_item_by_slot_name(player, "light");

	/* Need fuel? */
	if (of_has(obj->flags, OF_NO_FUEL)) return false;

	/* A lamp can be refueled from a battery */
	if (light && of_has(light->flags, OF_TAKES_FUEL)) {
		if (tval_is_fuel(obj))
			return true;
	}

	return false;
}

/* Can only take off non-sticky (or for the special case of lamps, uncharged) items */
bool obj_can_takeoff(const struct object *obj)
{
	if (of_has(obj->flags, OF_NO_EQUIP))
		return false;
	if (!obj_has_flag(obj, OF_STICKY))
		return true;
	if (tval_is_light(obj) && (obj->timeout == 0))
		return true;
	return false;
}

/* Equivalent, but implants can be removed (for use by the store) */
bool obj_cyber_can_takeoff(const struct object *obj)
{
	if (!obj_has_flag(obj, OF_STICKY))
		return true;
	if (tval_is_light(obj) && (obj->timeout == 0))
		return true;
	return false;
}

/* Can only put on wieldable items */
bool obj_can_wear(const struct object *obj)
{
	return ((wield_slot(obj) >= 0) && (!(of_has(obj->flags, OF_NO_EQUIP))));
}

/* Can only fire an item with the right tval */
bool obj_can_fire(const struct object *obj)
{
	return obj->tval == player->state.ammo_tval;
}

/**
 * Determine if an object is designed for throwing
 */
bool obj_is_throwing(const struct object *obj)
{
	return of_has(obj->flags, OF_THROWING);
}

/**
 * Determine if an object is a known artifact
 */
bool obj_is_known_artifact(const struct object *obj)
{
	if (!obj->artifact) return false;
	if (!obj->known) return false;
	if (!obj->known->artifact) return false;
	return true;
}

/* Can has inscrip pls */
bool obj_has_inscrip(const struct object *obj)
{
	return (obj->note ? true : false);
}

bool obj_has_flag(const struct object *obj, int flag)
{
	struct fault_data *c = obj->faults;

	/* Check the object's own flags */
	if (of_has(obj->flags, flag)) {
		return true;
	}

	/* Check any fault object flags */
	if (c) {
		int i;
		for (i = 1; i < z_info->fault_max; i++) {
			if (c[i].power && of_has(faults[i].obj->flags, flag)) {
				return true;
			}
		}
	}
	return false;
}

bool obj_is_useable(const struct object *obj)
{
	if (tval_is_useable(obj))
		return true;

	if (object_effect(obj))
		return true;

	if (tval_is_ammo(obj))
		return obj->tval == player->state.ammo_tval;

	return false;
}

const struct object_material *obj_material(const struct object *obj)
{
	return material + obj->kind->material;
}

bool obj_is_metal(const struct object *obj)
{
	return obj_material(obj)->metal;
}

/*** Generic utility functions ***/

/**
 * Return an object's effect.
 */
struct effect *object_effect(const struct object *obj)
{
	if (obj->activation)
		return obj->activation->effect;
	else if (obj->effect)
		return obj->effect;
	else
		return NULL;
}

/**
 * Does the given object need to be aimed?
 */ 
bool obj_needs_aim(struct object *obj)
{
	struct effect *effect = object_effect(obj);

	/* If the effect needs aiming, or if the object type needs
	   aiming, this object needs aiming. */
	return effect_aim(effect) || tval_is_ammo(obj) ||
			tval_is_wand(obj) ||
			(tval_is_rod(obj) && !object_flavor_is_aware(obj));
}

/**
 * Can the object fail if used?
 */
bool obj_can_fail(const struct object *o)
{
	if (tval_can_have_failure(o))
		return true;

	return wield_slot(o) == -1 ? false : true;
}


/**
 * Failure rate for magic devices.
 * This has been rewritten for 4.2.3 following the discussions in the thread
 * http://angband.oook.cz/forum/showthread.php?t=10594
 * It uses a scaled, shifted version of the sigmoid function x/(1+|x|), namely
 * 380 - 370(x/(10+|x|)), where x is 2 * (device skill - device level) + 1,
 * to give fail rates out of 1000.
 */
int get_use_device_chance(const struct object *obj)
{
	int lev, fail, x;
	int skill = player->state.skills[SKILL_DEVICE];

	/* Depends on what kind of object in it */
	if (tval_is_printer(obj) || tval_is_battery(obj)) {
		/* Printers are limited by chunk supplies and fail after using chunks,
		 * so don't need any further limiting. Batteries are supposed to be
		 * obvious... and all batteries that will get here (that is, reusable
		 * ones like the atomic cell) are limited by the cooldown.
		 */
		return 0;
	}

	/* Extract the item level, which is the difficulty rating */
	if (obj->artifact)
		lev = obj->artifact->level;
	else
		lev = obj->kind->level;

	/* Calculate x */
	x = 2 * (skill - lev) + 1;

	/* Now calculate the failure rate */
	fail = -370 * x;
	fail /= (10 + ABS(x));
	fail += 380;

	return fail;
}


/**
 * Distribute charges of rods, devices, or wands.
 *
 * \param source is the source item
 * \param dest is the target item, must be of the same type as source
 * \param amt is the number of items that are transfered
 */
void distribute_charges(struct object *source, struct object *dest, int amt)
{
	int charge_time = randcalc(source->time, 0, AVERAGE), max_time;

	/*
	 * Hack -- If rods, devices, or wands are dropped, the total maximum
	 * timeout or charges need to be allocated between the two stacks.
	 * If all the items are being dropped, it makes for a neater message
	 * to leave the original stack's pval alone. -LM-
	 */
	if (tval_can_have_charges(source)) {
		dest->pval = source->pval * amt / source->number;

		if (amt < source->number)
			source->pval -= dest->pval;
	}

	/*
	 * Hack -- Rods also need to have their timeouts distributed.
	 *
	 * The dropped stack will accept all time remaining to charge up to
	 * its maximum.
	 */
	if (tval_can_have_timeout(source) && (!tval_is_light(source))) {
		max_time = charge_time * amt;

		if (source->timeout > max_time)
			dest->timeout = max_time;
		else
			dest->timeout = source->timeout;

		if (amt < source->number)
			source->timeout -= dest->timeout;
	}
}


/**
 * Number of items (usually rods) charging
 */
int number_charging(const struct object *obj)
{
	int charge_time, num_charging;

	charge_time = randcalc(obj->time, 0, AVERAGE);

	/* Item has no timeout */
	if (charge_time <= 0) return 0;

	/* No items are charging */
	if (obj->timeout <= 0) return 0;

	/* Calculate number charging based on timeout */
	num_charging = (obj->timeout + charge_time - 1) / charge_time;

	/* Number charging cannot exceed stack size */
	if (num_charging > obj->number) num_charging = obj->number;

	return num_charging;
}

/**
 * Allow a stack of charging objects to charge by one unit per charging object
 * Return true if something recharged
 */
bool recharge_timeout(struct object *obj)
{
	int charging_before, charging_after;

	/* Find the number of charging items */
	charging_before = charging_after = number_charging(obj);

	/* Nothing to charge */	
	if (charging_before == 0)
		return false;

	/* Decrease the timeout */
	if ((!tval_is_light(obj)) || (obj->timeout < randcalc(obj->kind->pval, 0, AVERAGE))) {
		if (obj->timeout > 0) {
			obj->timeout -= MIN(charging_before, obj->timeout);
			charging_after = number_charging(obj);
			if (obj->timeout <= 0) {
				obj->timeout = 0;
				if (tval_is_light(obj))
					light_timeout(obj, true);
				/* The object may no longer exist */
			}
		}
	}

	/* Return true if at least 1 item obtained a charge */
	if (charging_after < charging_before)
		return true;
	else
		return false;
}

/**
 * Verify the choice of an item.
 *
 * The item can be negative to mean "item on floor".
 */
bool verify_object(const char *prompt, const struct object *obj,
		const struct player *p)
{
	char o_name[80];

	char out_val[160];

	/* Describe */
	object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL, p);

	/* Prompt */
	strnfmt(out_val, sizeof(out_val), "%s %s? ", prompt, o_name);

	/* Query */
	return (get_check(out_val));
}


typedef enum {
	MSG_TAG_NONE,
	MSG_TAG_NAME,
	MSG_TAG_BRIEFNAME,
	MSG_TAG_KIND,
	MSG_TAG_FLAVOR,
	MSG_TAG_VERB,
	MSG_TAG_VERB_IS
} msg_tag_t;

static msg_tag_t msg_tag_lookup(const char *tag)
{
	if (strncmp(tag, "name", 4) == 0) {
		return MSG_TAG_NAME;
	} else if (strncmp(tag, "briefname", 9) == 0) {
		return MSG_TAG_BRIEFNAME;
	} else if (strncmp(tag, "kind", 4) == 0) {
		return MSG_TAG_KIND;
	} else if (strncmp(tag, "flavor", 6) == 0) {
		return MSG_TAG_FLAVOR;
	} else if (strncmp(tag, "s", 1) == 0) {
		return MSG_TAG_VERB;
	} else if (strncmp(tag, "is", 2) == 0) {
		return MSG_TAG_VERB_IS;
	} else {
		return MSG_TAG_NONE;
	}
}

/**
 * Format a message from a string, customised to include details about an object
 */
char *format_custom_message(const struct object *obj, const char *string, char *buf, int len, const struct player *p)
{
	const char *next;
	const char *s;
	const char *tag;
	const char *orig = string;
	size_t end = 0;
	*buf = 0;

	/* Not always a string */
	if (!string) return buf;

	next = strchr(string, '{');
	while (next) {
		/* Copy the text leading up to this { */
		strnfcat(buf, len, &end, "%.*s", next - string, string); 

		s = next + 1;
		while (*s && isalpha((unsigned char) *s)) s++;

		/* Valid tag */
		if (*s == '}') {
			/* Start the tag after the { */
			tag = next + 1;
			string = s + 1;

			switch(msg_tag_lookup(tag)) {
			case MSG_TAG_NAME:
				if (obj) {
					u32b flags = ODESC_PREFIX | ODESC_BASE;
					/* First character of the string, so capitalize */
					if (next == orig)
						flags |= ODESC_CAPITAL;
					end += object_desc(buf + strlen(buf), len, obj, flags, p);
				} else {
					strnfcat(buf, len, &end, "hands");
				}
				break;
			case MSG_TAG_BRIEFNAME:
				if (obj) {
					u32b flags = ODESC_BASE;
					/* First character of the string, so capitalize */
					if (next == orig)
						flags |= ODESC_CAPITAL;
					end += object_desc(buf + strlen(buf), len, obj, flags, p);
				} else {
					strnfcat(buf, len, &end, "hands");
				}
				break;
			case MSG_TAG_FLAVOR:
				if (obj && (obj->kind->flavor) && (obj->kind->flavor->text) && (!object_flavor_is_aware(obj))) {
					obj_desc_name_format(&buf[end], sizeof(buf) - end, 0, obj_desc_basename(obj, true, true, true, p), obj->kind->flavor->text, false);
					end += strlen(&buf[end]);
					break;
				}
				/* FALL THROUGH */
			case MSG_TAG_KIND:
				if (obj) {
					object_kind_name(&buf[end], len - end, obj->kind, true);
					end += strlen(&buf[end]);
				} else {
					strnfcat(buf, len, &end, "hands");
				}
				break;
			case MSG_TAG_VERB:
				if (obj && obj->number == 1) {
					strnfcat(buf, len, &end, "s");
				}
				break;
			case MSG_TAG_VERB_IS:
				if ((!obj) || (obj->number > 1)) {
					strnfcat(buf, len, &end, "are");
				} else {
					strnfcat(buf, len, &end, "is");
				}
			default:
				break;
			}
		} else
			/* An invalid tag, skip it */
			string = next + 1;

		next = strchr(string, '{');
	}
	strnfcat(buf, len, &end, string);
	return buf;
}

/**
 * Print a message from a string, customised to include details about an object
 */
void print_custom_message(struct object *obj, const char *string, int msg_type, const struct player *p)
{
	char buf[1024];
	format_custom_message(obj, string, buf, sizeof(buf), p);
	msgt(msg_type, "%s", buf);
}

/**
 * Return if the given artifact has been created.
 */
bool is_artifact_created(const struct artifact *art)
{
	assert(art->aidx == aup_info[art->aidx].aidx);
	return aup_info[art->aidx].created;
}

/**
 * Return if the given artifact has been seen.
 */
bool is_artifact_seen(const struct artifact *art)
{
	assert(art->aidx == aup_info[art->aidx].aidx);
	return aup_info[art->aidx].seen;
}

/**
 * Return if the given artifact has ever been seen.
 */
bool is_artifact_everseen(const struct artifact *art)
{
	assert(art->aidx == aup_info[art->aidx].aidx);
	return aup_info[art->aidx].everseen;
}


/**
 * Set whether the given artifact has been created or not.
 */
void mark_artifact_created(const struct artifact *art, bool created)
{
	assert(art->aidx == aup_info[art->aidx].aidx);
	aup_info[art->aidx].created = created;
}

/**
 * Set whether the given artifact has been created or not.
 */
void mark_artifact_seen(const struct artifact *art, bool seen)
{
	assert(art->aidx == aup_info[art->aidx].aidx);
	aup_info[art->aidx].seen = seen;
}

/**
 * Set whether the given artifact has been seen or not.
 */
void mark_artifact_everseen(const struct artifact *art, bool seen)
{
	assert(art->aidx == aup_info[art->aidx].aidx);
	aup_info[art->aidx].everseen = seen;
}
