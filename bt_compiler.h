#pragma once

#include "bt_prelude.h"
#include "bt_op.h"

#include "bt_parser.h"

typedef struct bt_Compiler {
	bt_Context* context;
	bt_Parser* input;
} bt_Compiler;

bt_Compiler bt_open_compiler(bt_Parser* parser);
void bt_close_compiler(bt_Compiler* compiler);

bt_Module* bt_compile(bt_Compiler* compiler);