#pragma once

#include <stdint.h>
#include "bt_object.h"

static BT_FORCE_INLINE uint8_t bt_argc(bt_Thread* thread)
{
	return thread->callstack[thread->depth - 1].argc;
}

static BT_FORCE_INLINE bt_Value bt_arg(bt_Thread* thread, uint8_t idx)
{
	return thread->stack[thread->top + idx];
}

static BT_FORCE_INLINE void bt_return(bt_Thread* thread, bt_Value value)
{
	thread->stack[thread->top + thread->callstack[thread->depth - 1].return_loc] = value;
}

static BT_FORCE_INLINE bt_Value bt_getup(bt_Thread* thread, uint8_t idx)
{
	return BT_CLOSURE_UPVALS(thread->callstack[thread->depth - 1].callable)[idx];
}

static BT_FORCE_INLINE void bt_setup(bt_Thread* thread, uint8_t idx, bt_Value value)
{
	BT_CLOSURE_UPVALS(thread->callstack[thread->depth - 1].callable)[idx] = value;
}