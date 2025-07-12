#pragma once

#if __cplusplus
extern "C" {
#endif

/***
 * These functions are intended for use when embedding bolt into a native application.
 * With the exception of `bt_get_returned`, these all need to be called from within a bolt-bound native function,
 * as they manipulate or get information from the current thread's execution context.
 *
 * Argument counts, upvalue slots, and return types are all assumed to be correct according to the definitions provided when
 * binding the source functions, and are the responsibility of the programmer.
 */
	
#include <stdint.h>
#include "bt_object.h"

/**
 * Returns the number of arguments passed to this function invocation.
 * If the function type was declared with a fixed number of parameters, this should always reflect that.
 * In the case of a variadic function signature, this will include all variadic arguments AND all regular arguments.
 */
BOLT_API uint8_t bt_argc(bt_Thread* thread);

/**
 * Gets the argument at `idx` from the current function invocation.
 * It's the caller's responsibility to make sure the index is within bounds and that the resulting value is
 * treated as the same type as specified in the function's signature.
 */
BOLT_API bt_Value bt_arg(bt_Thread* thread, uint8_t idx);

/**
 * Sets the return value for this function in the stack of `thread`.
 * This will *NOT* halt actual execution of the function.
 * Calling this multiple times will not return multiple values, but rather overwrite the same stack index.
 */
BOLT_API void bt_return(bt_Thread* thread, bt_Value value);

/**
 * Returns upvalue at index `idx` in the currently executing closure.
 * It's the caller's responsibility to ensure that the function *is* a closure
 * and that the upvalue index is correct.
 */
BOLT_API bt_Value bt_getup(bt_Thread* thread, uint8_t idx);

/**
 * Sets the upvalue at index `idx` in the currently executing closure to `value`.
 * It's the caller's responsibility to ensure that the function *is* a closure
 * and that the upvalue index is correct.
 */
BOLT_API void bt_setup(bt_Thread* thread, uint8_t idx, bt_Value value);

/** Returns the owning module of the currently executing function */
BOLT_API bt_Module* bt_get_module(bt_Thread* thread);
	
/** Returns the value of the last function executed on `thread`, regardless of whether that function was native or in-language */
BOLT_API bt_Value bt_get_returned(bt_Thread* thread);

#if __cplusplus
}
#endif