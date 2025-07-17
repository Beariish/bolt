#include "boltstd_regex.h"

#include "boltstd_core.h"

#include "picomatch/picomatch.h"

#include "../bt_embedding.h"

static const char* regex_type_name = "Regex";

typedef struct btregex_Regex {
    pm_Regex* regex;
    pm_Group* capture_groups;
    size_t group_count, regex_size;
} btregex_Regex;

static void btregex_compile(bt_Context* ctx, bt_Thread* thread)
{
    bt_String* source = (bt_String*)bt_object(bt_arg(thread, 0));

    const char* err = NULL;
    int size = pm_expsize(bt_string_get(source), &err);
    if (size == 0) {
        bt_return(thread, boltstd_make_error(ctx, err));
        return;
    }

    pm_Regex* result = bt_gc_alloc(ctx, size);
    if (!pm_compile(result, size, bt_string_get(source))) {
        bt_return(thread, boltstd_make_error(ctx, pm_geterror(result)));
        bt_gc_free(ctx, result, size);
        return;
    }

    pm_Group* groups = bt_gc_alloc(ctx, sizeof(pm_Group) * pm_getgroups(result));

    btregex_Regex wrapped;
    wrapped.regex = result;
    wrapped.capture_groups = groups;
    wrapped.group_count = pm_getgroups(result);
    wrapped.regex_size = size;

    bt_Module* module = bt_get_module(thread);
    bt_Type* regex_type = (bt_Type*)bt_object(bt_module_get_storage(module, BT_VALUE_CSTRING(ctx, regex_type_name)));
    
    bt_return(thread, bt_value(bt_make_userdata(ctx, regex_type, &wrapped, sizeof(btregex_Regex))));
}

static void btregex_regex_finalizer(bt_Context* ctx, bt_Userdata* userdata)
{
    btregex_Regex* regex = bt_userdata_get(userdata);
    if (regex->regex) {
        bt_gc_free(ctx, regex->regex, regex->regex_size);
        bt_gc_free(ctx, regex->capture_groups, sizeof(pm_Group) * regex->group_count);
        regex->regex = 0;
        regex->capture_groups = 0;
        regex->regex_size = 0;
        regex->group_count = 0;
    }
}

static void btregex_size(bt_Context* ctx, bt_Thread* thread)
{
    bt_Userdata* userdata = (bt_Userdata*)bt_object(bt_arg(thread, 0));
    btregex_Regex* regex = bt_userdata_get(userdata);
    bt_return(thread, bt_make_number((bt_number)regex->regex_size));
}

static void btregex_groups(bt_Context* ctx, bt_Thread* thread)
{
    bt_Userdata* userdata = (bt_Userdata*)bt_object(bt_arg(thread, 0));
    btregex_Regex* regex = bt_userdata_get(userdata);
    bt_return(thread, bt_make_number((bt_number)regex->group_count));
}

static void btregex_match(bt_Context* ctx, bt_Thread* thread)
{
    bt_Userdata* userdata = (bt_Userdata*)bt_object(bt_arg(thread, 0));
    btregex_Regex* regex = bt_userdata_get(userdata);
    bt_String* pattern = (bt_String*)bt_object(bt_arg(thread, 1));
    
    if (pm_match(regex->regex, bt_string_get(pattern), bt_string_length(pattern), regex->capture_groups, regex->group_count, 0)) {
        bt_Array* result = bt_make_array(ctx, regex->group_count);
        for (size_t i = 0; i < regex->group_count; i++) {
            bt_array_push(ctx, result, bt_value(bt_make_string_len(ctx, bt_string_get(pattern) + regex->capture_groups[i].start, regex->capture_groups[i].length)));
        }

        bt_return(thread, bt_value(result));
        return;
    }
    
    bt_return(thread, bt_make_null());
}

static bt_Value bt_regex_all_iter_fn;

static void btregex_all(bt_Context* ctx, bt_Thread* thread)
{
    bt_Value regex = bt_arg(thread, 0);
    bt_Value pattern = bt_arg(thread, 1);
    
    bt_push(thread, bt_regex_all_iter_fn);
    bt_push(thread, regex);
    bt_push(thread, pattern);
    bt_push(thread, bt_make_number(0));

    bt_return(thread, bt_make_closure(thread, 3));
}

static void bt_regex_all_iter(bt_Context* ctx, bt_Thread* thread)
{
    bt_Userdata* userdata = (bt_Userdata*)bt_object(bt_getup(thread, 0));
    btregex_Regex* regex = bt_userdata_get(userdata);
    bt_String* pattern = (bt_String*)bt_object(bt_getup(thread, 1));
    bt_number remainder_num = bt_get_number(bt_getup(thread, 2));
    
    size_t old_remainder = (size_t)remainder_num;
    size_t remainder = (size_t)remainder_num;
    
    const char* str = bt_string_get(pattern) + remainder;
    size_t len = bt_string_length(pattern) - remainder;
    if (pm_match(regex->regex, str, len, regex->capture_groups, regex->group_count, &remainder)) {
        bt_Array* result = bt_make_array(ctx, regex->group_count);
        for (size_t i = 0; i < regex->group_count; i++) {
            bt_array_push(ctx, result, bt_value(bt_make_string_len(ctx, str + regex->capture_groups[i].start, regex->capture_groups[i].length)));
        }

        bt_return(thread, bt_value(result));
        bt_setup(thread, 2, bt_make_number((bt_number)(old_remainder + remainder)));
        return;
    }
    
    bt_return(thread, bt_make_null());
}

void boltstd_open_regex(bt_Context* context)
{
    bt_Module* module = bt_make_module(context);

    bt_Type* regex_type = bt_make_userdata_type(context, regex_type_name);
    bt_userdata_type_set_finalizer(regex_type, btregex_regex_finalizer);
    bt_module_export(context, module, regex_type, BT_VALUE_CSTRING(context, regex_type_name), BT_VALUE_OBJECT(regex_type));
    bt_module_set_storage(module, BT_VALUE_CSTRING(context, regex_type_name), bt_value(regex_type));

    bt_Type* string = bt_type_string(context);
    bt_Type* number = bt_type_number(context);

    bt_Module* core_module = bt_find_module(context, BT_VALUE_CSTRING(context, "core"));
    bt_Type* bt_error_type = (bt_Type*)bt_object(bt_module_get_storage(core_module, BT_VALUE_CSTRING(context, bt_error_type_name)));
    
    bt_Type* compile_return = bt_make_or_extend_union(context, NULL, regex_type);
    compile_return = bt_make_or_extend_union(context, compile_return, bt_error_type);
    bt_module_export_native(context, module, "compile", btregex_compile, compile_return, &string, 1);

    bt_Type* size_sig = bt_make_signature_type(context, number, &regex_type, 1);
    bt_NativeFn* size_ref = bt_make_native(context, module, size_sig, btregex_size);
    bt_type_add_field(context, regex_type, size_sig, BT_VALUE_CSTRING(context, "size"), BT_VALUE_OBJECT(size_ref));
    bt_module_export(context, module, size_sig, BT_VALUE_CSTRING(context, "size"), BT_VALUE_OBJECT(size_ref));

    bt_Type* groups_sig = bt_make_signature_type(context, number, &regex_type, 1);
    bt_NativeFn* groups_ref = bt_make_native(context, module, groups_sig, btregex_groups);
    bt_type_add_field(context, regex_type, groups_sig, BT_VALUE_CSTRING(context, "groups"), BT_VALUE_OBJECT(groups_ref));
    bt_module_export(context, module, groups_sig, BT_VALUE_CSTRING(context, "groups"), BT_VALUE_OBJECT(groups_ref));

    bt_Type* match_return = bt_type_make_nullable(context, bt_make_array_type(context, string));
    bt_Type* match_args[] = { regex_type, string };
    bt_Type* match_sig = bt_make_signature_type(context, match_return, match_args, 2);
    bt_NativeFn* match_ref = bt_make_native(context, module, match_sig, btregex_match);
    bt_type_add_field(context, regex_type, match_sig, BT_VALUE_CSTRING(context, "eval"), BT_VALUE_OBJECT(match_ref));
    bt_module_export(context, module, match_sig, BT_VALUE_CSTRING(context, "eval"), BT_VALUE_OBJECT(match_ref));

    bt_Type* all_iter_sig = bt_make_signature_type(context, match_return, NULL, 0);
    bt_regex_all_iter_fn = bt_value(bt_make_native(context, module, all_iter_sig, bt_regex_all_iter));
    bt_type_add_field(context, regex_type, all_iter_sig, BT_VALUE_CSTRING(context, "$_all_iter"), bt_regex_all_iter_fn);
    
    bt_Type* all_sig = bt_make_signature_type(context, all_iter_sig, match_args, 2);
    bt_NativeFn* all_ref = bt_make_native(context, module, all_sig, btregex_all);
    bt_type_add_field(context, regex_type, all_sig, BT_VALUE_CSTRING(context, "all"), BT_VALUE_OBJECT(all_ref));
    bt_module_export(context, module, all_sig, BT_VALUE_CSTRING(context, "all"), BT_VALUE_OBJECT(all_ref));
    
    bt_register_module(context, BT_VALUE_CSTRING(context, "regex"), module);
}
