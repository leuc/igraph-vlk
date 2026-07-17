/*
 * Copyright 2026 igraph-vlk team
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef GRAPH_NCOL_PARSE_H
#define GRAPH_NCOL_PARSE_H

#include <stdbool.h>

/**
 * Parse one NCOL-format line ("<name1> <name2> [<weight>]") in place.
 * Tokenizes *line* destructively (via strtok_r), so *name1/*name2 point
 * into *line*'s storage. Blank/whitespace-only lines are silently skipped
 * (returns false, no error printed); malformed lines print to stderr and
 * return false.
 * @param line Mutable NCOL line, modified in place by tokenization
 * @param name1 Output: pointer to first name token within *line*
 * @param name2 Output: pointer to second name token within *line*
 * @param has_weight Output: true if a third (weight) field was present
 * @param weight Output: parsed weight value (0.0 if has_weight is false)
 * @return true if the line was successfully parsed into two names
 */
bool parse_ncol_line(char *line, char **name1, char **name2, bool *has_weight, double *weight);

#endif // GRAPH_NCOL_PARSE_H
