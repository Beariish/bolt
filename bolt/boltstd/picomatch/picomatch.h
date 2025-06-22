#pragma once
/**
 * Copyright (C) 2025 Beariish
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the “Software”), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A completely pre-compiled regular expression.
 * Avoid interacting with the structure's fields directly, and use the API instead.
 * The actual compiled code is stored as a VLA off the end of the structure.
 */
typedef struct pm_Regex {
    const char* err;
    int size, capacity;
    int num_groups;
    int is_anchored;
} pm_Regex;

/**
 * A group of captured characters after completing a match, expected to be allocated and zeroed beforehand.
 * `start` and ´length` are to be treated as character indices into the source string
 * The correct number of groups to allocate can be found through `pm_getgroups`
 * The 0th group will always contain the full match
 */
typedef struct pm_Group {
    int start;
    int length;
} pm_Group;

/** Returns the required allocation size for an expression in bytes. If an error is encountered, set `err` */
int pm_expsize(const char* source, const char** err);

/** Attempts to compile `source` into `result`. Returns 0 if compilation fails. */
int pm_compile(pm_Regex* result, int result_size, const char* source);

/** If `pm_compile` fails, this returns an error describing the problem. Else, return NULL. */
const char* pm_geterror(pm_Regex* expr);

/** Returns the total number of capture groups needed to evaluate an expression fully */
int pm_getgroups(pm_Regex* expr);

/**
 * Matches `expr` against `len` characters of `source` and fills `groups` along the way, up to `group_count`.
 * If a `len` of 0 is provided, picomatch measures the string before matching.
 * Ideal group count can be found from `pm_group_count(expr)`.
 * Groups are expected to be zeroed out before calling.
 * Passing groups of `NULL` if you don't care about them is fine. 
 * If `remainder` is non-NULL, it's set to the index *after* the last matched character, or 0 if the entire string was consumed.
 * Returns the number of successful matches.
 */
int pm_match(pm_Regex* expr, const char* source, int len, pm_Group* groups, int group_count, int* remainder);

#ifdef PM_DEBUG
/**
 * Dumps the compiled code of `expr` to stdout for debugging purposes
 * Always call with `pc = 0` and `depth = 0`, they are used recursively
 */
void pm_debugprint(pm_Regex* expr, int pc, int depth);
#endif

#ifdef __cplusplus
}
#endif
