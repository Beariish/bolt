#pragma once

#if __cplusplus
extern "C" {
#endif

#include "../bolt.h"

extern const char* bt_error_type_name;
extern const char* bt_error_what_key_name;

void BOLT_API boltstd_open_core(bt_Context* context);
bt_Value BOLT_API boltstd_make_error(bt_Context* context, const char* message);

#if __cplusplus
}
#endif