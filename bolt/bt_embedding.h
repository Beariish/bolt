#pragma once

#if __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "bt_object.h"

BOLT_API uint8_t bt_argc(bt_Thread* thread);
BOLT_API bt_Value bt_arg(bt_Thread* thread, uint8_t idx);
BOLT_API void bt_return(bt_Thread* thread, bt_Value value);
BOLT_API bt_Value bt_get_returned(bt_Thread* thread);
BOLT_API bt_Value bt_getup(bt_Thread* thread, uint8_t idx);
BOLT_API void bt_setup(bt_Thread* thread, uint8_t idx, bt_Value value);

#if __cplusplus
}
#endif