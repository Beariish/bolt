#pragma once

#if __cplusplus
extern "C" {
#endif

#include "bt_buffer.h"
#include "bt_tokenizer.h"
#include "bt_context.h"

void bt_open(bt_Context* context, bt_Handlers* handlers);
bt_Handlers bt_default_handlers();
void bt_close(bt_Context* context);

bt_bool bt_run(bt_Context* context, const char* source);
bt_Module* bt_compile_module(bt_Context* context, const char* source, const char* mod_name);

#if __cplusplus
}
#endif