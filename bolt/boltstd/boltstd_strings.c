#include "boltstd_strings.h"

#include "../bt_embedding.h"

#include <memory.h>
#include <string.h>
#include <stdio.h>

static void bt_str_length(bt_Context* ctx, bt_Thread* thread)
{
	bt_Value arg = bt_arg(thread, 0);
	bt_String* as_str = (bt_String*)BT_AS_OBJECT(arg);
	bt_return(thread, BT_VALUE_NUMBER(as_str->len));
}

static void bt_str_substring(bt_Context* ctx, bt_Thread* thread)
{
	bt_String* str = (bt_String*)BT_AS_OBJECT(bt_arg(thread, 0));
	uint32_t start = (uint32_t)BT_AS_NUMBER(bt_arg(thread, 1));
	uint32_t length = (uint32_t)BT_AS_NUMBER(bt_arg(thread, 2));

	if (start < 0 || start > str->len) bt_runtime_error(thread, "Attempted to substring outside of bounds!", NULL);
	if (length <= 0 || start + length > str->len) bt_runtime_error(thread, "Invalid size for substring!", NULL);

	bt_String* substring = bt_make_string_len(ctx, BT_STRING_STR(str) + start, length);
	bt_return(thread, BT_VALUE_OBJECT(substring));
}

static void bt_string_concat(bt_Context* ctx, bt_Thread* thread)
{
	uint8_t argc = bt_argc(thread);

	uint32_t total_len = 0;
	for (uint8_t i = 0; i < argc; i++) {
		bt_String* str = (bt_String*)bt_get_object(bt_arg(thread, i));
		total_len += str->len;
	}

	bt_String* result = bt_make_string_empty(ctx, total_len);

	uint32_t progress = 0;
	for (uint8_t i = 0; i < argc; i++) {
		bt_String* str = (bt_String*)bt_get_object(bt_arg(thread, i));
		memcpy(BT_STRING_STR(result) + progress, BT_STRING_STR(str), str->len);
		progress += str->len;
	}

	BT_STRING_STR(result)[total_len] = 0;

	bt_return(thread, BT_VALUE_OBJECT(result));
}

typedef bt_Buffer(char) bt_StringBuffer;

static void push_string(bt_Context* ctx, bt_StringBuffer* output, const char* cstr, size_t len)
{
	bt_buffer_reserve(output, ctx, output->length + len);

	for (uint32_t i = 0; i < len; i++) {
		bt_buffer_push(ctx, output, cstr[i]);
	}
}

static void sprint_invalid(bt_Context* ctx, bt_StringBuffer* output) 
{
	const char* to_format = "<invalid>";
	push_string(ctx, output, to_format, strlen(to_format));
}

static void sptint_unknown_specifier(bt_Context* ctx, bt_StringBuffer* output)
{
	const char* to_format = "<unknown specicier>";
	push_string(ctx, output, to_format, strlen(to_format));
}

static void sprint_uint64_t(bt_Context* ctx, bt_StringBuffer* output, bt_Value value)
{
	if (!bt_is_number(value)) {
		sprint_invalid(ctx, output);
		return;
	}

	char buf[128];
	int32_t len = sprintf(buf, "%lld", (uint64_t)BT_AS_NUMBER(value));

	push_string(ctx, output, buf, len);
}

static void sprint_float(bt_Context* ctx, bt_StringBuffer* output, bt_Value value)
{
	if (!bt_is_number(value)) {
		sprint_invalid(ctx, output);
		return;
	}

	char buf[128];
	int32_t len = sprintf(buf, "%f", BT_AS_NUMBER(value));

	push_string(ctx, output, buf, len);
}

static void sprint_string(bt_Context* ctx, bt_StringBuffer* output, bt_Value value)
{
	bt_String* as_str = bt_to_string(ctx, value);

	push_string(ctx, output, BT_STRING_STR(as_str), as_str->len);
}

static void bt_string_format(bt_Context* ctx, bt_Thread* thread)
{
	uint8_t argc = bt_argc(thread);

	bt_String* format = (bt_String*)bt_get_object(bt_arg(thread, 0));

	bt_StringBuffer output;
	bt_buffer_empty(&output);

	uint8_t current_arg = 1;

	char* current_format = BT_STRING_STR(format);
	while (*current_format) {
		if (*current_format == '%') {
			char specifier = *(++current_format);
			current_format++;

			switch (specifier) {
			case '%':
				bt_buffer_push(ctx, &output, '%');
				break;
			case 'd': case 'i':
				sprint_uint64_t(ctx, &output, current_arg < argc ? bt_arg(thread, current_arg++) : BT_VALUE_NULL);
				break;
			case 'f':
				sprint_float(ctx, &output, current_arg < argc ? bt_arg(thread, current_arg++) : BT_VALUE_NULL);
				break;
			case 's': case 'v':
				sprint_string(ctx, &output, current_arg < argc ? bt_arg(thread, current_arg++) : BT_VALUE_NULL);
				break;
			default:
				sptint_unknown_specifier(ctx, &output);
				break;
			}
		}
		else {
			bt_buffer_push(ctx, &output, *current_format);
			current_format++;
		}
	}

	bt_buffer_push(ctx, &output, '\0');

	bt_String* result = bt_make_string_len(ctx, output.elements, output.length);
	bt_buffer_destroy(ctx, &output);

	bt_return(thread, BT_VALUE_OBJECT(result));
}

static void bt_string_find(bt_Context* ctx, bt_Thread* thread)
{
	bt_String* source = (bt_String*)bt_get_object(bt_arg(thread, 0));
	bt_String* needle = (bt_String*)bt_get_object(bt_arg(thread, 1));

	for (uint32_t i = 0; i < source->len - needle->len; ++i) {
		const char* start = BT_STRING_STR(source) + i;

		int32_t found = (int32_t)i;
		for (uint32_t j = 0; j < needle->len; j++) {
			if (start[j] != BT_STRING_STR(needle)[j]) {
				found = -1;
				break;
			}
		}

		if (found != -1) {
			bt_return(thread, BT_VALUE_NUMBER((double)found));
			return;
		}
	}

	bt_return(thread, BT_VALUE_NUMBER((double)-1));
}

static void bt_string_replace(bt_Context* ctx, bt_Thread* thread) 
{
	bt_String* orig_str = (bt_String*)bt_get_object(bt_arg(thread, 0));
	bt_String* rep_str = (bt_String*)bt_get_object(bt_arg(thread, 1));
	bt_String* with_str = (bt_String*)bt_get_object(bt_arg(thread, 2));

	const char* orig = BT_STRING_STR(orig_str);
	const char* rep = BT_STRING_STR(rep_str);
	const char* with = BT_STRING_STR(with_str);

	size_t len_rep = strlen(rep);
	if (len_rep == 0) bt_runtime_error(thread, "Replacement string cannot be empty!", NULL); // empty rep causes infinite loop during count
	size_t len_with = strlen(with);

	char* tmp;
	char* ins = orig;
	int count;
	for (count = 0; tmp = strstr(ins, rep); ++count) {
		ins = tmp + len_rep;
	}

	bt_String* result = bt_make_string_empty(ctx, strlen(orig) + (len_with - len_rep) * count + 1);
	tmp = BT_STRING_STR(result);

	int len_front;
	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep;
	}
	strcpy(tmp, orig);

	bt_return(thread, BT_VALUE_OBJECT(result));
}

static void bt_string_reverse(bt_Context* ctx, bt_Thread* thread) 
{
	bt_String* arg = (bt_String*)bt_get_object(bt_arg(thread, 0));

	bt_String* result = bt_make_string_empty(ctx, arg->len);
	BT_STRING_STR(result)[arg->len] = 0;

	for (uint32_t i = 0; i < arg->len; ++i) {
		BT_STRING_STR(result)[i] = BT_STRING_STR(arg)[arg->len - i - 1];
	}

	bt_return(thread, BT_VALUE_OBJECT(result));
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

	bt_Type* concat_sig = bt_make_vararg(context, bt_make_method(context, string, &string, 1), string);
	fn_ref = bt_make_native(context, concat_sig, bt_string_concat);

	bt_type_add_field(context, string, concat_sig, BT_VALUE_CSTRING(context, "concat"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, concat_sig, BT_VALUE_CSTRING(context, "concat"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* format_sig = bt_make_vararg(context, bt_make_method(context, string, &string, 1), context->types.any);
	fn_ref = bt_make_native(context, format_sig, bt_string_format);

	bt_type_add_field(context, string, format_sig, BT_VALUE_CSTRING(context, "format"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, format_sig, BT_VALUE_CSTRING(context, "format"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* find_args[] = { string, string };
	bt_Type* find_sig = bt_make_method(context, context->types.number, find_args, 2);
	fn_ref = bt_make_native(context, find_sig, bt_string_find);

	bt_type_add_field(context, string, find_sig, BT_VALUE_CSTRING(context, "find"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, find_sig, BT_VALUE_CSTRING(context, "find"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* replace_args[] = { string, string, string };
	bt_Type* replace_sig = bt_make_method(context, string, replace_args, 3);
	fn_ref = bt_make_native(context, replace_sig, bt_string_replace);

	bt_type_add_field(context, string, replace_sig, BT_VALUE_CSTRING(context, "replace"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, replace_sig, BT_VALUE_CSTRING(context, "replace"), BT_VALUE_OBJECT(fn_ref));

	bt_Type* reverse_sig = bt_make_method(context, string, &string, 1);
	fn_ref = bt_make_native(context, reverse_sig, bt_string_reverse);

	bt_type_add_field(context, string, reverse_sig, BT_VALUE_CSTRING(context, "reverse"), BT_VALUE_OBJECT(fn_ref));
	bt_module_export(context, module, reverse_sig, BT_VALUE_CSTRING(context, "reverse"), BT_VALUE_OBJECT(fn_ref));

	bt_register_module(context, BT_VALUE_CSTRING(context, "strings"), module);
}