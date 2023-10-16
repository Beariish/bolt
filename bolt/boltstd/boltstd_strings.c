#include "boltstd_strings.h"

#include "../bt_embedding.h"

static void bt_str_length(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value arg = bt_arg(thread, 0);
	bt_String* as_str = (bt_String*)BT_AS_OBJECT(arg);
	bt_return(thread, BT_VALUE_NUMBER(as_str->len));
}

static void bt_str_substring(bt_Context* ctx, bt_Thread* thread)
{
	bt_String* str = BT_AS_OBJECT(bt_arg(thread, 0));
	uint32_t start = (uint32_t)BT_AS_NUMBER(bt_arg(thread, 1));
	uint32_t length = (uint32_t)BT_AS_NUMBER(bt_arg(thread, 2));

	if (start < 0 || start > str->len) bt_runtime_error(thread, "Attempted to substring outside of bounds!", NULL);
	if (length <= 0 || start + length > str->len) bt_runtime_error(thread, "Invalid size for substring!", NULL);

	bt_String* substring = bt_make_string_len(ctx, BT_STRING_STR(str) + start, length);
	bt_return(thread, BT_VALUE_OBJECT(substring));
}

void boltstd_open_strings(bt_Context* context)
{
	bt_Module* module = bt_make_user_module(context);

	bt_Type* string = context->types.string;

	bt_Type* length_sig = bt_make_method(context, context->types.number, &context->types.string, 1);
	bt_NativeFn* fn_ref = bt_make_native(context, length_sig, bt_str_length);

	bt_type_add_field(context, string, length_sig, BT_VALUE_CSTRING(context, "length"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, length_sig, BT_VALUE_CSTRING(context, "length"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* substring_args[] = { context->types.string, context->types.number, context->types.number };
	bt_Type* substring_sig = bt_make_method(context, context->types.string, substring_args, 3);
	bt_NativeFn* substring_ref = bt_make_native(context, substring_sig, bt_str_substring);

	bt_type_add_field(context, string, substring_sig, BT_VALUE_CSTRING(context, "substring"), BT_VALUE_OBJECT(substring_ref));
	bt_module_export(context, module, substring_sig, BT_VALUE_CSTRING(context, "substring"), BT_VALUE_OBJECT(substring_ref));

	bt_register_module(context, BT_VALUE_CSTRING(context, "strings"), module);
}