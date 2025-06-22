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

#include "picomatch.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define CODE_BASE(r) ((unsigned char*)(r) + sizeof(pm_Regex))

static const char* control_chars = "|.^$*+?()[]{}\\";
static const char* class_chars = "sSdDwWbB";

enum {
    // Start a capture group, followed by a byte capture group index
    OP_OPENGROUP,
    // End a capture group, followed by a byte capture group index
    OP_CLOSEGROUP,
    // Choose between two matches, followed a byte for the 'else' branch offset, and a byte offset to skip to end
    OP_CHOOSE,
    // Represents a non-capturing group (?:...), followed by a byte offset to continue after the group is complete
    OP_BLOCK,
    // Represents the end of a matching block, or the entire expression
    OP_END,

    // Matches the beginning of the input '^'
    OP_MATCHBOL,
    // Matches the end of the input '$'
    OP_MATCHEOL,

    // Matches any character '.'
    OP_MATCHANY,
    // Matches exact literal characters, followed by a byte length and the characters inline
    OP_MATCHEXACT,
    // Matches against a set of characters, followed by a byte length and entries inline
    OP_MATCHSET,
    // Matches against a set, but inverts the result
    OP_INVMATCHSET,

    // All quantifiers store an additional byte offset to jump to after they've finished evaluating 
    
    // Matches the predicate zero or one times '?'
    OP_ZERO_ONE,
    // Matches the predicate zero or more times '*'
    OP_ZERO_MORE,
    // Matches the predicate one or more times '+'
    OP_ONE_MORE,
    // Matches the predicate zero or more times, lazily '*?'
    OP_ZERO_MORE_LAZY,
    // Matches the predicate one or more times, lazily '+?'
    OP_ONE_MORE_LAZY,
    // Matches the predicate min-max times. Stores a byte for each count, and an offset to skip once done.
    OP_COUNT_RANGE,

    // Pseudo-character indicating that the next two characters are to be treated as a range
    ARG_RANGE = 0xfe,
    // Pseudo-character indicating that the next character is a class predicate
    ARG_CLASS = 0xff
};

static int is_class_char(const char ch) {
    return strchr(class_chars, ch) != NULL;
}

static char get_escaped_char(const char** source) {
    char ch = **source;
    (*source)++;

    switch (ch) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '0': return '\0';
    case '\'': return '\'';
    }

    return ch;
}

static int emit_op(pm_Regex* result, unsigned char op, int measure) {
    if (measure) {
        result->size++;
        return 1;
    } else {
        if (result->size >= result->capacity) {
            result->err = "Failed to emit code, out of space";
            return 0;
        }

        CODE_BASE(result)[result->size++] = op;
        return 1;
    }
}

static int emit_arg(pm_Regex* result, unsigned char arg, int measure) {
    return emit_op(result, arg, measure);
}

static int store_data(pm_Regex* result, char* start, size_t len, size_t* out_idx, int measure) {
    if (measure) {
        result->size += len;
        return 1;
    } else {
        if (result->size + len >= result->capacity) {
            result->err = "Failed to emit data, out of space";
            return 0;
        }

        if (out_idx) *out_idx = result->size;
        char* new_start = (char*)CODE_BASE(result) + result->size;
        memcpy(new_start, start, len);
        result->size += len;
        return 1;
    }
}

static int emit_exact(pm_Regex* result, const char** source, int measure) {
    const char* match = *source;

    while (*match != '\0' && strchr(control_chars, *(match)++) == NULL) {}
    if (*match) match--;
    else {
        match--;
        if (strchr(control_chars, *(match)) == NULL) { match++;}
    }

    size_t n_matched = match - (*source);

    size_t start_idx = 0;
    if (!emit_op(result, OP_MATCHEXACT, measure)) return 0;
    if (!emit_arg(result, (unsigned char)n_matched, measure)) return 0;
    if (!store_data(result, *source, n_matched, &start_idx, measure)) return 0;

    *source = match;
    return 1;
}

static int set_jump_from(pm_Regex* result, size_t pc, int from) {
    int offset = result->size - from;
    if (offset > 0xff) {
        result->err = "Jump offset is too large";
        return 0;
    } else {
        CODE_BASE(result)[pc] = offset;
        return 1;
    }
}

static int emit_branch_end(pm_Regex* result, size_t branch_loc, int measure) {
    if (branch_loc > 0) {
        if (!emit_op(result, OP_END, measure)) return 0;
        if (!measure) {
            if (!set_jump_from(result, branch_loc, branch_loc - 2)) return 0;
        }
    }

    return 1;
}

static int shift_branch(pm_Regex* result, size_t start_loc, size_t shift_amount, int measure) {
    if (!emit_op(result, OP_END, measure)) return 0;
    if (!measure) {
        if (result->size + shift_amount >= result->capacity) {
            result->err = "Failed to shift branch, out of space";
            return 0;
        }

        memmove(CODE_BASE(result) + start_loc + shift_amount, CODE_BASE(result) + start_loc, result->size - start_loc);
    }

    result->size += shift_amount;
    return 1;
}

static int emit_set(pm_Regex* result, const char** source, int measure) {
    int op = OP_MATCHSET;

    if (**source == '^') {
        op = OP_INVMATCHSET;
        (*source)++;
    }

    if (!emit_op(result, op, measure)) return 0;
    size_t arg_idx = result->size;
    if (!emit_arg(result, 0, measure)) return 0;
    
    while (**source) {
        char ch = **source;
        (*source)++;

        switch (ch) {
        case ']':
            if (!measure) CODE_BASE(result)[arg_idx] = result->size - arg_idx - 1;
            return 1;
        case '\\':
            if (is_class_char(**source)) {
                if (!emit_arg(result, ARG_CLASS, measure)) return 0;
                if (!emit_arg(result, **source, measure)) return 0;
                (*source)++;
            } else {
                if (!emit_arg(result, get_escaped_char(source), measure)) return 0;
            }
            break;
        case '-':
            char last_ch = (!measure) ? CODE_BASE(result)[result->size - 1] : 0;
            char next_ch = **source;
            (*source)++;

            if (!measure && next_ch <= last_ch) {
                result->err = "Expected right side of range to be larger";
                return 0;
            }

            if (!measure) CODE_BASE(result)[result->size - 1] = ARG_RANGE;
            if (!emit_arg(result, last_ch, measure)) return 0;
            if (!emit_arg(result, next_ch, measure)) return 0;
            break;
        default:
            if (!emit_arg(result, ch, measure)) return 0;
            break;
        }
    }

    result->err = "Unclosed set, expected ']'";
    return 0;
}

static int emit_quantifier(pm_Regex* result, int last_emitted, int quant, int measure) {
    if (measure) {
        result->size += 5;
        return 1;
    }

    if (CODE_BASE(result)[last_emitted] == OP_MATCHEXACT && CODE_BASE(result)[last_emitted + 1] > 1) {
        CODE_BASE(result)[last_emitted + 1] -= 1;
        char replacing = CODE_BASE(result)[result->size - 1];

        result->size--;
        last_emitted = result->size;
        
        if (!emit_op(result, OP_MATCHEXACT, measure)) return 0;
        if (!emit_arg(result, 1, measure)) return 0;
        if (!emit_arg(result, replacing, measure)) return 0;
    }

    if (!shift_branch(result, last_emitted, 2, measure)) return 0;
    CODE_BASE(result)[last_emitted] = quant;
    if (!set_jump_from(result, last_emitted + 1, last_emitted)) return 0;
    return 1;
}

static int emit_range_quantifier(pm_Regex* result, const char** source, int last_emitted, int measure) {
    char* numend = NULL;
    int range_start = strtol(*source, &numend, 10);
    if (numend == (*source)) {
        result->err = "Invalid start of range";
        return 0;
    }
    *source = numend;
    int range_end = 0;
    if (**source == ',') {
        (*source)++;
        if (**source != '}') {
            range_end = strtol(*source, &numend, 10);
            if (numend == (*source)) {
                result->err = "Invalid end of range";
                return 0;
            }
            *source = numend;
        }
    }

    if (**source != '}') {
        result->err = "Expected '}'";
        return 0;
    }

    (*source)++;

    if (measure) {
        result->size += 7;
        return 1;
    }

    if (CODE_BASE(result)[last_emitted] == OP_MATCHEXACT && CODE_BASE(result)[last_emitted + 1] > 1) {
        CODE_BASE(result)[last_emitted + 1] -= 1;
        char replacing = CODE_BASE(result)[result->size - 1];

        result->size--;
        last_emitted = result->size;
        
        if (!emit_op(result, OP_MATCHEXACT, measure)) return 0;
        if (!emit_arg(result, 1, measure)) return 0;
        if (!emit_arg(result, replacing, measure)) return 0;
    }

    if (!shift_branch(result, last_emitted, 4, measure)) return 0;
    CODE_BASE(result)[last_emitted] = OP_COUNT_RANGE;
    CODE_BASE(result)[last_emitted + 1] = range_start;
    CODE_BASE(result)[last_emitted + 2] = range_end;
    if (!set_jump_from(result, last_emitted + 3, last_emitted)) return 0;
    return 1;
}

static int compile_internal(pm_Regex* result, const char** source, int in_block, int measure) {
    size_t last_emitted = result->size;
    size_t branch_start = result->size;
    size_t branch_fix = 0;
    int depth = result->num_groups;
    
    while (1) {
        char ch = **source;
        (*source)++;

        switch (ch) {
        case '\0':
            (*source)--;
            return 1;
        case '^':
            if (!emit_op(result, OP_MATCHBOL, measure)) return 0;
            break;
        case '$':
            if (!emit_op(result, OP_MATCHEOL, measure)) return 0;
            break;
        case '.':
            last_emitted = result->size;
            if (!emit_op(result, OP_MATCHANY, measure)) return 0;
            break;
        case '[':
            last_emitted = result->size;
            if (!emit_set(result, source, measure)) return 0;
            break;
        case '(':
            last_emitted = result->size;
            if (**source && **source == '?' && *(*source + 1) == ':') {
                (*source) += 2;
                if (!emit_op(result, OP_BLOCK, measure)) return 0;
                int block_start = result->size;
                if (!emit_arg(result, 0, measure)) return 0;
                if (!compile_internal(result, source, 1, measure)) return 0;
                if (!emit_op(result, OP_END, measure)) return 0;
                if (!measure && !set_jump_from(result, block_start, block_start - 1)) return 0;
                if (**source != ')') {
                    result->err = "Expected ')'";
                    return 0;
                }
                (*source)++;
            } else {
                if (in_block) {
                    result->err = "Unexpected group inside non-capturing group";
                    return 0;
                }
                int capture_idx = result->num_groups++;
                if (!emit_op(result, OP_OPENGROUP, measure)) return 0;
                if (!emit_arg(result, capture_idx, measure)) return 0;

                if (!compile_internal(result, source, 0, measure)) return 0;
                if (**source != ')') {
                    result->err = "Expected ')'";
                    return 0;
                }
                (*source)++;

                if (!emit_op(result, OP_CLOSEGROUP, measure)) return 0;
                if (!emit_arg(result, capture_idx, measure)) return 0;
            }
            
            break;
        case ')':
            (*source)--;
            if (!emit_branch_end(result, branch_fix, measure)) return 0;
            if (!in_block) {
                if (depth == 1) {
                    result->err = "Missing opening parenthesis";
                    return 0;
                }
            }
            return 1;
        case '|':
            if (!emit_branch_end(result, branch_fix, measure)) return 0;
            if (!shift_branch(result, branch_start, 3, measure)) return 0; // 3 for BRANCH, else, skip
            CODE_BASE(result)[branch_start] = OP_CHOOSE;
            if (!set_jump_from(result, branch_start + 1, branch_start)) return 0;
            branch_fix = branch_start + 2;
            break;
        case '\\':
            last_emitted = result->size;
            if (is_class_char(**source)) {
                if (!emit_op(result, ARG_CLASS, measure)) return 0;
                if (!emit_arg(result, **source, measure)) return 0;
                (*source)++;
            } else {
                if (!emit_op(result, OP_MATCHEXACT, measure)) return 0;
                if (!emit_arg(result, (unsigned char)1, measure)) return 0;
                if (!emit_arg(result, get_escaped_char(source), measure)) return 0;
            }
            break;
        case '+':
        case '*':
            int quant = *(*source - 1) == '+' ? OP_ONE_MORE : OP_ZERO_MORE;
            if (**source == '?') {
                (*source)++;
                quant = quant == OP_ONE_MORE ? OP_ONE_MORE_LAZY : OP_ZERO_MORE_LAZY;
            }
            if (!emit_quantifier(result, last_emitted, quant, measure)) return 0;
            break;
        case '?':
            if (!emit_quantifier(result, last_emitted, OP_ZERO_MORE, measure)) return 0;
            break;
        case '{':
            if (!emit_range_quantifier(result, source, last_emitted, measure)) return 0;
            break;
        default:
            (*source)--;
            last_emitted = result->size;
            if (!emit_exact(result, source, measure)) return 0;
            break;
        }
    }
}

static int compile_body(pm_Regex* result, const char* source, int measure) {
    if (!emit_op(result, OP_OPENGROUP, measure)) return 0;
    if (!emit_arg(result, 0, measure)) return 0;

    while (*source) {
        if (!compile_internal(result, &source, 0, measure)) return 0;
    }
    
    if (!emit_op(result, OP_CLOSEGROUP, measure)) return 0;
    if (!emit_arg(result, 0, measure)) return 0;
    
    if (!emit_op(result, OP_END, measure)) return 0;

    return 1;
}

int pm_expsize(const char* source, const char** err) {
    pm_Regex result = { 0 };
    result.num_groups = 1; // Include base match

    if (!compile_body(&result, source, 1)) {
        if (err) *err = result.err;
        return 0;
    }

    return sizeof(pm_Regex) + result.size;
}

int pm_compile(pm_Regex* result, int result_size, const char* source) {
    memset(result, 0, result_size);
    result->num_groups = 1; // Include base match
    result->capacity = result_size - sizeof(pm_Regex);

    if (source == NULL || source[0] == '\0') {
        result->err = "No source string!";
        return 0;
    }

    if (source[0] == '^') {
        result->is_anchored = 1;
    }

    if (!compile_body(result, source, 0)) return 0;

    return 1;
}

const char* pm_geterror(pm_Regex* expr) {
    return expr->err;
}

int pm_getgroups(pm_Regex* expr) {
    return expr->num_groups;
}

static int matches_class(const char* source, char class, int* consume) {
    if (consume) (*consume) += 1;
    char current = *source;
    switch (class) {
    case 's': return isspace(current);
    case 'S': return !isspace(current);
    case 'd': return isdigit(current);
    case 'D': return !isdigit(current);
    case 'w': return isalnum(current) || current == '_';
    case 'W': return !(isalnum(current) || current == '_');
    case 'b':
        if (consume) (*consume) -= 1;
        return (matches_class(source, 'w', 0) && matches_class(source + 1, 'W', 0))
            || (matches_class(source, 'W', 0) && matches_class(source + 1, 'w', 0));
    case 'B':
        return !matches_class(source, 'b', consume);
    }

    return 0;
}

static int match(pm_Regex* expr, int pc, const char* source, int len, int* offset, pm_Group* groups, int group_count, int spec_depth, int spec);

static void match_loop(pm_Regex* expr, int pc, const char* source, int len, int* offset, int spec_depth) {
    int last_matched = *offset;

    while (match(expr, pc + 2, source, len, offset, NULL, 0, spec_depth, 0)) {
        int saved_offset = *offset;
        if (match(expr, pc + CODE_BASE(expr)[pc + 1], source, len, offset, NULL, 0, spec_depth, 1)) {
            last_matched = saved_offset;
        }
        *offset = saved_offset;
    }

    *offset = last_matched;
}

static void match_loop_lazy(pm_Regex* expr, int pc, const char* source, int len, int* offset, int spec_depth) {
    int saved_offset = *offset;

    while (match(expr, pc + 2, source, len, offset, NULL, 0, spec_depth, 0)) {
        saved_offset = *offset;
        if (match(expr, pc + CODE_BASE(expr)[pc + 1], source, len, offset, NULL, 0, spec_depth, 1)) {
            break;
        }
    }

    *offset = saved_offset;
}

static int match_loop_range(pm_Regex* expr, int pc, const char* source, int len, int* offset, int min_count, int max_count, int spec_depth) {
    int last_matched = *offset;
    int num_matched = 0;
    
    while (match(expr, pc + 4, source, len, offset, NULL, 0, spec_depth, 0)) {
        int saved_offset = *offset;
        num_matched++;
        if (num_matched >= max_count && max_count != 0) break;
        if (match(expr, pc + CODE_BASE(expr)[pc + 3], source, len, offset, NULL, 0, spec_depth, 1)) {
            last_matched = saved_offset;
        }
        *offset = saved_offset;
    }

    *offset = last_matched;

    return num_matched >= min_count;
}

static int match(pm_Regex* expr, int pc, const char* source, int len, int* offset, pm_Group* groups, int group_count, int spec_depth, int spec) {
    int result = 1;
    int block_depth = spec_depth;

    while (result) {
        unsigned char* op = CODE_BASE(expr) + pc;
        switch (*op) {
        case OP_END:
            if (block_depth > 0 && spec) block_depth--;
            else return result;
            pc++;
            break;
        case OP_MATCHEXACT:
            result = 0;
            if (*(op + 1) <= len - (*offset) && memcmp(source + (*offset), (op + 2),  *(op + 1)) == 0) {
                *offset += *(op + 1);
                result = 1;
            }

            pc += *(op + 1) + 2;
            break;
        case OP_MATCHANY:
            result = 0;
            if (*offset < len) {
                *offset += 1;
                result = 1;
            }
            pc++;
            break;
        case OP_MATCHSET:
        case OP_INVMATCHSET:
            char current = source[*offset];
            result = (*op) == OP_INVMATCHSET;
            int set_idx = 0;
            while (set_idx < *(op + 1)) {
                unsigned char set_op = *(op + 2 + set_idx);
                if (set_op == ARG_RANGE) {
                    if (current >= *(op + 2 + set_idx + 1) && current <= *(op + 2 + set_idx + 2)) {
                        result = !result;
                        break;
                    }
                    set_idx += 3;
                } else if (set_op == ARG_CLASS) {
                    if (matches_class(source + (*offset), *(op + 2 + set_idx + 1), 0)) {
                        result = !result;
                        break;
                    }
                    set_idx++;
                } else {
                    if (current == set_op) {
                        result = !result;
                        break;
                    }
                    set_idx++;
                }
            }
            if (result) (*offset)++;
            pc += *(op + 1) + 2;
            break;
        case OP_MATCHBOL:
            result = *offset == 0;
            pc++;
            break;
        case OP_MATCHEOL:
            result = *offset == len;
            pc++;
            break;
        case OP_OPENGROUP:
            if (groups && group_count > *(op + 1)) {
                groups[*(op + 1)].start = *offset;
            }
            pc += 2;
            break;
        case OP_CLOSEGROUP:
            if (groups && group_count > *(op + 1)) {
                groups[*(op + 1)].length = (*offset) - groups[*(op + 1)].start;
            }
            pc += 2;
            break;
        case OP_CHOOSE:
            int old_offset = *offset;
            block_depth++;
            result = match(expr, pc + 3, source, len, offset, groups, group_count, block_depth, 0);
            if (result) {
                int new_offset = *offset;
                result = match(expr, pc + *(op + 2), source, len, offset, groups, group_count, block_depth, 0);
                if (result) old_offset = new_offset;
            }
            
            *offset = old_offset;
            if (result == 0) {
                result = match(expr, pc + *(op + 1), source, len, offset, groups, group_count, block_depth, 0);
            }
            block_depth--;
            pc += *(op + 2);
            break;
        case OP_BLOCK:
            block_depth++;
            result = match(expr, pc + 2, source, len, offset, groups, group_count, block_depth, 0);
            pc += *(op + 1);
            break;
        case OP_ZERO_ONE:
            result = 1;
            int saved_offset = *offset;
            if (!match(expr, pc + 2, source, len, offset, groups, group_count, block_depth, 0)) {
                *offset = saved_offset;
            }
            pc += *(op + 1);
            break;
        case OP_ZERO_MORE:
            result = 1;
            match_loop(expr, pc, source, len, offset, block_depth);
            pc += *(op + 1);
            break;
        case OP_ZERO_MORE_LAZY:
            result = 1;
            match_loop_lazy(expr, pc, source, len, offset, block_depth);
            pc += *(op + 1);
            break;
        case OP_COUNT_RANGE:
            result = match_loop_range(expr, pc, source, len, offset, *(op + 1), *(op + 2), block_depth);
            pc += *(op + 3);
            break;
        case OP_ONE_MORE:
            result = match(expr, pc + 2, source, len, offset, groups, group_count, block_depth, 0);
            if (result == 0) break;

            match_loop(expr, pc, source, len, offset, block_depth);
            pc += *(op + 1);
            break;
        case OP_ONE_MORE_LAZY:
            result = match(expr, pc + 2, source, len, offset, groups, group_count, block_depth, 0);
            if (result == 0) break;

            match_loop_lazy(expr, pc, source, len, offset, block_depth);
            pc += *(op + 1);
            break;
        case ARG_CLASS:
            result  = matches_class(source, *(op + 1), offset);
            pc += 2;
            break;
        default:
            pc++;
            break;
        }
    }

    return result;
}

int pm_match(pm_Regex* expr, const char* source, int len, pm_Group* groups, int group_count, int* remainder) {
    if (!source) return 0;
    if (len == 0) {
        len = strlen(source);
    }
    
    if (expr->is_anchored) {
        size_t offset = 0;
        int result = match(expr, 0, source, len, &offset, groups, group_count, 0, 0);
        if (remainder) *remainder = offset;
        return result;
    } else {
        size_t offset = 0;
        int result = 0;
        for (size_t i = 0; i < len && result == 0; i++) {
            offset = i;
            result = match(expr, 0, source, len, &offset, groups, group_count, 0, 0);
        }

        if (remainder) *remainder = offset;
        return result;
    }
}

#ifdef PM_DEBUG
#include <stdio.h>

void pm_debugprint(pm_Regex* expr, int pc, int depth) {
    while (CODE_BASE(expr)[pc] != OP_END) {
        unsigned char* op = CODE_BASE(expr) + pc;
        printf("[%3d] ", pc);
        for (int i = 0; i < depth; i++) {
            printf(" ");
        }
        switch (*op) {
        case OP_MATCHEXACT:
            printf("EXACT %d '%.*s'\n", *(op + 1), *(op + 1), (const char*)(op + 2));
            pc += *(op + 1) + 2;
            break;
        case OP_MATCHANY:
            printf("ANY\n");
            pc++;
            break;
        case OP_MATCHSET:
        case OP_INVMATCHSET:
            if ((*op) == OP_INVMATCHSET) printf("INVSET %d ", *(op + 1));
            else printf("SET %d ", *(op + 1));
            int set_idx = 0;
            while (set_idx < *(op + 1)) {
                unsigned char set_op = *(op + 2 + set_idx);
                if (set_op == ARG_RANGE) {
                    printf("%c-%c", *(op + 2 + set_idx + 1), *(op + 2 + set_idx + 2));
                    set_idx += 3;
                } else if (set_op == ARG_CLASS) {
                    printf("\\%c", *(op + 2 + set_idx + 1));
                    set_idx += 2;
                } else {
                    printf("%c", set_op);
                    set_idx++;
                }
            }
            printf("\n");
            pc += *(op + 1) + 2;
            break;
        case OP_MATCHBOL:
            printf("BOL\n");
            pc++;
            break;
        case OP_MATCHEOL:
            printf("EOL\n");
            pc++;
            break;
        case OP_OPENGROUP:
            printf("OPENGROUP %d\n", *(op + 1));
            pc += 2;
            break;
        case OP_CLOSEGROUP:
            printf("CLOSEGROUP %d\n", *(op + 1));
            pc += 2;
            break;
        case OP_BLOCK:
            printf("BLOCK %d\n", *(op + 1));
            pm_debugprint(expr, pc + 2, depth + 2);
            pc += *(op + 1);
            break;
        case OP_END:
            printf("END\n");
            pc++;
            break;
        case OP_CHOOSE:
            printf("CHOOSE %d %d\n", *(op + 1), *(op + 2));
            pm_debugprint(expr, pc + 3, depth + 2);
            pm_debugprint(expr, pc + *(op + 1), depth + 2);
            pc += *(op + 2);
            break;
        case OP_ZERO_ONE:
            printf("ZERO ONE %d\n", *(op + 1));
            pm_debugprint(expr, pc + 2, depth + 2);
            pc += *(op + 1);
            break;
        case OP_ZERO_MORE:
            printf("ZERO MORE %d\n", *(op + 1));
            pm_debugprint(expr, pc + 2, depth + 2);
            pc += *(op + 1);
            break;
        case OP_ZERO_MORE_LAZY:
            printf("ZERO MORE LAZY %d\n", *(op + 1));
            pm_debugprint(expr, pc + 2, depth + 2);
            pc += *(op + 1);
            break;
        case OP_ONE_MORE:
            printf("ONE MORE %d\n", *(op + 1));
            pm_debugprint(expr, pc + 2, depth + 2);
            pc += *(op + 1);
            break;
        case OP_ONE_MORE_LAZY:
            printf("ONE MORE LAZY %d\n", *(op + 1));
            pm_debugprint(expr, pc + 2, depth + 2);
            pc += *(op + 1);
            break;
        case OP_COUNT_RANGE:
            printf("COUNT RANGE %d %d %d\n", *(op + 1), *(op + 2), *(op + 3));
            pm_debugprint(expr, pc + 4, depth + 2);
            pc += *(op + 3);
            break;
        case ARG_CLASS:
            printf("CLASS \\%c\n", *(op + 1));
            pc += 2;
            break;
        default:
            pc++;
            break;
        }
    }

    printf("[%3d] ", pc);
    for (int i = 0; i < depth; i++) {
        printf(" ");
    }
    printf("END\n");
}
#endif