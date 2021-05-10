/* artifact/name */

#include "unit-test.h"
#include "unit-test-data.h"
#include "obj-properties.h"
#include "obj-randart.h"
#include "obj-tval.h"
#include "object.h"

void artifact_set_data_free(struct artifact_set_data *data);
struct artifact_set_data *artifact_set_data_new(void);

int setup_tests(void **state) {
	k_info = mem_zalloc(2 * sizeof(struct object_kind));
	k_info[1] = test_torch;
	z_info = mem_zalloc(sizeof(struct angband_constants));
	z_info->k_max = 2;
	return 0;
}

int teardown_tests(void *state) {
	mem_free(k_info);
	mem_free(z_info);
	return 0;
}

#define NAMES_TRIES	100

const char *names[] = {
	"aaaaaa",
	"bbbbbb",
	"cccccc",
	"dddddd",
	"eeeeee",
	"ffffff",
	"gggggg",
	"hhhhhh",
	"iiiiii",
	"jjjjjj",
	NULL
};

const char **p[] = { names, names };

int test_names(void *state) {
	struct artifact a;
	char *n;
	struct artifact_set_data *data = artifact_set_data_new();
	int i;

	z_info->a_base = 0;
	z_info->a_max = 50;
	z_info->rand_art = 50;

	a.aidx = 1;
	a.tval = TV_LIGHT;
	a.sval = 1;
	a.name = "of Prometheus";

	for (i = 0; i < NAMES_TRIES; i++) {
		n = artifact_gen_name(data, &a, p, 42, TV_LIGHT, false);
		if (strchr(n, '\''))
			require(strchr(n, '\'') != strrchr(n, '\''));
		else
			require(strstr(n, "of "));
		mem_free(n);
	}

	artifact_set_data_free(data);

	ok;
}

const char *suite_name = "artifact/name";
struct test tests[] = {
	{ "names", test_names },
	{ NULL, NULL }
};
