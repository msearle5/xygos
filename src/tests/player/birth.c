/* player/birth */

#include "unit-test.h"
#include "unit-test-data.h"
#include "player-birth.h"
#include "player-quest.h"

int setup_tests(void **state) {
	struct player *p = mem_zalloc(sizeof *p);
	z_info = mem_zalloc(sizeof(struct angband_constants));
	z_info->pack_size = 23;
	z_info->quest_max = 1;
	z_info->quiver_size = 10;
	quests = &test_quest;
	races = &TEST_RACE;
	extensions = &TEST_EXT;
	personalities = &TEST_PER;
	classes = &test_class;
	player_init(p);
	*state = p;
	return 0;
}

int teardown_tests(void *state) {
	struct player *p = state;
	player_quests_free((struct player *)state);
	mem_free(z_info);
	if (p->upkeep) {
		mem_free(p->upkeep->inven);
		mem_free(p->upkeep->quiver);
		mem_free(p->upkeep);
	}
	mem_free(p->timed);
	mem_free(p->obj_k->brands);
	mem_free(p->obj_k->slays);
	mem_free(p->obj_k->faults);
	mem_free(p->obj_k);
	mem_free(state);
	return 0;
}

static int test_generate0(void *state) {
	struct player *p = state;
	player = p;
	player_generate(p, &TEST_RACE, &TEST_EXT, &test_class, &TEST_PER, false);
	eq(p->lev, 1);
	ptreq(p->race, &TEST_RACE);
	ptreq(p->extension, &TEST_EXT);
	ptreq(p->personality, &TEST_PER);
	ptreq(p->class, &test_class);
	ok;
}

const char *suite_name = "player/birth";
struct test tests[] = {
	{ "generate0", test_generate0 },
	{ NULL, NULL }
};
