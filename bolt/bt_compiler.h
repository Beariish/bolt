#pragma once

#if __cplusplus
extern "C" {
#endif

#include "bt_prelude.h"
#include "bt_parser.h"

typedef struct bt_CompilerOptions {
	bt_bool generate_debug_info;
	bt_bool accelerate_arithmetic;
	bt_bool allow_method_hoisting;
	bt_bool predict_hash_slots;
	bt_bool typed_array_subscript;
} bt_CompilerOptions;

typedef struct bt_Compiler {
	bt_CompilerOptions options;

	bt_AstNode* debug_stack[128];
	uint32_t debug_top;

	bt_Context* context;
	bt_Parser* input;

	bt_bool has_errored;
} bt_Compiler;

BOLT_API bt_Compiler bt_open_compiler(bt_Parser* parser, bt_CompilerOptions options);
BOLT_API void bt_close_compiler(bt_Compiler* compiler);

BOLT_API bt_Module* bt_compile(bt_Compiler* compiler);

#if __cplusplus
}
#endif