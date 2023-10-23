#include "boltstd_core.h"

#include "../bt_embedding.h"

#include <stdio.h>
#include <time.h>

static __forceinline uint64_t get_timestamp()
{
	struct timespec ts;
#ifdef _MSC_VER
	timespec_get(&ts, TIME_UTC);
#else
	clock_gettime(CLOCK_REALTIME, &ts);
#endif

	uint64_t seconds = ts.tv_sec;
	uint64_t nano_seconds = ts.tv_nsec;

	return (seconds * 1'000'000) + (nano_seconds / 1'000);
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
	static char buffer[4096];
	int32_t pos = 0;

	uint8_t argc = bt_argc(thread);
	for (uint8_t i = 0; i < argc; ++i) {
		bt_Value arg = bt_arg(thread, i);
		pos += bt_to_string_inplace(ctx, buffer + pos, 4096 - pos, arg);

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

static void bt_throw(bt_Context* ctx, bt_Thread* thread)
{
	bt_String* message = bt_to_string(ctx, bt_arg(thread, 0));
	bt_runtime_error(thread, BT_STRING_STR(message), NULL);
}

static bt_Type* bt_error_type;
static bt_Value bt_error_what_key;

static void bt_error(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value what = bt_arg(thread, 0);
	bt_Table* result = bt_make_table(ctx, 1);
	result->prototype = bt_type_get_proto(ctx, bt_error_type);

	bt_table_set(ctx, result, bt_error_what_key, what);

	bt_return(thread, BT_VALUE_OBJECT(result));
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

	bt_register_module(context, BT_VALUE_CSTRING(context, "core"), module);
}