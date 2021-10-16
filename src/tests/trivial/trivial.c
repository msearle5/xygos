/* trivial/trivial.c */

#include "unit-test.h"

NOSETUP
NOTEARDOWN

static int test_empty(void *state)
{
	(void)state;
	ok;
}

static int test_require(void *state)
{
	(void)state;
	require(1);
	ok;
}

const char *suite_name = "trivial/trivial";
struct test tests[] = {
	{ "empty", test_empty },
	{ "require", test_require },
	{ NULL, NULL },
};
