/**
 * \file bones.c
 * \brief Bones file handling
 *
 * Copyright (c) 2021 Mike Searle
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
#include "bones.h"
#include "buildid.h"
#include "datafile.h"
#include "init.h"
#include "mon-init.h"
#include "mon-util.h"
#include "monster.h"
#include "parser.h"

/* A bonefile starts with 4 unsigned longs.
	This is because Nethack does it, I'm hoping to be able to use Hearse
	some day with as little modification as possible.
	Unsigned long's size may be 4 or 8, and the byte order may vary.
	* On NH this is a feature (the rest of the file format cares).
	Here, the rest of the file is text.
	The longs are parsed by the Hearse server independently of length,
	as a MD5 of the values printed as text. Meaning that a 64-bit file needs
	the high word to be binary zero (and so not printable), and preventing
	64-bit files from being byte-order independent.
	
	A 32-bit structure is followed by 16 newlines to make it easier to skip
	the header (and to distinguish 32 from 64-bit).
	On 64-bit builds, the text follows the header without any newlines.
	
	Header words' format follows:
*/

struct version_info {
	unsigned long   incarnation;    /* actual version number */
	unsigned long   feature_set;    /* bitmask of config settings */
	unsigned long   entity_count;   /* # of monsters and objects */
	unsigned long   struct_sizes;   /* size of key structs */
};

union bones_header {
	struct version_info version;
	u32b val_32[8];
	unsigned long val_long[4];
	char pad[32];
};

/* True if this is a 64 bit long system */
static bool host_64(void) {
	return (sizeof(unsigned long) == 8);
}

/* Byte swap a 32-bit unsigned int */
static u32b bswap_32(u32b in)
{
	return  ((in >> 24) & 0xff) |
			((in >> 8)  & 0xff00) |
			((in << 8)  & 0xff0000) |
			((in << 24) & 0xff000000);
}

/* Create a new bones file header */
static void bones_new(union bones_header *header)
{
	u32b val;
	char buf[16];

	/* Pad with newlines */
	memset(header, '\n', sizeof(*header));
	
	/* Build a 4-char version */
	int major = 1;
	int minor = 0;
	int patch = 0;
	sscanf(VERSION_STRING, "%d.%d.%d", &major, &minor, &patch);
	assert(major < 16);
	assert(minor < 16);
	assert(patch < 10);
	sprintf(buf, "%x%x%d", major, minor, patch);
	val = (VERSION_STRING[0] << 24);
	val |= buf[0] << 16;
	val |= buf[1] << 8;
	val |= buf[2];
	header->version.incarnation = val;

	/* Config settings */
	val = 0;
	header->version.feature_set = val;
	
	/* Entity count */
	header->version.entity_count = val;
	
	/* Struct sizes */
	header->version.struct_sizes = val;
}

/* Compare two bones structures for equality.
 * At this point, both are in host length and byte order, so nothing
 * special needs to be done.
 */
static bool bones_eq(const struct version_info *a, const struct version_info *b)
{
	return (!memcmp(a, b, sizeof(*a)));
}

/* Convert a bones file header into host compatible form.
 * This relies on:
 * 		The version number is always 4 characters of which the first is
 * 		alphabetic, and the last is numeric.
 */
static void bones_normalise(union bones_header *dst, const union bones_header *src)
{
	/* Clear */
	memset(dst, '\n', sizeof(*dst));

	/* Is it from 32 or 64-bit? */
	bool from64 = false;
	for(int i=16;i<32;i++) {
		if (src->pad[i] != '\n') {
			from64 = true;
			break;
		}
	}

	/* Reading 32 bit header on a 64-bit system - expand */
	if (!from64 && host_64()) {
		for(int i=0;i<4;i++)
			dst->val_long[i] = src->val_32[i];
	}

	/* Reading 64-bit header on a 32=bit system - shrink */
	else if (from64 && !host_64()) {
		for(int i=0;i<4;i++) {
			/* This should work independently of byteorder so long as
			 * the top 4 bytes of a 64-bit long are always 0
			 */
			dst->val_32[i] = src->val_32[(i * 2)] | src->val_32[(i * 2)+1];
		}
	}

	/* Length matches */
	else {
		memcpy(&dst->version, &src->version, sizeof(dst->version));
	}

	/* Header is now in native length, unknown byte order.
	 * If the last byte of the version isn't a digit, swap it.
	 * Always swap the low 32 bits only.
	 */
	bool wrongendian = (!isdigit(dst->version.incarnation & 0xff));
	if (wrongendian) {
		for(int i=0;i<4;i++)
			dst->val_long[i] = bswap_32(dst->val_long[i]);
	}
}

/* Parse a bones monster's name */
static enum parser_error parse_bones_name(struct parser *p) {
	struct monster_race *r = lookup_monster("Randy, the Random");
	if (!r)
		return PARSE_ERROR_MISSING_RECORD_HEADER;

	/* Clear up the previous monster */
	cleanup_one_monster(r);
	memset(r, 0, sizeof *r);

	r->name = string_make(parser_getstr(p, "name"));
	parser_setpriv(p, r);
	return PARSE_ERROR_NONE;
}

/* Read a bones file.
 * This replaces the current Randy Random monster with a new one, read from
 * the file.
 * It returns a parse error code - possible results are:
 *		The file was read successfully.
 * 		OS error (failed file read)
 * 		Header mismatch
 * 		Parse failure
 * They should be distinguished, as the response must differ.
 */
errr bones_read(const char *filename)
{
	char path[1024];
	char buf[1024];
	ang_file *fh;
	errr r = 0;
	
	/* Build the bones parser (based on the monster parser, but it always
	 * rewrites Randy, the Random instead of building a list)
	 **/
	static struct parser *p = NULL;
	if (!p) {
		p = init_parse_monster();
		parser_reg(p, "name str name", parse_bones_name);
	}
	
	/* Look for it in the bones directory */
	path_build(path, sizeof(path), ANGBAND_DIR_BONES, filename);

	if (!file_exists(filename))
		return PARSE_ERROR_NO_FILE_FOUND;

	/* Read the header */
	fh = file_open(path, MODE_READ, FTYPE_RAW);

	/* File wasn't found, return the error */
	if (!fh)
		return PARSE_ERROR_NO_FILE_FOUND;
	
	/* Check the header */
	union bones_header thisversion, normversion, readversion;
	if (file_read(fh, (char *)&readversion, sizeof(union bones_header)) != sizeof(union bones_header)) {
		file_close(fh);
		return PARSE_ERROR_NO_FILE_FOUND;
	}
	bones_normalise(&normversion, &readversion);
	bones_new(&thisversion);
	file_close(fh);

	if (!bones_eq(&thisversion.version, &normversion.version))
		return PARSE_ERROR_OBSOLETE_FILE;

	/* Read the rest */
	fh = file_open(path, MODE_READ, FTYPE_TEXT);
	if (!fh)
		return PARSE_ERROR_NO_FILE_FOUND;
	if (!file_skip(fh, sizeof(union bones_header)))
		return PARSE_ERROR_NO_FILE_FOUND;

	/* Parse it */
	while (file_getl(fh, buf, sizeof(buf))) {
		r = parser_parse(p, buf);
		if (r)
			break;
	}
	file_close(fh);
	return r;
}

