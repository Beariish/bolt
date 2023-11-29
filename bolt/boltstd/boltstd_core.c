#include "boltstd_core.h"

#include "../bt_embedding.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
	printf("\r");
}

static void bt_cout(const char* fmt, bt_Context* ctx, bt_Thread* thread)
{
	static char buffer[4096*128];
	int32_t pos = 0;

	uint8_t argc = bt_argc(thread);
	for (uint8_t i = 0; i < argc; ++i) {
		bt_Value arg = bt_arg(thread, i);
		pos += bt_to_string_inplace(ctx, buffer + pos, (4096 * 128) - pos, arg);

		if (i < argc - 1) buffer[pos++] = ' ';
	}

	buffer[pos] = 0;
	printf(fmt, buffer);
}

static void bt_print(bt_Context* ctx, bt_Thread* thread)
{
	bt_cout("%s\n", ctx, thread);
}

static void bt_write(bt_Context* ctx, bt_Thread* thread)
{
	bt_cout("%s", ctx, thread);
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

bt_Type* bt_error_type;
bt_Value bt_error_what_key;

static void bt_error(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value what = bt_arg(thread, 0);
	bt_Table* result = bt_make_table(ctx, 1);
	result->prototype = bt_type_get_proto(ctx, bt_error_type);

	bt_table_set(ctx, result, bt_error_what_key, what);

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

	bt_Type* compound_return = bt_make_union(ctx);
	bt_push_union_variant(ctx, compound_return, return_type);
	bt_push_union_variant(ctx, compound_return, bt_error_type);

	return bt_make_signature(ctx, compound_return, new_args, 1 + arg->as.fn.args.length);
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
		bt_Table* result = bt_make_table(ctx, 1);
		result->prototype = bt_type_get_proto(ctx, bt_error_type);

		bt_table_set(ctx, result, bt_error_what_key, BT_VALUE_OBJECT(new_thread->last_error));

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

	if (arg->category != BT_TYPE_CATEGORY_UNION) return NULL;
	if (!bt_union_has_variant(arg, bt_error_type)) return NULL;
	if (argc == 2 && bt_type_dealias(args[1]) != ctx->types.string) return NULL;

	bt_Type* return_type;
	if (arg->as.selector.types.length > 2) {
		return_type = bt_make_union(ctx);
	
		for (uint8_t i = 0; i < arg->as.selector.types.length; ++i) {
			bt_Type* next = arg->as.selector.types.elements[i];
			if (next == bt_error_type) continue;

			bt_push_union_variant(ctx, return_type, next);
		}
	}
	else {
		return_type = arg->as.selector.types.elements[0];
		if (return_type == bt_error_type) {
			return_type = arg->as.selector.types.elements[1];
		}
	}

	return bt_make_signature(ctx, return_type, args, argc);
}

static void bt_assert(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value result = bt_arg(thread, 0);

	if (bt_is_type(result, bt_error_type)) {
		bt_String* error_message = (bt_String*)BT_AS_OBJECT(bt_get(ctx, BT_AS_OBJECT(result), bt_error_what_key));
		if (bt_argc(thread) == 2) {
			bt_String* new_error = (bt_String*)BT_AS_OBJECT(bt_arg(thread, 1));
			new_error = bt_append_cstr(ctx, new_error, ": ");
			error_message = bt_concat_strings(ctx, new_error, error_message);
		}

		bt_runtime_error(thread, BT_STRING_STR(error_message), NULL);
	}
	else {
		bt_return(thread, result);
	}
}

void boltstd_open_core(bt_Context* context)
{
	bt_Module* module = bt_make_user_module(context);

	bt_Type* noargs_sig = bt_make_signature(context, NULL, NULL, 0);
	bt_Type* printable_sig = bt_make_vararg(context, noargs_sig, context->types.any);

	bt_module_export(context, module, printable_sig,
		BT_VALUE_CSTRING(context, "print"),
		BT_VALUE_OBJECT(bt_make_native(context, printable_sig, bt_print)));

	bt_module_export(context, module, printable_sig,
		BT_VALUE_CSTRING(context, "write"),
		BT_VALUE_OBJECT(bt_make_native(context, printable_sig, bt_write)));

	bt_module_export(context, module, noargs_sig,
		BT_VALUE_CSTRING(context, "sameline"),
		BT_VALUE_OBJECT(bt_make_native(context, noargs_sig, bt_sameline)));

	bt_Type* throw_sig = bt_make_signature(context, NULL, &context->types.string, 1);
	bt_module_export(context, module, throw_sig,
		BT_VALUE_CSTRING(context, "throw"),
		BT_VALUE_OBJECT(bt_make_native(context, throw_sig, bt_throw)));

	bt_Type* tostring_sig = bt_make_signature(context, context->types.string, &context->types.any, 1);
	bt_module_export(context, module, tostring_sig,
		BT_VALUE_CSTRING(context, "to_string"),
		BT_VALUE_OBJECT(bt_make_native(context, tostring_sig, bt_tostring)));

	bt_Type* tonumber_sig = bt_make_signature(context, bt_make_nullable(context, context->types.number), &context->types.string, 1);
	bt_module_export(context, module, tonumber_sig,
		BT_VALUE_CSTRING(context, "to_number"),
		BT_VALUE_OBJECT(bt_make_native(context, tonumber_sig, bt_tonumber)));

	bt_Type* time_sig = bt_make_signature(context, context->types.number, NULL, 0);
	bt_module_export(context, module, time_sig,
		BT_VALUE_CSTRING(context, "time"),
		BT_VALUE_OBJECT(bt_make_native(context, time_sig, bt_time)));

	bt_error_type = bt_make_tableshape(context, "Error", BT_FALSE);
	bt_error_what_key = BT_VALUE_CSTRING(context, "what");
	bt_tableshape_add_layout(context, bt_error_type, context->types.string, bt_error_what_key, context->types.string);
	bt_module_export(context, module, bt_make_alias(context, "Error", bt_error_type), BT_VALUE_CSTRING(context, "Error"), BT_VALUE_OBJECT(bt_error_type));

	bt_Type* error_sig = bt_make_signature(context, bt_error_type, &context->types.string, 1);
	bt_module_export(context, module, error_sig,
		BT_VALUE_CSTRING(context, "error"),
		BT_VALUE_OBJECT(bt_make_native(context, error_sig, bt_error)));

	bt_Type* protect_sig = bt_make_poly_signature(context, "protect(fn(..T): R, ..T): R | Error", bt_protect_type);
	bt_module_export(context, module, protect_sig,
		BT_VALUE_CSTRING(context, "protect"),
		BT_VALUE_OBJECT(bt_make_native(context, protect_sig, bt_protect)));

	bt_Type* assert_sig = bt_make_poly_signature(context, "assert(T | Error, string): T", bt_assert_type);
	bt_module_export(context, module, assert_sig,
		BT_VALUE_CSTRING(context, "assert"),
		BT_VALUE_OBJECT(bt_make_native(context, assert_sig, bt_assert)));

	bt_register_module(context, BT_VALUE_CSTRING(context, "core"), module);
}