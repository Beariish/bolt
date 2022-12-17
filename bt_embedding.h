#pragma once

#define BT_ARGC(thread) (thread->callstack[thread->depth - 1].argc)
#define BT_ARG(thread, idx) (thread->stack[thread->top + idx])
#define BT_RETURN(thread, val) (thread->stack[thread->top + thread->callstack[thread->depth - 1].return_loc] = val); return