#pragma once

#if __cplusplus
extern "C" {
#endif

#include "../bolt.h"

extern const char* bt_error_type_name;
extern const char* bt_error_what_key_name;

BOLT_API void boltstd_open_core(bt_Context* context);
BOLT_API bt_Value boltstd_make_error(bt_Context* context, const char* message);
BOLT_API bt_Type* boltstd_get_error_type(bt_Context* context);

#if __cplusplus
}
#endif