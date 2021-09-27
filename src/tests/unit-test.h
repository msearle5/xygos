/* unit-test.h */

#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include "unit-test-types.h"
#include "z-util.h"

#define TEST

extern int verbose;

extern int showpass(void);
extern int showfail(void);

/* Forward declaration for string provided by the test case but expected by
 * unit-test.c and the macros declared here.
 */
extern const char *suite_name;

/* Forward declaration for the test case array provided by the test case but
 * expected by unit-test.c.
 */
extern struct test tests[];

/* Provided by the test case and called by unit-test.c.  If a test case
 * does not need setup or teardown use the NOSETUP or NOTEARDOWN macros
 * in the test case to provide the functions unit-test.c wants.
 */
extern int setup_tests(void **data);
extern int teardown_tests(void *data);
#define NOSETUP int setup_tests(void **data) { return 0; }
#define NOTEARDOWN int teardown_tests(void *data) { return 0; }

#define ok return showpass();

#define eq(x,y) \
	if ((x) != (y)) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == '%s' failed\n", suite_name, \
		           __LINE__, #x, #y); \
			printf("      %s: %lld\n", #x, (long long)x); \
			printf("      %s: %lld\n", #y, (long long)y); \
		} \
		return 1; \
	}

#define noteq(x,y) \
	if ((x) == (y)) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' != '%s' failed\n", suite_name, \
		           __LINE__, #x, #y); \
			printf("      %s: %lld\n", #x, (long long)x); \
			printf("      %s: %lld\n", #y, (long long)y); \
		} \
		return 1; \
	}

#define require(x) \
	do { \
		if (!(x)) { \
			if (verbose) { \
				showfail(); \
				printf("    %s:%d: requirement '%s' failed\n", \
			           suite_name, __LINE__, #x); \
			} \
			return 1; \
		} \
	} while (0)

#define vrequire(x, s) \
	do { \
		if (!(x)) { \
			if (verbose) { \
				showfail(); \
				printf("    %s:%d: requirement '%s' failed\n", \
			           suite_name, __LINE__, #x); \
				printf("    %s\n", s ? s : "(null)"); \
			} \
			return 1; \
		} \
	} while (0)

#if __WORDSIZE == 64
#define vptreq(x,y,s) \
	if ((x) != (y)) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == '%s' failed\n", suite_name, \
		           __LINE__, #x, #y); \
		    printf("      %s\n", s ? s : "(null)"); \
			printf("      %s: %llu\n", #x, (unsigned long long)(x)); \
			printf("      %s: %llu\n", #y, (unsigned long long)(y)); \
		} \
		return 1; \
	}

#define ptreq(x,y) \
	if ((x) != (y)) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == '%s' failed\n", suite_name, \
		           __LINE__, #x, #y); \
			printf("      %s: %llu\n", #x, (unsigned long long)(x)); \
			printf("      %s: %llu\n", #y, (unsigned long long)(y)); \
		} \
		return 1; \
	}

#define vnull(x,s) \
	if ((x) != 0) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == NULL failed\n", suite_name, \
		           __LINE__, #x); \
		    printf("      %s\n", s ? s : "(null)"); \
			printf("      %s: %llu\n", #x, (unsigned long long)(x)); \
		} \
		return 1; \
	}

#define vnotnull(x,s) \
	if ((x) == 0) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == NULL failed\n", suite_name, \
		           __LINE__, #x); \
		    printf("      %s\n", s ? s : "(null)"); \
			printf("      %s: %llu\n", #x, (unsigned long long)(x)); \
		} \
		return 1; \
	}

#define null(x) \
	if ((x) != 0) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == NULL failed\n", suite_name, \
		           __LINE__, #x); \
			printf("      %s: %llu\n", #x, (unsigned long long)(x)); \
		} \
		return 1; \
	}

#define notnull(x) \
	if ((x) == 0) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' != NULL failed\n", suite_name, \
		           __LINE__, #x); \
			printf("      %s: %llu\n", #x, (unsigned long long)(x)); \
		} \
		return 1; \
	}

#else
#define vptreq(x,y,s) \
	if ((x) != (y)) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == '%s' failed\n", suite_name, \
		           __LINE__, #x, #y); \
		    printf("      %s\n", s ? s : "(null)"); \
			printf("      %s: %p\n", #x, (void *)(x)); \
			printf("      %s: %p\n", #y, (void *)(y)); \
		} \
		return 1; \
	}

#define ptreq(x,y) \
	if ((x) != (y)) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == '%s' failed\n", suite_name, \
		           __LINE__, #x, #y); \
			printf("      %s: %p\n", #x, (void *)(x)); \
			printf("      %s: %p\n", #y, (void *)(y)); \
		} \
		return 1; \
	}

#define vnull(x) \
	if ((x) != 0) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == NULL failed\n", suite_name, \
		           __LINE__, #x); \
		    printf("      %s\n", s ? s : "(null)"); \
			printf("      %s: %p\n", #x, (void *)(x)); \
		} \
		return 1; \
	}

#define vnotnull(x) \
	if ((x) == 0) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == NULL failed\n", suite_name, \
		           __LINE__, #x); \
		    printf("      %s\n", s ? s : "(null)"); \
			printf("      %s: %p\n", #x, (void *)(x)); \
		} \
		return 1; \
	}

#define null(x) \
	if ((x) != 0) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' == NULL failed\n", suite_name, \
		           __LINE__, #x); \
			printf("      %s: %p\n", #x, (void *)(x)); \
		} \
		return 1; \
	}

#define notnull(x) \
	if ((x) == 0) { \
		if (verbose) { \
			showfail(); \
			printf("    %s:%d: requirement '%s' != NULL failed\n", suite_name, \
		           __LINE__, #x); \
			printf("      %s: %p\n", #x, (void *)(x)); \
		} \
		return 1; \
	}

#endif

#endif /* !UNIT_TEST_H */
