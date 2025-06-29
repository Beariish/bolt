#pragma once

#if __cplusplus
extern "C" {
#endif

#include "../bolt.h"

extern bt_Type* bt_error_type;
extern bt_Value bt_error_what_key;

void BOLT_API boltstd_open_core(bt_Context* context);
bt_Value BOLT_API boltstd_make_error(bt_Context* context, const char* message);

#if __cplusplus
}
#endif