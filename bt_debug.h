#pragma once

#include "bt_parser.h"
#include "bt_compiler.h"

void bt_debug_print_parse_tree(bt_Parser* parser);
void bt_debug_print_module(bt_Context* ctx, bt_Module* module);
void bt_debug_print_fn(bt_Context* ctx, bt_Fn* module);