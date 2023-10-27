#include "bt_embedding.h"
#include "bt_context.h"

#if !BOLT_INLINE_HEADER
BOLT_API uint8_t bt_argc(bt_Thread* thread)
{
	return thread->callstack[thread->depth - 1].argc;
}

BOLT_API bt_Value bt_arg(bt_Thread* thread, uint8_t idx)
{
	return thread->stack[thread->top + idx];
}

BOLT_API void bt_return(bt_Thread* thread, bt_Value value)
{
	thread->stack[thread->top + thread->callstack[thread->depth - 1].return_loc] = value;
}

BOLT_API bt_Value bt_get_returned(bt_Thread* thread)
{
	return thread->stack[thread->top];
}

BOLT_API bt_Value bt_getup(bt_Thread* thread, uint8_t idx)
{
	return BT_CLOSURE_UPVALS(thread->callstack[thread->depth - 1].callable)[idx];
}

BOLT_API void bt_setup(bt_Thread* thread, uint8_t idx, bt_Value value)
{
	BT_CLOSURE_UPVALS(thread->callstack[thread->depth - 1].callable)[idx] = value;
}
#endif