#pragma once

#if __cplusplus
extern "C" {
#endif

#include "bt_buffer.h"
#include "bt_tokenizer.h"
#include "bt_context.h"

/**
 * Creates and sets up a new bolt context, allocating it with the function provided in `handlers`
 * `bt_default_handlers()` provides a quick way to get started.
 */
BOLT_API void bt_open(bt_Context** context, bt_Handlers* handlers);
/** Create a set of default handlers for bolt, note that the defines set in `bt_config.h` may affect this. */
BOLT_API bt_Handlers bt_default_handlers();
/** Completely frees and shuts down `context`. Frees all allocated objects, and then destroys the allocation itself */
BOLT_API void bt_close(bt_Context* context);

/** Compiles `source` as an anonymous module, then executes it. Returns whether an error was encountered */
BOLT_API bt_bool bt_run(bt_Context* context, const char* source);
/** Compiles `source` as a module named `mod_name`. Returns NULL on error. */
BOLT_API bt_Module* bt_compile_module(bt_Context* context, const char* source, const char* mod_name);

#if __cplusplus
}
#endif