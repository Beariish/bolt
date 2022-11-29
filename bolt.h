#pragma once

#include "bt_buffer.h"
#include "bt_tokenizer.h"
#include "bt_context.h"

void bt_open(bt_Context* context, bt_Alloc allocator, bt_Free free);
void bt_close(bt_Context* context);
