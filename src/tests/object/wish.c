/* object/wish */

#include "test-utils.h"
#include "unit-test.h"
#include "unit-test-data.h"

#include "obj-desc.h"
#include "obj-init.h"
#include "obj-pile.h"
#include "obj-randart.h"
#include "obj-util.h"
#include "object.h"
#include "player-birth.h"
#include "player-util.h"

#include "cave.h"
#include "z-util.h"

extern int n_artinames;
extern struct init_module obj_make_module;

int setup_tests(void **state) {
	/* Init the game */
	set_file_paths();
	init_angband();

	player = &test_player;
	bodies = &test_player_body;
	/* HP array */
	if (!(player->player_hp))
		player->player_hp = mem_zalloc(sizeof(s16b) * PY_MAX_LEVEL * (1 + classes->cidx));
	player->obj_k->slays = mem_zalloc(z_info->slay_max * sizeof(*(player->obj_k->slays)));
	player->obj_k->brands = mem_zalloc(z_info->brand_max * sizeof(*(player->obj_k->brands)));
	player->obj_k->faults = mem_zalloc(z_info->fault_max * sizeof(*(player->obj_k->faults)));
	player->upkeep->quiver = mem_zalloc(z_info->quiver_size * sizeof(struct object *));
	player->upkeep->inven = mem_zalloc((z_info->pack_size + 1) * sizeof(struct object *));
	player->shape = lookup_player_shape("normal");

	roll_hp();
	player_embody(player);

	cave = &test_cave;
	cave->squares = mem_zalloc(sizeof(struct square *) * cave->height);
	cave->squares[0] = mem_zalloc(sizeof(struct square) * cave->height * cave->width);
	for(int i=1;i<cave->height;i++)
		cave->squares[i] = cave->squares[i-1] + cave->width;

	/* Restore the standard artifacts */
	cleanup_parser(&randart_parser);
	deactivate_randart_file();
	run_parser(&artifact_parser);
	/* Use both random and non-random artifacts */
	z_info->a_max = z_info->a_base + z_info->rand_art;
	do_randart(42, true, false);
	deactivate_randart_file();

	return 0;
}


int teardown_tests(void *state) {
	(*obj_make_module.cleanup)();
	mem_free(cave->squares[0]);
	mem_free(cave->squares);
	return 0;
}

static int test_artifacts(void *state) {
	struct object *obj;
	char buf[256];
	char o_name[256];

	for(int i=0;i<z_info->a_max;i++) {
		if (a_info[i].name) {
			char *name = a_info[i].name;
			if (ispunct(*name))
				name++;
			obj = wish(name, 1);
			notnull(obj);
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL | ODESC_SPOIL, player);
			strnfmt(buf, sizeof(buf), "Asked for artifact %s, got %s", name, o_name);
			vnull(obj->ego[0], buf);
			vptreq(obj->artifact, &a_info[i], buf);
			vptreq(obj->kind, lookup_kind(obj->artifact->tval, obj->artifact->sval), buf);
			object_delete(&obj);

			// clear artifact use
			for (int j = 0; z_info && j < z_info->a_max; j++)
				mark_artifact_created(&a_info[j], false);
		}
	}
	ok;
}

static int test_kinds(void *state) {
	struct object *obj;
	char buf[256];
	char o_name[256];
	char fname[256];

	for(int i=0;i<z_info->k_max;i++) {
		if (k_info[i].name) {
			char *name = k_info[i].name;
			if ((*name == '<') && (name[strlen(name)-1] == '>'))
				continue;
			if (kf_has(k_info[i].kind_flags, KF_INSTA_ART))
				continue;
			if (kf_has(k_info[i].kind_flags, KF_SPECIAL_GEN))
				continue;
			obj_desc_name_format(fname, sizeof(fname), 0, k_info[i].name, NULL, false);
			obj = wish(fname, 1);
			notnull(obj);
			object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL | ODESC_SPOIL, player);
			strnfmt(buf, sizeof(buf), "Asked for base item %s, got %s (%s)", fname, o_name, obj->kind->name);
			vnull(obj->ego[0], buf);
			vnull(obj->artifact, buf);
			vptreq(obj->kind, k_info+i, buf);
			object_delete(&obj);
		}
	}
	ok;
}

/* Test every single ego / base object combination */  
static int test_egos(void *state) {
	struct object *obj;
	char buf[256];
	char o_name[256];
	char fname[256];
	char egoname[256];

	for(int i=0;i<z_info->e_max;i++) {
		if (e_info[i].name) {
			struct poss_item *poss = e_info[i].poss_items;
			while (poss) {
				struct object_kind *k = k_info + poss->kidx;
				if (!kf_has(k->kind_flags, KF_INSTA_ART) && (!kf_has(k->kind_flags, KF_SPECIAL_GEN))) {
					obj_desc_name_format(fname, sizeof(fname), 0, k->name, NULL, false);
					strnfmt(egoname, sizeof(egoname), "%s %s", e_info[i].name, fname);
					obj = wish(egoname, 1);
					notnull(obj);
					object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL | ODESC_SPOIL, player);
					strnfmt(buf, sizeof(buf), "Asked for %s, got %s", egoname, o_name);
					vptreq(obj->ego[0], e_info+i, buf);
					vnull(obj->ego[1], buf);
					vnull(obj->artifact, buf);
					vptreq(obj->kind, k, buf);
					object_delete(&obj);
				}
				poss = poss->next;
			};
		}
	}
	ok;
}

/* Test all single egos without a base object */
static int test_ego_only(void *state) {
	struct object *obj;
	char buf[256];
	char o_name[256];
	char ename[256];
	char fname[256];

	for(int i=0;i<z_info->e_max;i++) {
		bool eok = true;
		if (e_info[i].name) {
			/* Avoid reasonable ambiguity (e.g. combat pill, vs. combat ego) */
			obj_desc_name_format(fname, sizeof(fname), 0, e_info[i].name, NULL, false);
			strip_punct(fname);
			for(int j=0;j<z_info->k_max;j++) {
				if (k_info[j].name) {
					obj_desc_name_format(ename, sizeof(ename), 0, k_info[j].name, NULL, false);
					strip_punct(ename);
					if (streq(ename, fname)) {
						eok = false;
						break;
					}
				}
			}

			if (eok) {
				obj = wish(e_info[i].name, 1);
				notnull(obj);
				notnull(obj->kind);
				notnull(obj->ego[0]);
				object_desc(o_name, sizeof(o_name), obj, ODESC_PREFIX | ODESC_FULL | ODESC_SPOIL, player);
				strnfmt(buf, sizeof(buf), "Asked for %s, got %s (%s)", e_info[i].name, obj->ego[0]->name,  o_name);
				vrequire((obj->ego[0]->name), buf);
				obj_desc_name_format(ename, sizeof(ename), 0, obj->ego[0]->name, NULL, false);
				strip_punct(ename);
				vrequire((streq(ename, fname)), buf);
				vnull(obj->ego[1], buf);
				vnull(obj->artifact, buf);
				object_delete(&obj);
			}
		}
	}
	ok;
}

const char *suite_name = "object/wish";
struct test tests[] = {
	{ "ego-only", test_ego_only },
	{ "artifacts", test_artifacts },
	{ "kinds", test_kinds },
	{ "egos", test_egos },
	{ NULL, NULL }
};
