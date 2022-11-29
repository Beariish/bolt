#pragma once

#include "bt_parser.h"
#include "bt_compiler.h"

void bt_debug_print_parse_tree(bt_Parser* parser);
void bt_debug_print_compiler_output(bt_Compiler* compiler);