#include "boltstd_core.h"

#include "../bt_embedding.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

const char* bt_error_type_name = "Error";
const char* bt_error_what_key_name = "what";

static uint64_t get_timestamp()
{
	struct timespec ts;
#ifdef _MSC_VER
	timespec_get(&ts, TIME_UTC);
#else
	clock_gettime(CLOCK_REALTIME, &ts);
#endif

	uint64_t seconds = ts.tv_sec;
	uint64_t nano_seconds = ts.tv_nsec;

	return (seconds * 1000000) + (nano_seconds / 1000);
}

static void bt_time(bt_Context* ctx, bt_Thread* thread)
{
	bt_return(thread, BT_VALUE_NUMBER(get_timestamp()));
}

static void bt_sameline(bt_Context* ctx, bt_Thread* thread)
{
	ctx->write(ctx, "\r");
}

static void bt_cout(bt_Context* ctx, bt_Thread* thread)
{
	uint8_t argc = bt_argc(thread);
	for (uint8_t i = 0; i < argc; ++i) {
		bt_Value arg = bt_arg(thread, i);
		bt_String* as_str = bt_to_string(ctx, arg);
		ctx->write(ctx, BT_STRING_STR(as_str));

		if (i < argc - 1) ctx->write(ctx, " ");
	}
}

static void bt_print(bt_Context* ctx, bt_Thread* thread)
{
	bt_cout(ctx, thread);
	ctx->write(ctx, "\n");
}

static void bt_write(bt_Context* ctx, bt_Thread* thread)
{
	bt_cout(ctx, thread);
}

static void bt_readline(bt_Context* ctx, bt_Thread* thread)
{
	char line[256];
	int matched = scanf("%255[^\n]%*c", line);

	// We got an empty line
	if (matched == 0) { 
		(void)scanf("%*c");
		line[0] = 0;
	}
	
	bt_String* result = bt_make_string(ctx, line);
	bt_return(thread, bt_value((bt_Object*)result));
}

static void bt_tostring(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value arg = bt_arg(thread, 0);
	bt_return(thread, BT_VALUE_OBJECT(bt_to_string(ctx, arg)));
}

static void bt_tonumber(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value arg = bt_arg(thread, 0);
	bt_String* as_str = (bt_String*)BT_AS_OBJECT(arg);

	char* end;
	char* start = BT_STRING_STR(as_str);
	double n = strtod(start, &end);

	if (start == end) {
		bt_return(thread, BT_VALUE_NULL);
	}
	else {
		bt_return(thread, BT_VALUE_NUMBER(n));
	}
}

static void bt_throw(bt_Context* ctx, bt_Thread* thread)
{
	bt_String* message = bt_to_string(ctx, bt_arg(thread, 0));
	bt_runtime_error(thread, BT_STRING_STR(message), NULL);
}

static void bt_error(bt_Context* ctx, bt_Thread* thread)
{
	bt_Module* module = bt_get_module(thread);
	bt_Type* error_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, bt_error_type_name)));
	
	bt_Value what = bt_arg(thread, 0);
	bt_Table* result = bt_make_table(ctx, 1);
	result->prototype = bt_type_get_proto(ctx, error_type);

	bt_table_set(ctx, result, BT_VALUE_CSTRING(ctx, bt_error_what_key_name), what);

	bt_return(thread, BT_VALUE_OBJECT(result));
}

static bt_Type* bt_protect_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc < 1) return NULL;
	bt_Type* arg = bt_type_dealias(args[0]);

	if (arg->category != BT_TYPE_CATEGORY_SIGNATURE) return NULL;

	bt_Type* return_type = ctx->types.null;
	if (arg->as.fn.return_type) return_type = arg->as.fn.return_type;

	bt_Type* new_args[16];
	new_args[0] = arg;

	for (uint8_t i = 0; i < arg->as.fn.args.length; ++i) {
		new_args[i + 1] = arg->as.fn.args.elements[i];
	}

	bt_Module* module = bt_find_module(ctx, BT_VALUE_CSTRING(ctx, "core"));
	bt_Type* error_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, bt_error_type_name)));
	
	bt_Type* options[] = { return_type, error_type };
	bt_Type* compound_return = bt_make_union_from(ctx, options, 2);
	return bt_make_signature_type(ctx, compound_return, new_args, 1 + arg->as.fn.args.length);
}

static void bt_protect(bt_Context* ctx, bt_Thread* thread)
{
	bt_Callable* to_call = (bt_Callable*)BT_AS_OBJECT(bt_arg(thread, 0));
	bt_Type* return_type = bt_get_return_type(to_call);

	bt_Thread* new_thread = bt_make_thread(ctx);
	new_thread->should_report = BT_FALSE;

	bt_bool success = bt_execute_with_args(ctx, new_thread, to_call, 
		thread->stack + thread->top + 1, bt_argc(thread) - 1);
	
	if (!success) {
		bt_Module* module = bt_get_module(thread);
		bt_Type* error_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, bt_error_type_name)));
	
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, error_type);

		bt_table_set(ctx, result, BT_VALUE_CSTRING(ctx, bt_error_what_key_name), BT_VALUE_OBJECT(new_thread->last_error));

		bt_return(thread, BT_VALUE_OBJECT(result));
	}
	else if (return_type) {
		bt_return(thread, bt_get_returned(new_thread));
	}
	else {
		bt_return(thread, BT_VALUE_NULL);
	}
	
	bt_destroy_thread(ctx, new_thread);
}

static bt_Type* bt_assert_type(bt_Context* ctx, bt_Type** args, uint8_t argc)
{
	if (argc < 1 || argc > 2) return NULL;
	bt_Type* arg = bt_type_dealias(args[0]);

	bt_Module* module = bt_find_module(ctx, BT_VALUE_CSTRING(ctx, "core"));
	bt_Type* error_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, bt_error_type_name)));
	
	if (arg->category != BT_TYPE_CATEGORY_UNION) return NULL;
	if (!bt_union_has_variant(arg, error_type)) return NULL;
	if (argc == 2 && bt_type_dealias(args[1]) != ctx->types.string) return NULL;

	bt_Type* return_type;
	if (arg->as.selector.types.length > 2) {
		return_type = bt_make_union(ctx);
	
		for (uint8_t i = 0; i < arg->as.selector.types.length; ++i) {
			bt_Type* next = arg->as.selector.types.elements[i];
			if (next == error_type) continue;

			bt_union_push_variant(ctx, return_type, next);
		}
	}
	else {
		return_type = arg->as.selector.types.elements[0];
		if (return_type == error_type) {
			return_type = arg->as.selector.types.elements[1];
		}
	}

	return bt_make_signature_type(ctx, return_type, args, argc);
}

static void bt_assert(bt_Context* ctx, bt_Thread* thread)
{
	bt_Module* module = bt_get_module(thread);
	bt_Type* error_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, bt_error_type_name)));
	
	bt_Value result = bt_arg(thread, 0);

	if (bt_is_type(result, error_type)) {
		bt_String* error_message = (bt_String*)BT_AS_OBJECT(bt_get(ctx, BT_AS_OBJECT(result), BT_VALUE_CSTRING(ctx, bt_error_what_key_name)));
		if (bt_argc(thread) == 2) {
			bt_String* new_error = (bt_String*)BT_AS_OBJECT(bt_arg(thread, 1));
			new_error = bt_string_append_cstr(ctx, new_error, ": ");
			error_message = bt_string_concat(ctx, new_error, error_message);
		}

		bt_runtime_error(thread, BT_STRING_STR(error_message), NULL);
	}
	else {
		bt_return(thread, result);
	}
}

void boltstd_open_core(bt_Context* context)
{
	bt_Module* module = bt_make_module(context);

	bt_Type* string = bt_type_string(context);
	
	bt_Type* noargs_sig = bt_make_signature_type(context, NULL, NULL, 0);
	bt_Type* printable_sig = bt_make_signature_vararg(context, noargs_sig, context->types.any);

	bt_module_export(context, module, printable_sig,
		BT_VALUE_CSTRING(context, "print"),
		BT_VALUE_OBJECT(bt_make_native(context, module, printable_sig, bt_print)));

	bt_module_export(context, module, printable_sig,
		BT_VALUE_CSTRING(context, "write"),
		BT_VALUE_OBJECT(bt_make_native(context, module, printable_sig, bt_write)));

	bt_module_export_native(context, module, "sameline",  bt_sameline, NULL,   NULL,                0);
	bt_module_export_native(context, module, "throw",     bt_throw,    NULL,   &string,             1);
	bt_module_export_native(context, module, "to_string", bt_tostring, string, &context->types.any, 1);
	bt_module_export_native(context, module, "read_line", bt_readline, string, NULL,                0);

	bt_Type* tonumber_ret = bt_type_make_nullable(context, context->types.number);
	bt_module_export_native(context, module, "to_number", bt_tonumber, tonumber_ret, &string, 1);

	bt_module_export_native(context, module, "time", bt_time, context->types.number, NULL, 0);

	bt_Type* bt_error_type = bt_make_tableshape_type(context, bt_error_type_name, BT_FALSE);
	bt_tableshape_add_layout(context, bt_error_type, string, BT_VALUE_CSTRING(context, bt_error_what_key_name), string);
	
	bt_module_export(context, module, bt_make_alias_type(context, "Error", bt_error_type), BT_VALUE_CSTRING(context, "Error"), BT_VALUE_OBJECT(bt_error_type));
	bt_module_set_storage(module, BT_VALUE_CSTRING(context, bt_error_type_name), bt_value((bt_Object*)bt_error_type));
	
	bt_module_export_native(context, module, "error", bt_error, bt_error_type, &string, 1);

	bt_Type* protect_sig = bt_make_poly_signature_type(context, "protect(fn(..T): R, ..T): R | Error", bt_protect_type);
	bt_module_export(context, module, protect_sig,
		BT_VALUE_CSTRING(context, "protect"),
		BT_VALUE_OBJECT(bt_make_native(context, module, protect_sig, bt_protect)));

	bt_Type* assert_sig = bt_make_poly_signature_type(context, "assert(T | Error, string): T", bt_assert_type);
	bt_module_export(context, module, assert_sig,
		BT_VALUE_CSTRING(context, "assert"),
		BT_VALUE_OBJECT(bt_make_native(context, module, assert_sig, bt_assert)));

	bt_register_module(context, BT_VALUE_CSTRING(context, "core"), module);
}

bt_Value boltstd_make_error(bt_Context* context, const char* message)
{
	bt_Module* module = bt_find_module(context, BT_VALUE_CSTRING(context, "core"));
	bt_Type* error_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(context, bt_error_type_name)));
	
	bt_String* what = bt_make_string(context, message);
	bt_Table* result = bt_make_table_from_proto(context, error_type);
	bt_table_set(context, result, BT_VALUE_CSTRING(context, bt_error_what_key_name), BT_VALUE_OBJECT(what));
	return BT_VALUE_OBJECT(result);
}
