#pragma once

#include "bt_prelude.h"
#include "bt_op.h"

#include "bt_parser.h"

typedef struct bt_CompilerOptions {
	bt_bool generate_debug_info;
} bt_CompilerOptions;

typedef struct bt_Compiler {
	bt_CompilerOptions options;

	bt_AstNode* debug_stack[128];
	uint32_t debug_top;

	bt_Context* context;
	bt_Parser* input;
} bt_Compiler;

typedef struct bt_Constant {
	bt_StrSlice name;
	bt_Value value;
} bt_Constant;

bt_Compiler bt_open_compiler(bt_Parser* parser, bt_CompilerOptions options);
void bt_close_compiler(bt_Compiler* compiler);

bt_Module* bt_compile(bt_Compiler* compiler);