#pragma once

#include "bt_parser.h"
#include "bt_compiler.h"

BOLT_API void bt_debug_print_parse_tree(bt_Parser* parser);
BOLT_API void bt_debug_print_module(bt_Context* ctx, bt_Module* module);
BOLT_API void bt_debug_print_fn(bt_Context* ctx, bt_Fn* module);

BOLT_API bt_String* bt_debug_dump_fn(bt_Context* ctx, bt_Callable* function);