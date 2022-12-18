#pragma once

#include <stdint.h>
#include "bt_object.h"

static __forceinline uint8_t bt_argc(bt_Thread* thread)
{
	return thread->callstack[thread->depth - 1].argc;
}

static __forceinline bt_Value bt_arg(bt_Thread* thread, uint8_t idx)
{
	return thread->stack[thread->top + idx];
}

static __forceinline void bt_return(bt_Thread* thread, bt_Value value)
{
	thread->stack[thread->top + thread->callstack[thread->depth - 1].return_loc] = value;
}