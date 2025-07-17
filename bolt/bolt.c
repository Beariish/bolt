#include "bolt.h"

#ifdef BT_DEBUG
#include <assert.h>
#endif 

#include <memory.h>
#include <immintrin.h>
#include <string.h>

#include "bt_context.h"
#include "bt_object.h"
#include "bt_tokenizer.h"
#include "bt_parser.h"
#include "bt_compiler.h"
#include "bt_debug.h"
#include "bt_gc.h"

void bt_open(bt_Context** context, bt_Handlers* handlers)
{
	*context = handlers->alloc(sizeof(bt_Context));
	bt_Context* ctx = *context;

	ctx->alloc = handlers->alloc;
	ctx->free = handlers->free;
	ctx->realloc = handlers->realloc;
	ctx->on_error = handlers->on_error;

	ctx->write = handlers->write;

	ctx->read_file = handlers->read_file;
	ctx->close_file = handlers->close_file;
	ctx->free_source = handlers->free_source;

	bt_make_gc(ctx);

	for (uint32_t i = 0; i < BT_STRINGTABLE_SIZE; i++) {
		bt_buffer_empty(&ctx->string_table[i]);
	}

	ctx->n_allocated = 0;
	ctx->next = 0;
	ctx->root = bt_allocate(ctx, sizeof(bt_Object), BT_OBJECT_TYPE_NONE);
	ctx->next = ctx->root;
	ctx->troot_top = 0;

	ctx->current_thread = 0;

	ctx->types.null = bt_make_primitive_type(ctx, "null", bt_type_satisfier_same);
	ctx->types.any = bt_make_primitive_type(ctx, "any", bt_type_satisfier_any);
	ctx->types.number = bt_make_primitive_type(ctx, "number", bt_type_satisfier_same);
	ctx->types.boolean = bt_make_primitive_type(ctx, "bool", bt_type_satisfier_same);
	ctx->types.string = bt_make_primitive_type(ctx, "string", bt_type_satisfier_same);
	
	ctx->types.table = bt_make_tableshape_type(ctx, "table", BT_FALSE);
	ctx->types.table->prototype = ctx->types.table;
	
	ctx->types.array = bt_make_array_type(ctx, ctx->types.any);
	ctx->types.array->prototype = ctx->types.array;

	ctx->types.type = bt_make_fundamental_type(ctx);
	ctx->types.type->as.type.boxed = ctx->types.any;

	ctx->loaded_modules = bt_make_table(ctx, 1);
	ctx->prelude = bt_make_table(ctx, 16);
	ctx->native_references = bt_make_table(ctx, 16);

	ctx->type_registry = bt_make_table(ctx, 16);
	bt_register_type(ctx, BT_VALUE_OBJECT(bt_make_string_hashed(ctx, "number")), ctx->types.number);
	bt_register_type(ctx, BT_VALUE_OBJECT(bt_make_string_hashed(ctx, "bool")), ctx->types.boolean);
	bt_register_type(ctx, BT_VALUE_OBJECT(bt_make_string_hashed(ctx, "string")), ctx->types.string);
	bt_register_type(ctx, BT_VALUE_OBJECT(bt_make_string_hashed(ctx, "table")), ctx->types.table);
	bt_register_type(ctx, BT_VALUE_OBJECT(bt_make_string_hashed(ctx, "any")), ctx->types.any);
	bt_register_type(ctx, BT_VALUE_OBJECT(bt_make_string_hashed(ctx, "null")), ctx->types.null);
	bt_register_type(ctx, BT_VALUE_OBJECT(bt_make_string_hashed(ctx, "array")), ctx->types.array);
	bt_register_type(ctx, BT_VALUE_OBJECT(bt_make_string_hashed(ctx, "Type")), ctx->types.type);

	ctx->meta_names.add = bt_make_string_hashed_len(ctx, "@add", 4);
	ctx->meta_names.sub = bt_make_string_hashed_len(ctx, "@sub", 4);
	ctx->meta_names.mul = bt_make_string_hashed_len(ctx, "@mul", 4);
	ctx->meta_names.div = bt_make_string_hashed_len(ctx, "@div", 4);
	ctx->meta_names.lt = bt_make_string_hashed_len(ctx, "@lt", 3);
	ctx->meta_names.lte = bt_make_string_hashed_len(ctx, "@lte", 4);
	ctx->meta_names.eq = bt_make_string_hashed_len(ctx, "@eq", 3);
	ctx->meta_names.neq = bt_make_string_hashed_len(ctx, "@neq", 4);
	ctx->meta_names.format = bt_make_string_hashed_len(ctx, "@format", 7);
	ctx->meta_names.collect = bt_make_string_hashed_len(ctx, "@collect", 8);

	ctx->compiler_options.generate_debug_info = BT_TRUE;
	ctx->compiler_options.accelerate_arithmetic = BT_TRUE;
	ctx->compiler_options.allow_method_hoisting = BT_TRUE;
	ctx->compiler_options.predict_hash_slots = BT_TRUE;
	ctx->compiler_options.typed_array_subscript = BT_TRUE;

	ctx->module_paths = NULL;
	bt_append_module_path(ctx, "%s.bolt");
	bt_append_module_path(ctx, "%s/module.bolt");
}

#ifdef BOLT_ALLOW_PRINTF
#include <stdio.h>

static void bt_error(bt_ErrorType type, const char* module, const char* message, uint16_t line, uint16_t col) {
	switch (type)
	{
	case BT_ERROR_PARSE: {
		printf("parse error [%s (%d:%d)]: %s\n", module, line, col, message);
	} break;

	case BT_ERROR_COMPILE: {
		printf("compile error [%s (%d:%d)]: %s\n", module, line, col, message);
	} break;

	case BT_ERROR_RUNTIME: {
		printf("runtime error [%s (%d:%d)]: %s\n", module, line, col, message);
	} break;
	}
}

static void bt_write(bt_Context* ctx, const char* msg)
{
	printf("%s", msg);
}
#endif

#ifdef BOLT_ALLOW_MALLOC
#include <malloc.h>
#endif

#ifdef BOLT_ALLOW_FOPEN
#include <stdio.h>

static char* bt_read_file(bt_Context* ctx, const char* path, void** handle)
{
	*handle = (void*)fopen(path, "rb");
	if (*handle == 0) return NULL;

	fseek(*handle, 0, SEEK_END);
	uint32_t len = ftell(*handle);
	fseek(*handle, 0, SEEK_SET);

	char* code = ctx->alloc(len + 1);
	fread(code, 1, len, *handle);
	code[len] = 0;

	return code;
}

static void bt_close_file(bt_Context* ctx, const char* path, void* handle)
{
	fclose(handle);
}

static void bt_free_source(bt_Context* ctx, char* source)
{
	ctx->free(source);
}
#endif

bt_Handlers bt_default_handlers()
{
	bt_Handlers result = { 0 };

#ifdef BOLT_ALLOW_MALLOC
	result.alloc = malloc;
	result.realloc = realloc;
	result.free = free;
#endif

#ifdef BOLT_ALLOW_FOPEN
	result.read_file = bt_read_file;
	result.close_file = bt_close_file;
	result.free_source = bt_free_source;
#endif

#ifdef BOLT_ALLOW_PRINTF
	result.on_error = bt_error;
	result.write = bt_write;
#endif

	return result;
}

void bt_close(bt_Context* context)
{
	context->types.any = 0;
	context->types.null = 0;
	context->types.number = 0;
	context->types.boolean = 0;
	context->types.string = 0;
	context->types.array = 0;
	context->types.table = 0;
	context->types.type = 0;
	
	context->meta_names.add = 0;
	context->meta_names.div = 0;
	context->meta_names.mul = 0;
	context->meta_names.sub = 0;
	context->meta_names.lt = 0;
	context->meta_names.lte = 0;
	context->meta_names.eq = 0;
	context->meta_names.neq = 0;
	context->meta_names.format = 0;
	context->meta_names.collect = 0;
	
	context->type_registry = 0;
	context->prelude = 0;
	context->loaded_modules = 0;
	context->troot_top = 0;
	context->current_thread = 0;
	context->native_references = 0;
	while (bt_collect(&context->gc, 0));

	bt_free(context, context->root);

	bt_Path* path = context->module_paths;
	while (path) {
		bt_Path* next = path->next;
		bt_gc_free(context, path->spec, strlen(path->spec) + 1);
		bt_gc_free(context, path, sizeof(bt_Path));
		path = next;
	}

	bt_destroy_gc(context, &context->gc);
	context->free(context);
}

bt_bool bt_run(bt_Context* context, const char* source)
{
	bt_Module* mod = bt_compile_module(context, source, "<interp>");
	if (!mod) return BT_FALSE;
	return bt_execute(context, (bt_Callable*)mod);
}

bt_Module* bt_compile_module(bt_Context* context, const char* source, const char* mod_name)
{
#ifdef BOLT_PRINT_DEBUG
	printf("%s\n", source);
	printf("-----------------------------------------------------\n");
#endif
	
	bt_Tokenizer tok = bt_open_tokenizer(context);
	bt_tokenizer_set_source(&tok, source);
	bt_tokenizer_set_source_name(&tok, mod_name);

	bt_Parser parser = bt_open_parser(&tok);
	bt_bool parse_result = bt_parse(&parser);

#ifdef BOLT_PRINT_DEBUG
	printf("-----------------------------------------------------\n");
#endif 

	bt_Module* result = 0;

	if (parse_result) {
		bt_Compiler compiler = bt_open_compiler(&parser, context->compiler_options);
		result = bt_compile(&compiler);
		bt_close_compiler(&compiler);
	}

	bt_close_parser(&parser);
	bt_close_tokenizer(&tok);

	return result;
}

void bt_push_root(bt_Context* ctx, bt_Object* root)
{
	if (ctx->troot_top >= BT_TEMPROOTS_SIZE) {
		bt_runtime_error(ctx->current_thread, "Temp root stack overflow!", NULL);
	}

	ctx->troots[ctx->troot_top++] = root;
}

void bt_pop_root(bt_Context* ctx)
{
	if (ctx->troot_top <= 0) {
		bt_runtime_error(ctx->current_thread, "Temp root stack underflow!", NULL);
	}

	ctx->troots[--ctx->troot_top] = 0;
}

uint32_t bt_add_ref(bt_Context* ctx, bt_Object* obj)
{
	bt_Value num_refs = bt_table_get(ctx->native_references, BT_VALUE_OBJECT(obj));
	if (num_refs == BT_VALUE_NULL) {
		num_refs = BT_VALUE_NUMBER(0);
	}

	num_refs = BT_VALUE_NUMBER((uint32_t)BT_AS_NUMBER(num_refs) + 1);
	bt_table_set(ctx, ctx->native_references, BT_VALUE_OBJECT(obj), num_refs);

	return (uint32_t)BT_AS_NUMBER(num_refs);
}

uint32_t bt_remove_ref(bt_Context* ctx, bt_Object* obj) {
	bt_Value num_refs = bt_table_get(ctx->native_references, BT_VALUE_OBJECT(obj));
	if (num_refs == BT_VALUE_NULL) {
		return 0;
	}

	num_refs = BT_VALUE_NUMBER((uint32_t)BT_AS_NUMBER(num_refs) - 1);
	bt_table_set(ctx, ctx->native_references, BT_VALUE_OBJECT(obj), num_refs);

	if ((uint32_t)BT_AS_NUMBER(num_refs) == 0) {
		bt_table_delete_key(ctx->native_references, BT_VALUE_OBJECT(obj));
	}

	return (uint32_t)BT_AS_NUMBER(num_refs);
}

void bt_register_type(bt_Context* context, bt_Value name, bt_Type* type)
{
	bt_table_set(context, context->type_registry, name, BT_VALUE_OBJECT(type));
	bt_register_prelude(context, name, context->types.type, BT_VALUE_OBJECT(type));
}

bt_Type* bt_find_type(bt_Context* context, bt_Value name)
{
	return (bt_Type*)BT_AS_OBJECT(bt_table_get(context->type_registry, name));
}

void bt_register_prelude(bt_Context* context, bt_Value name, bt_Type* type, bt_Value value)
{
	bt_ModuleImport* new_import = BT_ALLOCATE(context, IMPORT, bt_ModuleImport);
	new_import->name = (bt_String*)BT_AS_OBJECT(name);
	new_import->type = type;
	new_import->value = value;

	bt_table_set(context, context->prelude, name, BT_VALUE_OBJECT(new_import));
}

void bt_register_module(bt_Context* context, bt_Value name, bt_Module* module)
{
	bt_table_set(context, context->loaded_modules, name, BT_VALUE_OBJECT(module));
}

void bt_append_module_path(bt_Context* context, const char* spec)
{
	bt_Path** ptr_next = &context->module_paths;
	while (*ptr_next) {
		ptr_next = &(*ptr_next)->next;
	}

	bt_Path* actual_next = bt_gc_alloc(context, sizeof(bt_Path));
	actual_next->spec = bt_gc_alloc(context, strlen(spec) + 1);
	strcpy(actual_next->spec, spec);
	actual_next->next = NULL;

	*ptr_next = actual_next;
}

// I really don't like this but we need to do something here so that identical modules through different paths maintain identity
static bt_Value bt_normalize_path(bt_Context* context, bt_Value path)
{
	bt_String* as_string = (bt_String*)BT_AS_OBJECT(path);
	bt_String* result = bt_make_string_empty(context, as_string->len);
	char* char_data = BT_STRING_STR(result);
	
	uint32_t new_len = 0;
	for (uint32_t idx = 0; idx < as_string->len; idx++) {
		char_data[new_len++] = BT_STRING_STR(as_string)[idx];

		if (new_len > 2 && char_data[new_len - 1] == '.' && char_data[new_len - 2] == '.') {
			new_len -= 4; // skip the slash

			while (new_len > 0 && char_data[new_len] != '/') char_data[new_len--] = 0;
		}
	}

	while (char_data[0] == '/') {
		for (uint32_t idx = 0; idx < new_len - 1; idx++) {
			char_data[idx] = char_data[idx + 1];
		}

		char_data[new_len--] = 0;
	}
	
	result->len = new_len;
	return BT_VALUE_OBJECT(result);
}

bt_Module* bt_find_module(bt_Context* context, bt_Value name)
{
	bt_push_root(context, BT_AS_OBJECT(name));
	// TODO: resolve module name with path
	bt_Value normalized_path = bt_normalize_path(context, name);
	bt_Module* mod = (bt_Module*)BT_AS_OBJECT(bt_table_get(context->loaded_modules, normalized_path));
	if (mod == 0) {
		bt_String* to_load = (bt_String*)BT_AS_OBJECT(name);

		char path_buf[BT_MODULE_PATH_SIZE];
		uint32_t path_len = 0;
		void* handle = NULL;
		char* code = NULL;

		bt_Path* pathspec = context->module_paths;
		while (pathspec && !code) {
			path_len = sprintf(path_buf, pathspec->spec, BT_STRING_STR(to_load));
	
			if (path_len >= BT_MODULE_PATH_SIZE) {
				bt_runtime_error(context->current_thread, "Path buffer overrun when loading module!", NULL);
				bt_pop_root(context);
				return NULL;
			}

			path_buf[path_len] = 0;
			code = context->read_file(context, path_buf, &handle);
			if (code) context->close_file(context, path_buf, handle);
			
			pathspec = pathspec->next;
		}

		if (code == 0) {
			if(context->current_thread) bt_runtime_error(context->current_thread, "Cannot find module file", NULL);
			bt_pop_root(context);
			return NULL;
		}


		bt_Module* new_mod = bt_compile_module(context, code, path_buf);
		context->free_source(context, code);

		if (new_mod) {
			new_mod->name = (bt_String*)BT_AS_OBJECT(normalized_path);
			new_mod->path = bt_make_string_len(context, path_buf, path_len);
			if (bt_execute(context, (bt_Callable*)new_mod)) {
				bt_register_module(context, normalized_path, new_mod);

				bt_pop_root(context);
				return new_mod;
			}
			else {
				bt_pop_root(context);
				return NULL;
			}
		}
		else {
			bt_pop_root(context);
			return NULL;
		}
	}

	bt_pop_root(context);
	return mod;
}

bt_Thread* bt_make_thread(bt_Context* context)
{
	bt_Thread* result = bt_gc_alloc(context, sizeof(bt_Thread));
	result->depth = 0;
	result->native_depth = 0;
	result->top = 0;
	result->context = context;
	result->should_report = BT_TRUE;
	result->last_error = 0;

	result->native_stack[result->native_depth].return_loc = 0;
	result->native_stack[result->native_depth].argc = 0;
	
	result->callstack[result->depth++] = BT_MAKE_STACKFRAME(0, 0, 0);

	return result;
}

void bt_destroy_thread(bt_Context* context, bt_Thread* thread)
{
	bt_gc_free(context, thread, sizeof(bt_Thread));
}

static void call(bt_Context* context, bt_Thread* thread, bt_Module* module, bt_Op* ip, bt_Value* constants, int8_t return_loc);

bt_bool bt_execute(bt_Context* context, bt_Callable* callable)
{
	bt_Thread* thread = bt_make_thread(context);
	bt_bool result = bt_execute_on_thread(context, thread, callable);
	bt_destroy_thread(context, thread);
	
	return result;
}

bt_bool bt_execute_on_thread(bt_Context* context, bt_Thread* thread, bt_Callable* callable)
{
	return bt_execute_with_args(context, thread, callable, NULL, 0);
}

bt_bool bt_execute_with_args(bt_Context* context, bt_Thread* thread, bt_Callable* callable, bt_Value* args, uint8_t argc)
{
	bt_Thread* old_thread = context->current_thread;

	context->current_thread = thread;

	bt_push(thread, BT_VALUE_OBJECT(callable));

	for (uint8_t i = 0; i < argc; ++i) {
		bt_push(thread, args[i]);
	}

	int32_t result = setjmp(&thread->error_loc[0]);
	if (result == 0) bt_call(thread, argc);
	else {
		context->current_thread = old_thread;
		return BT_FALSE;
	}

	context->current_thread = old_thread;
	return BT_TRUE;
}

void bt_runtime_error(bt_Thread* thread, const char* message, bt_Op* ip)
{
	thread->last_error = bt_make_string(thread->context, message);
	if (thread->should_report) {
		if (ip != NULL) {
			bt_Callable* callable = BT_STACKFRAME_GET_CALLABLE(thread->callstack[thread->depth - 1]);
			
			uint16_t line = 0, col = 0;
				
			bt_DebugLocBuffer* loc_buffer = bt_get_debug_locs(callable);
			if (loc_buffer) {
				uint32_t loc_index = bt_get_debug_index(callable, ip);
				bt_TokenBuffer* tokens = bt_get_debug_tokens(callable);
				bt_Token* source_token = tokens->elements[loc_buffer->elements[loc_index]];
				
				line = source_token->line - 1;
				col = source_token->col;
			}

			bt_Module* module = bt_get_owning_module(callable);
			thread->context->on_error(BT_ERROR_RUNTIME, (module && module->path) ? BT_STRING_STR(module->path) : "", message, line, col);
		}
		else {
			thread->context->on_error(BT_ERROR_RUNTIME, "<native>", message, 0, 0);
		}
	}

	thread->context->current_thread = NULL;
	longjmp(thread->error_loc, 1);
}

void bt_push(bt_Thread* thread, bt_Value value)
{
	bt_StackFrame* frame = &thread->callstack[thread->depth - 1];
	*frame += 1;
	thread->stack[thread->top + BT_STACKFRAME_GET_SIZE(*frame) + BT_STACKFRAME_GET_USER_TOP(*frame)] = value;
}

bt_Value bt_pop(bt_Thread* thread)
{
	bt_StackFrame* frame = &thread->callstack[thread->depth - 1];
	bt_Value result = thread->stack[thread->top + BT_STACKFRAME_GET_SIZE(*frame) + BT_STACKFRAME_GET_USER_TOP(*frame)];
	*frame -= 1;
	return result;
}

bt_Value bt_make_closure(bt_Thread* thread, uint8_t num_upvals)
{
	bt_StackFrame* frame = thread->callstack + thread->depth - 1;
	bt_Value* true_top = thread->stack + thread->top + BT_STACKFRAME_GET_SIZE(*frame) + BT_STACKFRAME_GET_USER_TOP(*frame);

	bt_Closure* cl = BT_ALLOCATE_INLINE_STORAGE(thread->context, CLOSURE, bt_Closure, sizeof(bt_Value) * num_upvals);
	cl->num_upv = num_upvals;
	bt_Value* upv = BT_CLOSURE_UPVALS(cl);
	for (bt_Value* top = true_top - (num_upvals - 1); top <= true_top; top++) {
		*upv = *top;
		upv++;
	}

	cl->fn = (bt_Fn*)BT_AS_OBJECT(*(true_top - num_upvals));
	*frame -= num_upvals + 1;

	return BT_VALUE_OBJECT(cl);
}

void bt_call(bt_Thread* thread, uint8_t argc)
{
	uint16_t old_top = thread->top;

	bt_StackFrame* frame = &thread->callstack[thread->depth - 1];
	*frame -= argc; // + 1 for the function itself

	thread->top += BT_STACKFRAME_GET_SIZE(*frame) + 2;
	bt_Object* obj = BT_AS_OBJECT(thread->stack[thread->top - 1]);

	switch (BT_OBJECT_GET_TYPE(obj)) {
	case BT_OBJECT_TYPE_FN: {
		bt_Fn* callable = (bt_Fn*)obj;
		thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, callable->stack_size, 0);
		call(thread->context, thread, callable->module, callable->instructions.elements, callable->constants.elements, -1);
	} break;
	case BT_OBJECT_TYPE_CLOSURE: {
		bt_Fn* callable = ((bt_Closure*)obj)->fn;
		thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, callable->stack_size, 0);
		call(thread->context, thread, callable->module, callable->instructions.elements, callable->constants.elements, -1);
	} break;
	case BT_OBJECT_TYPE_NATIVE_FN: {
		thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, 0, 0);

		thread->native_stack[thread->native_depth].return_loc = -2;
		thread->native_stack[thread->native_depth].argc = argc;
		thread->native_depth++;

		bt_NativeFn* callable = (bt_NativeFn*)obj;
		callable->fn(thread->context, thread);
		thread->native_depth--;
	} break;
	case BT_OBJECT_TYPE_MODULE: {
		bt_Module* mod = (bt_Module*)obj;
		thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, mod->stack_size, 0);

		call(thread->context, thread, mod, mod->instructions.elements, mod->constants.elements, -1);
	} break;
	default: bt_runtime_error(thread, "Unsupported callable type.", NULL);
	}

	thread->depth--;
	thread->top = old_top;
}

const char* bt_get_debug_source(bt_Callable* callable)
{
	switch (BT_OBJECT_GET_TYPE(callable)) {
	case BT_OBJECT_TYPE_FN:
		return callable->fn.module->debug_source;
	case BT_OBJECT_TYPE_MODULE:
		return callable->module.debug_source;
	case BT_OBJECT_TYPE_CLOSURE:
		return callable->cl.fn->module->debug_source;
	}

	return NULL;
}

bt_TokenBuffer* bt_get_debug_tokens(bt_Callable* callable)
{
	switch (BT_OBJECT_GET_TYPE(callable)) {
	case BT_OBJECT_TYPE_FN:
		return &callable->fn.module->debug_tokens;
	case BT_OBJECT_TYPE_MODULE:
		return &callable->module.debug_tokens;
	case BT_OBJECT_TYPE_CLOSURE:
		return &callable->cl.fn->module->debug_tokens;
	}

	return NULL;
}

bt_StrSlice bt_get_debug_line(const char* source, uint16_t line)
{
	uint16_t cur_line = 1;
	while (*source) {
		if (cur_line == line) {
			const char* line_start = source;
			while (*source && *source != '\n') source++;
			return (bt_StrSlice) { line_start, (uint16_t)(source - line_start) };
		}

		if (*source == '\n') cur_line++;
		source++;
	}

	return (bt_StrSlice) { source, 0 };
}

bt_DebugLocBuffer* bt_get_debug_locs(bt_Callable* callable)
{
	switch (BT_OBJECT_GET_TYPE(callable)) {
	case BT_OBJECT_TYPE_FN:
		return callable->fn.debug;
	case BT_OBJECT_TYPE_MODULE:
		return callable->module.debug_locs;
	case BT_OBJECT_TYPE_CLOSURE:
		return callable->cl.fn->debug;
	}

	return NULL;
}

uint32_t bt_get_debug_index(bt_Callable* callable, bt_Op* ip)
{
	bt_InstructionBuffer* instructions = NULL;
	switch (BT_OBJECT_GET_TYPE(callable)) {
	case BT_OBJECT_TYPE_FN:
		instructions = &callable->fn.instructions;
		break;
	case BT_OBJECT_TYPE_MODULE:
		instructions = &callable->module.instructions;
		break;
	case BT_OBJECT_TYPE_CLOSURE:
		instructions = &callable->cl.fn->instructions;
		break;
	}

	if (instructions) {
		return (uint32_t)(ip - instructions->elements);
	}

	return 0;
}

bt_String* bt_get_or_make_interned(bt_Context* ctx, const char* str, uint32_t len)
{
	uint64_t hash = bt_hash_str(str, len);
	uint64_t bucket_idx = hash % BT_STRINGTABLE_SIZE;

	bt_StringTableBucket* bucket = ctx->string_table + bucket_idx;

	for (uint32_t i = 0; i < bucket->length; ++i) {
		bt_StringTableEntry* entry = bucket->elements + i;
		if (entry->hash == hash) return entry->string;
	}

	bt_StringTableEntry new_entry;
	new_entry.hash = hash;
	new_entry.string = BT_ALLOCATE_INLINE_STORAGE(ctx, STRING, bt_String, len + 1);
	memcpy(BT_STRING_STR(new_entry.string), str, len);
	BT_STRING_STR(new_entry.string)[len] = 0;
	new_entry.string->len = len;
	new_entry.string->hash = hash;
	new_entry.string->interned = 1;
	bt_buffer_push(ctx, bucket, new_entry);

	return new_entry.string;
}

void bt_remove_interned(bt_Context* ctx, bt_String* str)
{
	uint64_t bucket_idx = str->hash % BT_STRINGTABLE_SIZE;

	bt_StringTableBucket* bucket = ctx->string_table + bucket_idx;
	for (uint32_t i = 0; i < bucket->length; ++i) {
		bt_StringTableEntry* entry = bucket->elements + i;
		if (entry->hash == str->hash) {
			bucket->elements[i] = bucket->elements[bucket->length - 1];
			bucket->length--;
			return;
		}
	}
}

#define XSTR(x) #x
#define ARITH_MF(name)                                                                               \
if (BT_IS_OBJECT(lhs)) {																			 \
	bt_Object* obj = BT_AS_OBJECT(lhs);																 \
	if (BT_OBJECT_GET_TYPE(obj) == BT_OBJECT_TYPE_TABLE) {											 \
		bt_Table* tbl = (bt_Table*)obj;																 \
		bt_Value add_fn = bt_table_get(tbl, BT_VALUE_OBJECT(thread->context->meta_names.name));		 \
		if (add_fn == BT_VALUE_NULL) bt_runtime_error(thread, "Unable to find @" #name "metafunction!", ip);	 \
																									 \
		bt_push(thread, add_fn);																	 \
		bt_push(thread, lhs);																		 \
		bt_push(thread, rhs);																		 \
		bt_call(thread, 2);																			 \
		*result = bt_pop(thread);   														 \
																									 \
		return;																						 \
	}																								 \
}

static BT_NO_INLINE void bt_add(bt_Thread* thread, bt_Value* __restrict result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) + BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(add);

	if (BT_IS_OBJECT(lhs) && BT_IS_OBJECT(rhs)) {
		bt_String* lhs_str = (bt_String*)BT_AS_OBJECT(lhs);
		bt_String* rhs_str = (bt_String*)BT_AS_OBJECT(rhs);

		if (BT_OBJECT_GET_TYPE(lhs_str) == BT_OBJECT_TYPE_STRING && BT_OBJECT_GET_TYPE(rhs_str) == BT_OBJECT_TYPE_STRING) {
			*result = BT_VALUE_OBJECT(bt_string_concat(thread->context, lhs_str, rhs_str));
			return;
		}
	}

	bt_runtime_error(thread, "Unable to add values", ip);
}

static BT_FORCE_INLINE void bt_neg(bt_Thread* thread, bt_Value* __restrict result, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(-BT_AS_NUMBER(rhs));
		return;
	}

	bt_runtime_error(thread, "Cannot negate non-number value!", ip);
}

static BT_FORCE_INLINE void bt_sub(bt_Thread* thread, bt_Value* __restrict result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) - BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(sub);

	bt_runtime_error(thread, "Cannot subtract non-number value!", ip);
}
static BT_NO_INLINE void bt_mul(bt_Thread* thread, bt_Value* __restrict result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) * BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(mul);

	bt_runtime_error(thread, "Cannot multiply non-number value!", ip);
}

static BT_NO_INLINE void bt_div(bt_Thread* thread, bt_Value* __restrict result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_VALUE_NUMBER(BT_AS_NUMBER(lhs) / BT_AS_NUMBER(rhs));
		return;
	}

	ARITH_MF(div);

	bt_runtime_error(thread, "Cannot divide non-number value!", ip);
}

static BT_NO_INLINE void bt_lt(bt_Thread* thread, bt_Value* __restrict result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_AS_NUMBER(lhs) < BT_AS_NUMBER(rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		return;
	}

	bt_runtime_error(thread, "Cannot lt non-number value!", ip);
}

static BT_FORCE_INLINE void bt_lte(bt_Thread* thread, bt_Value* __restrict result, bt_Value lhs, bt_Value rhs, bt_Op* ip)
{
	if (BT_IS_NUMBER(lhs) && BT_IS_NUMBER(rhs)) {
		*result = BT_AS_NUMBER(lhs) <= BT_AS_NUMBER(rhs) ? BT_VALUE_TRUE : BT_VALUE_FALSE;
		return;
	}

	bt_runtime_error(thread, "Cannot lte non-number value!", ip);
}

static void call(bt_Context* __restrict context, bt_Thread* __restrict thread, bt_Module* __restrict module, bt_Op* __restrict ip, bt_Value* __restrict constants, int8_t return_loc)
{
	register bt_Value* __restrict stack = thread->stack + thread->top;
	_mm_prefetch((const char*)stack, 1);
	register bt_Value* __restrict upv = BT_CLOSURE_UPVALS(BT_STACKFRAME_GET_CALLABLE(thread->callstack[thread->depth - 1]));
	register bt_Object* __restrict obj, * __restrict obj2;

	BT_ASSUME(obj);
	BT_ASSUME(obj2);

#ifndef BOLT_USE_INLINE_THREADING
	register bt_Op op;
#define NEXT break;
#define RETURN return;
#define CASE(x) case BT_OP_##x
#define DISPATCH \
	op = *ip++; \
	switch(BT_GET_OPCODE(op))
#else
#define RETURN return;
#define CASE(x) lbl_##x
#define X(op) case BT_OP_##op: goto lbl_##op;
#define op (*ip)
#define NEXT                          \
	switch (BT_GET_OPCODE(*(++ip))) { \
		BT_OPS_X                      \
	}
#define DISPATCH                      \
	switch (BT_GET_OPCODE(op)) {	  \
		BT_OPS_X                      \
	}
#endif
#ifndef BOLT_USE_INLINE_THREADING
	for (;;) 
#endif 
	{
		DISPATCH {
		CASE(LOAD):        stack[BT_GET_A(op)] = constants[BT_GET_B(op)];                       NEXT;
		CASE(LOAD_SMALL):  stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_GET_IBC(op));               NEXT;
		CASE(LOAD_NULL):   stack[BT_GET_A(op)] = BT_VALUE_NULL;                                 NEXT;
		CASE(LOAD_BOOL):   stack[BT_GET_A(op)] = BT_GET_B(op) ? BT_VALUE_TRUE : BT_VALUE_FALSE; NEXT;
		CASE(LOAD_IMPORT): stack[BT_GET_A(op)] = module->imports.elements[BT_GET_B(op)]->value; NEXT;

		CASE(MOVE): stack[BT_GET_A(op)] = stack[BT_GET_B(op)]; NEXT;
			
		CASE(LOADUP):  BT_ASSUME(upv); stack[BT_GET_A(op)] = upv[BT_GET_B(op)]; NEXT;
		CASE(STOREUP): BT_ASSUME(upv); upv[BT_GET_A(op)] = stack[BT_GET_B(op)]; NEXT;

		CASE(NEG):
			if(BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(-BT_AS_NUMBER(stack[BT_GET_B(op)]));
			else bt_neg(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], ip);                      
		NEXT;
		
		CASE(ADD): 
			if(BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) + BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_add(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); 
		NEXT;
		
		CASE(SUB): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) - BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_sub(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); 
		NEXT;

		CASE(MUL): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) * BT_AS_NUMBER(stack[BT_GET_C(op)])); 
			else bt_mul(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); 
		NEXT;

		CASE(DIV): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_B(op)]) / BT_AS_NUMBER(stack[BT_GET_C(op)])); 
			else bt_div(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip); 
		NEXT;

		CASE(EQ):
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) == BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else stack[BT_GET_A(op)] = BT_VALUE_FALSE + bt_value_is_equal(stack[BT_GET_B(op)], stack[BT_GET_C(op)]);  
		NEXT;

		CASE(NEQ): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) != BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else stack[BT_GET_A(op)] = BT_VALUE_TRUE - bt_value_is_equal(stack[BT_GET_B(op)], stack[BT_GET_C(op)]);  
		NEXT;
		
		CASE(LT): 
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) < BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_lt(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);
		NEXT;

		CASE(LTE):
			if (BT_IS_ACCELERATED(op)) stack[BT_GET_A(op)] = BT_VALUE_FALSE + (BT_AS_NUMBER(stack[BT_GET_B(op)]) <= BT_AS_NUMBER(stack[BT_GET_C(op)]));
			else bt_lte(thread, stack + BT_GET_A(op), stack[BT_GET_B(op)], stack[BT_GET_C(op)], ip);
		NEXT;

		CASE(NOT): stack[BT_GET_A(op)] = BT_VALUE_BOOL(BT_IS_FALSE(stack[BT_GET_B(op)])); NEXT;

		CASE(TEST):
			if (stack[BT_GET_A(op)] == BT_VALUE_BOOL(BT_IS_ACCELERATED(op))) {
				ip += BT_GET_IBC(op);
			}
			NEXT;
			
		CASE(LOAD_IDX):
			obj = BT_AS_OBJECT(stack[BT_GET_B(op)]);
			if (BT_IS_ACCELERATED(op)) {
				if (BT_IS_FAST(stack[BT_GET_B(op)])) {
					stack[BT_GET_A(op)] = (BT_TABLE_PAIRS(obj) + BT_GET_C(op))->value;
					ip++; // skip the ext op
				}
				else {
					obj2 = (bt_Object*)(uintptr_t)BT_GET_A(op); // save this, as we modify op
					stack[(uint8_t)obj2] = bt_get(context, obj, constants[BT_GET_IBC(*(++ip))]);
				}
			} else stack[BT_GET_A(op)] = bt_get(context, obj, stack[BT_GET_C(op)]); 
		NEXT;

		CASE(STORE_IDX):
			obj = BT_AS_OBJECT(stack[BT_GET_A(op)]);

			if (BT_IS_ACCELERATED(op))	{
				if (BT_IS_FAST(stack[BT_GET_A(op)])) {
					(BT_TABLE_PAIRS(obj) + BT_GET_B(op))->value = stack[BT_GET_C(op)];
					ip++; // skip the ext op
				}
				else {
					obj2 = (bt_Object*)(intptr_t)BT_GET_C(op); // save this, as we modify op
					bt_set(context, obj, constants[BT_GET_IBC(*(++ip))], stack[(uint8_t)obj2]);
				}
			}
			else bt_set(context, obj, stack[BT_GET_B(op)], stack[BT_GET_C(op)]); 
		NEXT;
			
		CASE(TABLE): 
			if (BT_IS_ACCELERATED(op)) {
				obj = (bt_Object*)BT_AS_OBJECT(stack[BT_GET_C(op)]);
				obj2 = (bt_Object*)BT_ALLOCATE_INLINE_STORAGE(context, TABLE, bt_Table, (sizeof(bt_TablePair) * BT_GET_B(op)) - sizeof(bt_Value));
				memcpy((char*)obj2 + sizeof(bt_Object), 
					((char*)((bt_Type*)obj)->as.table_shape.tmpl) + sizeof(bt_Object),
					(sizeof(bt_Table) - sizeof(bt_Object)) + (sizeof(bt_TablePair) * (BT_GET_B(op))) - sizeof(bt_Value));
				stack[BT_GET_A(op)] = BT_VALUE_OBJECT(obj2);
			}
			else stack[BT_GET_A(op)] = BT_VALUE_OBJECT(bt_make_table(context, BT_GET_IBC(op))); 
		NEXT;

		CASE(ARRAY):
			obj = (bt_Object*)bt_make_array(context, BT_GET_IBC(op));
			((bt_Array*)obj)->length = BT_GET_IBC(op);
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(obj);
		NEXT;

		CASE(EXPORT): bt_module_export(context, module, (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)]), stack[BT_GET_A(op)], stack[BT_GET_B(op)]); NEXT;

		CASE(CLOSE):
			obj2 = (bt_Object*)BT_ALLOCATE_INLINE_STORAGE(context, CLOSURE, bt_Closure, sizeof(bt_Value) * BT_GET_C(op));
			obj = BT_AS_OBJECT(stack[BT_GET_B(op)]);
			for (uint8_t i = 0; i < BT_GET_C(op); i++) {
				BT_CLOSURE_UPVALS(obj2)[i] = stack[BT_GET_B(op) + 1 + i];
			}
			((bt_Closure*)obj2)->fn = (bt_Fn*)obj;
			((bt_Closure*)obj2)->num_upv = BT_GET_C(op);
			stack[BT_GET_A(op)] = BT_VALUE_OBJECT(obj2);
		NEXT;

		CASE(LOAD_IDX_K): stack[BT_GET_A(op)] = bt_get(context, BT_AS_OBJECT(stack[BT_GET_B(op)]), constants[BT_GET_C(op)]); NEXT;
		CASE(STORE_IDX_K): bt_set(context, BT_AS_OBJECT(stack[BT_GET_A(op)]), constants[BT_GET_B(op)], stack[BT_GET_C(op)]); NEXT;

		CASE(LOAD_PROTO): stack[BT_GET_A(op)] = bt_table_get(((bt_Table*)BT_AS_OBJECT(stack[BT_GET_B(op)]))->prototype, constants[BT_GET_C(op)]); NEXT;

		CASE(EXPECT):   stack[BT_GET_A(op)] = stack[BT_GET_B(op)]; if (stack[BT_GET_A(op)] == BT_VALUE_NULL) bt_runtime_error(thread, "Operator '!' failed - lhs was null!", ip); NEXT;
		CASE(COALESCE): stack[BT_GET_A(op)] = stack[BT_GET_B(op)] == BT_VALUE_NULL ? stack[BT_GET_C(op)] : stack[BT_GET_B(op)]; NEXT;

		CASE(TCHECK): stack[BT_GET_A(op)] = bt_is_type(stack[BT_GET_B(op)], (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)])) ? BT_VALUE_TRUE : BT_VALUE_FALSE; NEXT;
		CASE(TCAST):
			if (bt_can_cast(stack[BT_GET_B(op)], (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)]))) {
				if (BT_IS_OBJECT(stack[BT_GET_B(op)])) {
					stack[BT_GET_A(op)] = BT_MAKE_SLOW(stack[BT_GET_B(op)]);
				} else {
					stack[BT_GET_A(op)] = bt_value_cast(stack[BT_GET_B(op)], (bt_Type*)BT_AS_OBJECT(stack[BT_GET_C(op)]));
				}
			} else {
				stack[BT_GET_A(op)] = BT_VALUE_NULL;
			}
		NEXT;

		CASE(TSET):
			bt_type_set_field(context, (bt_Type*)BT_AS_OBJECT(stack[BT_GET_A(op)]), stack[BT_GET_B(op)], stack[BT_GET_C(op)]);
		NEXT;

		CASE(CALL):
			if (thread->depth >= BT_CALLSTACK_SIZE) {
				bt_runtime_error(thread, "Stack overflow!", ip);
			}

			obj2 = (bt_Object*)(uint64_t)thread->top;

			obj = BT_AS_OBJECT(stack[BT_GET_B(op)]);

			thread->top += BT_GET_B(op) + 1;

			switch (BT_OBJECT_GET_TYPE(obj)) {
			case BT_OBJECT_TYPE_FN:
				thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, ((bt_Fn*)obj)->stack_size, 0);
				call(context, thread, ((bt_Fn*)obj)->module, ((bt_Fn*)obj)->instructions.elements, ((bt_Fn*)obj)->constants.elements, BT_GET_A(op) - (BT_GET_B(op) + 1));
			break;
			case BT_OBJECT_TYPE_CLOSURE:
				switch (BT_OBJECT_GET_TYPE(((bt_Closure*)obj)->fn)) {
				case BT_OBJECT_TYPE_FN:
					thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, ((bt_Closure*)obj)->fn->stack_size, 0);
					call(context, thread, ((bt_Closure*)obj)->fn->module, ((bt_Closure*)obj)->fn->instructions.elements, ((bt_Closure*)obj)->fn->constants.elements, BT_GET_A(op) - (BT_GET_B(op) + 1));
					break;
				case BT_OBJECT_TYPE_NATIVE_FN:
					thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, 0, 0);

					thread->native_stack[thread->native_depth].return_loc = BT_GET_A(op) - (BT_GET_B(op) + 1);
					thread->native_stack[thread->native_depth].argc = BT_GET_C(op);
					thread->native_depth++;

					((bt_NativeFn*)((bt_Closure*)obj)->fn)->fn(context, thread);
					thread->native_depth--;
				break;
				default: bt_runtime_error(thread, "Closure contained unsupported callable type.", ip);
				}
			break;
			case BT_OBJECT_TYPE_NATIVE_FN:
				thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, 0, 0);

				thread->native_stack[thread->native_depth].return_loc = BT_GET_A(op) - (BT_GET_B(op) + 1);
				thread->native_stack[thread->native_depth].argc = BT_GET_C(op);
				thread->native_depth++;

				((bt_NativeFn*)obj)->fn(context, thread);
				thread->native_depth--;
			break;
			default: bt_runtime_error(thread, "Unsupported callable type.", ip);
			}

			thread->depth--;
			thread->top = (uint32_t)(uint64_t)obj2;
		NEXT;

		CASE(REC_CALL):
			if (thread->depth >= BT_CALLSTACK_SIZE) {
				bt_runtime_error(thread, "Stack overflow!", ip);
			}

		obj2 = (bt_Object*)(uint64_t)thread->top;

		obj = (bt_Object*)BT_STACKFRAME_GET_CALLABLE(thread->callstack[thread->depth - 1]);

		thread->top += BT_GET_B(op);

		switch (BT_OBJECT_GET_TYPE(obj)) {
		case BT_OBJECT_TYPE_FN:
			thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, ((bt_Fn*)obj)->stack_size, 0);
			call(context, thread, ((bt_Fn*)obj)->module, ((bt_Fn*)obj)->instructions.elements, ((bt_Fn*)obj)->constants.elements, BT_GET_A(op) - (BT_GET_B(op)));
			break;
		case BT_OBJECT_TYPE_CLOSURE:
			switch (BT_OBJECT_GET_TYPE(((bt_Closure*)obj)->fn)) {
			case BT_OBJECT_TYPE_FN:
				thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, ((bt_Closure*)obj)->fn->stack_size, 0);
				call(context, thread, ((bt_Closure*)obj)->fn->module, ((bt_Closure*)obj)->fn->instructions.elements, ((bt_Closure*)obj)->fn->constants.elements, BT_GET_A(op) - (BT_GET_B(op)));
				break;
			default: bt_runtime_error(thread, "Closure contained unsupported callable type.", ip);
			}
			break;
		default: bt_runtime_error(thread, "Unsupported callable type.", ip);
		}

		thread->depth--;
		thread->top = (uint32_t)(uint64_t)obj2;
		NEXT;

		CASE(JMP): ip += BT_GET_IBC(op); NEXT;
		CASE(JMPF): if (stack[BT_GET_A(op)] == BT_VALUE_FALSE) ip += BT_GET_IBC(op); NEXT;

		CASE(RETURN): stack[return_loc] = stack[BT_GET_A(op)];
		CASE(END): RETURN;

		CASE(NUMFOR):
			stack[BT_GET_A(op)] = BT_VALUE_NUMBER(BT_AS_NUMBER(stack[BT_GET_A(op)]) + BT_AS_NUMBER(stack[BT_GET_A(op) + 1]));
			if (BT_AS_NUMBER(stack[BT_GET_A(op)]) >= BT_AS_NUMBER(stack[BT_GET_A(op) + 2])) ip += BT_GET_IBC(op);
		NEXT;

		CASE(ITERFOR):
			obj = BT_AS_OBJECT(stack[BT_GET_A(op) + 1]);
			thread->top += BT_GET_A(op) + 2;
			if (BT_OBJECT_GET_TYPE(((bt_Closure*)obj)->fn) == BT_OBJECT_TYPE_FN) {
				thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, ((bt_Closure*)obj)->fn->stack_size, 0);
				call(context, thread, ((bt_Closure*)obj)->fn->module, ((bt_Closure*)obj)->fn->instructions.elements, ((bt_Closure*)obj)->fn->constants.elements, -2);
			}
			else {
				thread->callstack[thread->depth++] = BT_MAKE_STACKFRAME(obj, 0, 0);
				thread->native_stack[thread->native_depth].return_loc = -2;
				thread->native_depth++;
				((bt_NativeFn*)((bt_Closure*)obj)->fn)->fn(context, thread);
				thread->native_depth--;
			}

			thread->depth--;
			thread->top -= BT_GET_A(op) + 2;
			if (stack[BT_GET_A(op)] == BT_VALUE_NULL) { ip += BT_GET_IBC(op); }
		NEXT;

		CASE(LOAD_SUB_F): stack[BT_GET_A(op)] = bt_array_get(context, (bt_Array*)BT_AS_OBJECT(stack[BT_GET_B(op)]), (uint64_t)BT_AS_NUMBER(stack[BT_GET_C(op)])); NEXT;
		CASE(STORE_SUB_F): bt_array_set(context, (bt_Array*)BT_AS_OBJECT(stack[BT_GET_A(op)]), (uint64_t)BT_AS_NUMBER(stack[BT_GET_B(op)]), stack[BT_GET_C(op)]); NEXT;
		CASE(APPEND_F): bt_array_push(context, (bt_Array*)BT_AS_OBJECT(stack[BT_GET_A(op)]), stack[BT_GET_B(op)]); NEXT;

		CASE(IDX_EXT):;
#ifndef BOLT_USE_INLINE_THREADING
#ifdef BT_DEBUG
		default: assert(0 && "Unimplemented opcode!");
#else
		default: BT_ASSUME(0);
#endif
#endif
		}
	}

	BT_ASSUME(0);
}