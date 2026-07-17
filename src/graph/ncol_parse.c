/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "graph/ncol_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool parse_ncol_line(char *line, char **name1, char **name2, bool *has_weight, double *weight)
{
	if (line[0] == '\0')
		return false; // blank line: silently skipped

	char *saveptr = NULL;
	char *tok1 = strtok_r(line, " \t", &saveptr);
	if (!tok1)
		return false; // whitespace-only line: silently skipped

	char *tok2 = strtok_r(NULL, " \t", &saveptr);
	if (!tok2) {
		fprintf(stderr, "graph_stream: malformed NCOL line (need at least 2 fields): \"%s\"\n", tok1);
		return false;
	}

	const char *tok3 = strtok_r(NULL, " \t", &saveptr);
	double w = 0.0;
	bool hw = false;
	if (tok3) {
		char *end = NULL;
		w = strtod(tok3, &end);
		if (end == tok3 || *end != '\0') {
			fprintf(stderr, "graph_stream: malformed weight field \"%s\", skipping line\n", tok3);
			return false;
		}
		hw = true;
		if (strtok_r(NULL, " \t", &saveptr)) {
			fprintf(stderr, "graph_stream: too many fields in NCOL line \"%s %s ...\", skipping\n", tok1, tok2);
			return false;
		}
	}

	*name1 = tok1;
	*name2 = tok2;
	*has_weight = hw;
	*weight = w;
	return true;
}
