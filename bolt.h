#pragma once

#include "bt_buffer.h"
#include "bt_tokenizer.h"
#include "bt_context.h"

void bt_open(bt_Context* context, bt_Alloc allocator, bt_Realloc realloc, bt_Free free, bt_ErrorFunc error);
void bt_close(bt_Context* context);

bt_bool bt_run(bt_Context* context, const char* source);
bt_Module* bt_compile_module(bt_Context* context, const char* source, const char* mod_name);