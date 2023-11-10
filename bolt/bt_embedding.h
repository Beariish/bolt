#pragma once

#include <stdint.h>
#include "bt_object.h"

#if BOLT_INLINE_HEADER
#include "bt_context.h"

static BT_FORCE_INLINE uint8_t bt_argc(bt_Thread* thread)
{
	return thread->native_stack[thread->native_depth - 1].argc;
}

static BT_FORCE_INLINE bt_Value bt_arg(bt_Thread* thread, uint8_t idx)
{
	return thread->stack[thread->top + idx];
}

static BT_FORCE_INLINE void bt_return(bt_Thread* thread, bt_Value value)
{
	thread->stack[thread->top + thread->native_stack[thread->native_depth - 1].return_loc] = value;
}

static BT_FORCE_INLINE bt_Value bt_get_returned(bt_Thread* thread)
{
	return thread->stack[thread->top];
}

static BT_FORCE_INLINE bt_Value bt_getup(bt_Thread* thread, uint8_t idx)
{
	return BT_CLOSURE_UPVALS(BT_STACKFRAME_GET_CALLABLE(thread->callstack[thread->depth - 1]))[idx];
}

static BT_FORCE_INLINE void bt_setup(bt_Thread* thread, uint8_t idx, bt_Value value)
{
	BT_CLOSURE_UPVALS(BT_STACKFRAME_GET_CALLABLE(thread->callstack[thread->depth - 1]))[idx] = value;
}
#else
BOLT_API uint8_t bt_argc(bt_Thread* thread);
BOLT_API bt_Value bt_arg(bt_Thread* thread, uint8_t idx);
BOLT_API void bt_return(bt_Thread* thread, bt_Value value);
BOLT_API bt_Value bt_get_returned(bt_Thread* thread);
BOLT_API bt_Value bt_getup(bt_Thread* thread, uint8_t idx);
BOLT_API void bt_setup(bt_Thread* thread, uint8_t idx, bt_Value value);
#endif