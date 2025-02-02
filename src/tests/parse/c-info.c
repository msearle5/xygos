/* parse/c-info */

#include "unit-test.h"

#include "init.h"
#include "obj-properties.h"
#include "object.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player.h"

int setup_tests(void **state) {
	*state = init_parse_class();
	return !*state;
}

int teardown_tests(void *state) {
	struct player_class *c = parser_priv(state);
	int i;
	string_free((char *)c->name);
	for (i = 0; i < (int)(c->titles); i++) {
		string_free((char *)c->title[i]);
	}
	while (c->start_items) {
		struct start_item *si = c->start_items;

		c->start_items = si->next;
		mem_free(si->eopts);
		mem_free(si);
	}
	/*
	 * If tests are set up that lead to spell allocation, those will also
	 * have to be freed here before releasing the books.
	 */
	mem_free(c->magic.books);
	mem_free(c);
	parser_destroy(state);
	return 0;
}

static int test_name0(void *state) {
	enum parser_error r = parser_parse(state, "name:Ranger");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	require(streq(c->name, "Ranger"));
	ok;
}

static int test_stats0(void *state) {
	enum parser_error r = parser_parse(state, "stats:3:-3:2:-2:1:2:3");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_adj[STAT_STR], 3);
	eq(c->c_adj[STAT_INT], -3);
	eq(c->c_adj[STAT_WIS], 2);
	eq(c->c_adj[STAT_DEX], -2);
	eq(c->c_adj[STAT_CON], 1);
	eq(c->c_adj[STAT_CHR], 2);
	eq(c->c_adj[STAT_SPD], 3);
	ok;
}

static int test_skill_disarm0(void *state) {
	enum parser_error r = parser_parse(state, "skill-disarm-phys:30:8");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_DISARM_PHYS], 30);
	eq(c->x_skills[SKILL_DISARM_PHYS], 8);
	ok;
}

static int test_skill_device0(void *state) {
	enum parser_error r = parser_parse(state, "skill-device:32:10");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_DEVICE], 32);
	eq(c->x_skills[SKILL_DEVICE], 10);
	ok;
}

static int test_skill_save0(void *state) {
	enum parser_error r = parser_parse(state, "skill-save:28:10");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_SAVE], 28);
	eq(c->x_skills[SKILL_SAVE], 10);
	ok;
}

static int test_skill_stealth0(void *state) {
	enum parser_error r = parser_parse(state, "skill-stealth:3:0");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_STEALTH], 3);
	eq(c->x_skills[SKILL_STEALTH], 0);
	ok;
}

static int test_skill_search0(void *state) {
	enum parser_error r = parser_parse(state, "skill-search:24:0");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_SEARCH], 24);
	eq(c->x_skills[SKILL_SEARCH], 0);
	ok;
}

static int test_skill_melee0(void *state) {
	enum parser_error r = parser_parse(state, "skill-melee:56:30");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_TO_HIT_MELEE], 56);
	eq(c->x_skills[SKILL_TO_HIT_MELEE], 30);
	ok;
}

static int test_skill_shoot0(void *state) {
	enum parser_error r = parser_parse(state, "skill-shoot:72:45");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_TO_HIT_GUN], 72);
	eq(c->x_skills[SKILL_TO_HIT_GUN], 45);
	ok;
}

static int test_skill_throw0(void *state) {
	enum parser_error r = parser_parse(state, "skill-throw:72:45");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_TO_HIT_THROW], 72);
	eq(c->x_skills[SKILL_TO_HIT_THROW], 45);
	ok;
}

static int test_skill_dig0(void *state) {
	enum parser_error r = parser_parse(state, "skill-dig:0:0");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_skills[SKILL_DIGGING], 0);
	eq(c->x_skills[SKILL_DIGGING], 0);
	ok;
}

static int test_hitdie0(void *state) {
	enum parser_error r = parser_parse(state, "hitdie:4");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->c_mhp, 4);
	ok;
}

static int test_max_attacks0(void *state) {
	enum parser_error r = parser_parse(state, "max-attacks:5");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->max_attacks, 5);
	ok;
}

static int test_min_weight0(void *state) {
	enum parser_error r = parser_parse(state, "min-weight:35");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->min_weight, 35);
	ok;
}

static int test_strength_multiplier0(void *state) {
	enum parser_error r = parser_parse(state, "strength-multiplier:4");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->att_multiply, 4);
	ok;
}

static int test_title0(void *state) {
	enum parser_error r0 = parser_parse(state, "title:Runner");
	enum parser_error r1 = parser_parse(state, "title:Strider");
	struct player_class *c;

	eq(r0, PARSE_ERROR_NONE);
	eq(r1, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	require(streq(c->title[0], "Runner"));
	require(streq(c->title[1], "Strider"));
	ok;
}


static int test_flags0(void *state) {
	enum parser_error r = parser_parse(state, "player-flags:CHOOSE_SPELLS");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	require(c->pflags);
	ok;
}

static int test_flags1(void *state) {
	enum parser_error r = parser_parse(state, "obj-flags:SEE_INVIS | IMPAIR_HP");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	require(c->flags);
	ok;
}

static int test_magic0(void *state) {
	enum parser_error r = parser_parse(state, "magic:3:9");
	struct player_class *c;

	eq(r, PARSE_ERROR_NONE);
	c = parser_priv(state);
	require(c);
	eq(c->magic.spell_first, 3);
	ok;
}

const char *suite_name = "parse/c-info";
struct test tests[] = {
	{ "name0", test_name0 },
	{ "stats0", test_stats0 },
	{ "skill_disarm0", test_skill_disarm0 },
	{ "skill_device0", test_skill_device0 },
	{ "skill_save0", test_skill_save0 },
	{ "skill_stealth0", test_skill_stealth0 },
	{ "skill_search0", test_skill_search0 },
	{ "skill_melee0", test_skill_melee0 },
	{ "skill_shoot0", test_skill_shoot0 },
	{ "skill_throw0", test_skill_throw0 },
	{ "skill_dig0", test_skill_dig0 },
	{ "hitdie0", test_hitdie0 },
	{ "max_attacks0", test_max_attacks0 },
	{ "min_weight0", test_min_weight0 },
	{ "strength_multiplier0", test_strength_multiplier0 },
	{ "title0", test_title0 },
	{ "flags0", test_flags0 },
	{ "flags1", test_flags1 },
	{ "magic0", test_magic0 },
	{ NULL, NULL }
};
