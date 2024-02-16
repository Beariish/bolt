#include "bt_object.h"

#include "bt_context.h"
#include "bt_userdata.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

uint64_t bt_hash_str(const char* key, uint32_t len)
{
    uint64_t h = 525201411107845655ull;
    for (uint32_t i = 0; i < len; ++i, ++key) {
        h ^= *key;
        h *= 0x5bd1e9955bd1e995;
        h ^= h >> 47;
    }
    return h;
}


bt_String* bt_to_string(bt_Context* ctx, bt_Value value)
{
    if (BT_IS_OBJECT(value) && BT_OBJECT_GET_TYPE(BT_AS_OBJECT(value)) == BT_OBJECT_TYPE_STRING) return (bt_String*)BT_AS_OBJECT(value);

    char buffer[4096];
    int32_t len = bt_to_string_inplace(ctx, buffer, 4096, value);
    return bt_make_string_len(ctx, buffer, len);
}

int32_t bt_to_string_inplace(bt_Context* ctx, char* buffer, uint32_t size, bt_Value value)
{
#ifdef _MSC_VER
#define BT_SPRINTF(...) sprintf_s(buffer, size, __VA_ARGS__)
#else
#define BT_SPRINTF(...) sprintf(buffer, __VA_ARGS__)
#endif
    int32_t len = 0;

    if (BT_IS_NUMBER(value)) {
        double n = BT_AS_NUMBER(value);

        if (floor(n) == n && !isnan(n) && !isinf(n)) {
            len = BT_SPRINTF("%lld", (uint64_t)n);
        }
        else {
            len = BT_SPRINTF("%f", n);
        }
    }
    else {
        switch (BT_TYPEOF(value)) {
        case BT_TYPE_BOOL:
            if (BT_IS_TRUE(value)) len = BT_SPRINTF("true");
            else                   len = BT_SPRINTF("false");
            break;
        case BT_TYPE_NULL: len = BT_SPRINTF("null"); break;
        case BT_TYPE_ENUM: len = BT_SPRINTF("%d", (uint32_t)BT_AS_ENUM(value)); break;
        default: {
            bt_Object* obj = BT_AS_OBJECT(value);
            switch (BT_OBJECT_GET_TYPE(obj)) {
            case BT_OBJECT_TYPE_STRING: {
                bt_String* str = (bt_String*)BT_AS_OBJECT(value);
                len = str->len;
                memcpy(buffer, BT_STRING_STR(str), len);
            } break;
            case BT_OBJECT_TYPE_TYPE:      len = BT_SPRINTF("%s", ((bt_Type*)obj)->name); break;
            case BT_OBJECT_TYPE_FN:        len = BT_SPRINTF("<0x%llx: %s>", value, ((bt_Fn*)obj)->signature->name); break;
            case BT_OBJECT_TYPE_CLOSURE:        len = BT_SPRINTF("<0x%llx: %s>", value, ((bt_Closure*)obj)->fn->signature->name); break;
            case BT_OBJECT_TYPE_NATIVE_FN: len = BT_SPRINTF("<Native(0x%llx): %s>", value, ((bt_NativeFn*)obj)->type ? ((bt_NativeFn*)obj)->type->name : "???"); break;
            case BT_OBJECT_TYPE_ARRAY: {
                bt_Array* arr = (bt_Array*)obj;
                len = BT_SPRINTF("<0x%llx: array[%d]>", value, arr->length);
            } break;
            case BT_OBJECT_TYPE_TABLE: {
                bt_Table* tbl = (bt_Table*)obj;
                bt_Value format_fn = bt_table_get(tbl, BT_VALUE_OBJECT(ctx->meta_names.format));

                if (format_fn != BT_VALUE_NULL && ctx->current_thread) {
                    bt_push(ctx->current_thread, format_fn);
                    bt_push(ctx->current_thread, value);
                    bt_call(ctx->current_thread, 1);
                    bt_String* result = (bt_String*)BT_AS_OBJECT(bt_pop(ctx->current_thread));
                    memcpy(buffer, BT_STRING_STR(result), result->len);
                    len = result->len;
                }
                else {
                    len = BT_SPRINTF("<0x%llx: table>", value);
                }
            } break;
            case BT_OBJECT_TYPE_IMPORT: {
                bt_ModuleImport* import = (bt_ModuleImport*)BT_AS_OBJECT(value);
                len = BT_SPRINTF("<0x%llx: Import(>", value);
                len += bt_to_string_inplace(ctx, buffer + len, size - len, BT_VALUE_OBJECT(import->name));
            } break;
            default: len = BT_SPRINTF("<0x%llx: object>", value); break;
            }
        }
        }
    }

    return len;
}

bt_String* bt_make_string(bt_Context* ctx, const char* str)
{
    return bt_make_string_len(ctx, str, (uint32_t)strlen(str));
}

bt_String* bt_make_string_len(bt_Context* ctx, const char* str, uint32_t len)
{
    if (len <= BT_STRINGTABLE_MAX_LEN) {
        return bt_get_or_make_interned(ctx, str, len);
    }

    bt_String* result = BT_ALLOCATE_INLINE_STORAGE(ctx, STRING, bt_String, len + 1);
    memcpy(BT_STRING_STR(result), str, len);
    BT_STRING_STR(result)[len] = 0;
    result->len = len;
    result->interned = 0;
    result->hash = 0;
    return result;
}

bt_String* bt_make_string_hashed(bt_Context* ctx, const char* str)
{
    return bt_make_string_hashed_len(ctx, str, (uint32_t)strlen(str));
}

bt_String* bt_make_string_hashed_len(bt_Context* ctx, const char* str, uint32_t len)
{
    bt_String* result = bt_make_string_len(ctx, str, len);
    return bt_hash_string(result);
}

bt_String* bt_make_string_hashed_len_escape(bt_Context* ctx, const char* str, uint32_t len)
{
    bt_String* result = bt_make_string_empty(ctx, len);
    char* strbuf = BT_STRING_STR(result);

    uint32_t idx = 0;
    bt_bool in_escape = BT_FALSE;
    for (uint32_t i = 0; i < len; ++i) {
        if (str[i] == '\\') {
            switch (str[++i]) {
            case 'n':  strbuf[idx++] = '\n'; break;
            case 't':  strbuf[idx++] = '\t'; break;
            case 'r':  strbuf[idx++] = '\r'; break;
            case '"':  strbuf[idx++] = '"';  break;
            case '\\': strbuf[idx++] = '\\'; break;
            default: bt_runtime_error(ctx->current_thread, "Unhandled escape character in string!", NULL);
            }
        }
        else {
            strbuf[idx++] = str[i];
        }
    }

    strbuf[idx] = 0;
    result->len = idx;

    return result;
}

bt_String* bt_make_string_empty(bt_Context* ctx, uint32_t len)
{
    bt_String* result = BT_ALLOCATE_INLINE_STORAGE(ctx, STRING, bt_String, len + 1);
    result->len = len;
    result->hash = 0;
    return result;
}

bt_String* bt_hash_string(bt_String* str)
{
    if (str->hash == 0) {
        str->hash = bt_hash_str(BT_STRING_STR(str), str->len);
    }

    return str;
}

bt_StrSlice bt_as_strslice(bt_String* str)
{
    bt_StrSlice result;
    result.source = BT_STRING_STR(str);
    result.length = str->len;
    return result;
}

const char* const bt_get_string(bt_String* str)
{
    return BT_STRING_STR(str);
}

bt_String* bt_concat_strings(bt_Context* ctx, bt_String* a, bt_String* b)
{
    uint32_t length = a->len + b->len;

    bt_String* result = bt_make_string_empty(ctx, length);
    char* added = BT_STRING_STR(result);
    memcpy(added, BT_STRING_STR(a), a->len);
    memcpy(added + a->len, BT_STRING_STR(b), b->len);
    added[length] = 0;

    return result;
}

bt_String* bt_append_cstr(bt_Context* ctx, bt_String* a, const char* b)
{
    uint32_t b_len = (uint32_t)strlen(b);
    uint32_t length = a->len + b_len;

    bt_String* result = bt_make_string_empty(ctx, length);
    char* added = BT_STRING_STR(result);
    memcpy(added, BT_STRING_STR(a), a->len);
    memcpy(added + a->len, b, b_len);
    added[length] = 0;

    return result;
}

bt_Table* bt_make_table(bt_Context* ctx, uint16_t initial_size)
{
    bt_Table* table = BT_ALLOCATE_INLINE_STORAGE(ctx, TABLE, bt_Table, sizeof(bt_TablePair) * initial_size);
    table->length = 0;
    table->is_inline = BT_TRUE;
    table->capacity = initial_size;
    table->inline_capacity = initial_size;
    table->prototype = NULL;
    table->outline = NULL;

    return table;
}

bt_bool bt_table_set(bt_Context* ctx, bt_Table* tbl, bt_Value key, bt_Value value)
{
    bt_String* as_str = (bt_String*)BT_VALUE_OBJECT(key);
    for (uint32_t i = 0; i < tbl->length; ++i) {
        bt_TablePair* pair = BT_TABLE_PAIRS(tbl) + i;
        if (bt_value_is_equal(pair->key, key)) {
            pair->value = value;
            return BT_TRUE;
        }
    }

    if (tbl->capacity <= tbl->length) {
        uint32_t old_cap = tbl->capacity;
        tbl->capacity *= 2;
        if (tbl->capacity == 0) tbl->capacity = 4;
        tbl->outline = ctx->realloc(tbl->outline, sizeof(bt_TablePair) * tbl->capacity);
        ctx->gc.byets_allocated += (tbl->capacity - old_cap) * sizeof(bt_TablePair);

        if (tbl->is_inline) {
            memcpy(tbl->outline, BT_TABLE_PAIRS(tbl), sizeof(bt_TablePair) * tbl->length);
            tbl->is_inline = BT_FALSE;
        }
    }

    (BT_TABLE_PAIRS(tbl) + tbl->length)->key = key;
    (BT_TABLE_PAIRS(tbl) + tbl->length)->value = value;
    tbl->length++;

    return BT_FALSE;
}

bt_bool bt_table_set_cstr(bt_Context* ctx, bt_Table* tbl, const char* key, bt_Value value)
{
    bt_Value str = BT_VALUE_OBJECT(bt_make_string_hashed(ctx, key));
    return bt_table_set(ctx, tbl, str, value);
}

bt_Value bt_table_get(bt_Table* tbl, bt_Value key)
{
    for (uint32_t i = 0; i < tbl->length; ++i) {
        bt_TablePair* pair = BT_TABLE_PAIRS(tbl) + i;
        if (bt_value_is_equal(pair->key, key)) {
            return pair->value;
        }
    }

    if (tbl->prototype) {
        return bt_table_get(tbl->prototype, key);
    }

    return BT_VALUE_NULL;
}

bt_Value bt_table_get_cstr(bt_Context* ctx, bt_Table* tbl, const char* key)
{
    bt_Value str = BT_VALUE_OBJECT(bt_make_string_hashed(ctx, key));
    return bt_table_get(tbl, str);
}

int16_t bt_table_get_idx(bt_Table* tbl, bt_Value key)
{
    for (uint32_t i = 0; i < tbl->length; ++i) {
        bt_TablePair* pair = BT_TABLE_PAIRS(tbl) + i;
        if (bt_value_is_equal(pair->key, key)) {
            return i;
        }
    }

    return -1;
}

BOLT_API bt_bool bt_table_delete_key(bt_Table* tbl, bt_Value key)
{
    bt_TablePair* start = BT_TABLE_PAIRS(tbl);

    for (uint32_t i = 0; i < tbl->length; i++) {
        if (bt_value_is_equal(key, start[i].key)) {
            memcpy(start + i, start + tbl->length - 1, sizeof(bt_TablePair));
            tbl->length--;

            return BT_TRUE;
        }
    }

    return BT_FALSE;
}

bt_Array* bt_make_array(bt_Context* ctx, uint32_t initial_capacity)
{
    bt_Array* arr = BT_ALLOCATE(ctx, ARRAY, bt_Array);
    arr->items = initial_capacity ? ctx->alloc(sizeof(bt_Value) * initial_capacity) : 0;
    arr->length = 0;
    arr->capacity = initial_capacity;

    ctx->gc.byets_allocated += initial_capacity * sizeof(bt_Value);

    return arr;
}

uint64_t bt_array_push(bt_Context* ctx, bt_Array* arr, bt_Value value)
{
    if (arr->length == arr->capacity) {
        uint32_t old_cap = arr->capacity;

        arr->capacity *= 2;
        if (arr->capacity == 0) arr->capacity = 4;
        arr->items = ctx->realloc(arr->items, sizeof(bt_Value) * arr->capacity);
        ctx->gc.byets_allocated += (arr->capacity - old_cap) * sizeof(bt_Value);
    }

    arr->items[arr->length++] = value;

    return arr->length;
}

bt_Value bt_array_pop(bt_Array* arr)
{
    if (arr->length > 0) {
        return arr->items[--arr->length];
    }

    return BT_VALUE_NULL;
}

uint64_t bt_array_length(bt_Array* arr)
{
    return arr->length;
}

bt_bool bt_array_set(bt_Context* ctx, bt_Array* arr, uint64_t index, bt_Value value)
{
    if (index >= arr->length) bt_runtime_error(ctx->current_thread, "Array index out of bounds!", NULL);
    arr->items[index] = value;
    return BT_TRUE;
}

bt_Value bt_array_get(bt_Context* ctx, bt_Array* arr, uint64_t index)
{
    if (index >= arr->length) bt_runtime_error(ctx->current_thread, "Array index out of bounds!", NULL);
    return arr->items[index];
}

bt_Fn* bt_make_fn(bt_Context* ctx, bt_Module* module, bt_Type* signature, bt_ValueBuffer* constants, bt_InstructionBuffer* instructions, uint8_t stack_size)
{
    bt_Fn* result = BT_ALLOCATE(ctx, FN, bt_Fn);
    return bt_make_fn_inplace(result, ctx, module, signature, constants, instructions, stack_size);
}

bt_Fn* bt_make_fn_inplace(bt_Fn* fn, bt_Context* ctx, bt_Module* module, bt_Type* signature, bt_ValueBuffer* constants, bt_InstructionBuffer* instructions, uint8_t stack_size)
{
    fn->signature = signature;
    fn->stack_size = stack_size;

    fn->module = module;

    bt_buffer_clone(ctx, &fn->constants, constants);
    bt_buffer_clone(ctx, &fn->instructions, instructions);
    ctx->gc.byets_allocated += fn->constants.capacity * sizeof(bt_Value);
    ctx->gc.byets_allocated += fn->instructions.capacity * sizeof(bt_Op);

    return fn;
}

bt_Module* bt_make_module(bt_Context* ctx, bt_ImportBuffer* imports)
{
    bt_Module* result = BT_ALLOCATE(ctx, MODULE, bt_Module);

    bt_buffer_clone(ctx, &result->imports, imports);
    result->exports = bt_make_table(ctx, 0);
    result->type = bt_make_tableshape(ctx, "<module>", BT_TRUE);
    
    result->debug_source = 0;
    result->stack_size = 0;
    result->name = 0;
    result->path = 0;

    bt_buffer_empty(&result->instructions);
    bt_buffer_empty(&result->constants);
    bt_buffer_empty(&result->debug_tokens);

    return result;
}

bt_Module* bt_make_user_module(bt_Context* ctx)
{
    bt_Module* result = BT_ALLOCATE(ctx, MODULE, bt_Module);
    
    result->debug_source = 0;
    result->stack_size = 0;
    bt_buffer_empty(&result->imports);
    bt_buffer_empty(&result->instructions);
    bt_buffer_empty(&result->constants);
    bt_buffer_empty(&result->debug_tokens);
    result->exports = bt_make_table(ctx, 0);
    result->type = bt_make_tableshape(ctx, "<module>", BT_TRUE);

    return result;
}

void bt_module_set_debug_info(bt_Module* module, bt_Tokenizer* tok)
{
    bt_buffer_move(&module->debug_tokens, &tok->tokens);
    module->debug_source = (char*)tok->source;
    tok->source = 0;
}

bt_NativeFn* bt_make_native(bt_Context* ctx, bt_Type* signature, bt_NativeProc proc)
{
    bt_NativeFn* result = BT_ALLOCATE(ctx, NATIVE_FN, bt_NativeFn);
    result->type = signature;
    result->fn = proc;

    return result;
}

bt_Type* bt_get_return_type(bt_Callable* callable)
{
    switch (BT_OBJECT_GET_TYPE(callable)) {
    case BT_OBJECT_TYPE_FN:
        return ((bt_Fn*)callable)->signature->as.fn.return_type;
    case BT_OBJECT_TYPE_CLOSURE:
        return ((bt_Closure*)callable)->fn->signature->as.fn.return_type;
    case BT_OBJECT_TYPE_NATIVE_FN:
        return ((bt_NativeFn*)callable)->type->as.fn.return_type;
    }
    
    return NULL;
}

bt_Userdata* bt_make_userdata(bt_Context* ctx, bt_Type* type, void* data, uint32_t size)
{
    bt_Userdata* result = BT_ALLOCATE(ctx, USERDATA, bt_Userdata);
    
    result->type = type;
    result->data = ctx->alloc(size);
    memcpy(result->data, data, size);
    
    return result;
}

void bt_module_export(bt_Context* ctx, bt_Module* module, bt_Type* type, bt_Value key, bt_Value value)
{
    bt_tableshape_add_layout(ctx, module->type, ctx->types.string, key, type);
    bt_table_set(ctx, module->exports, key, value);
}

void bt_module_export_native(bt_Context* ctx, bt_Module* module, const char* name, bt_NativeProc proc, bt_Type* ret_type, bt_Type** args, uint8_t arg_count)
{
    bt_Type* sig = bt_make_signature(ctx, ret_type, args, arg_count);
    bt_NativeFn* fn = bt_make_native(ctx, sig, proc);
    bt_module_export(ctx, module, sig, BT_VALUE_CSTRING(ctx, name), BT_VALUE_OBJECT(fn));
}

bt_Type* bt_module_get_export_type(bt_Module* module, bt_Value key)
{
    return bt_tableshape_get_layout(module->type, key);
}

bt_Value bt_module_get_export(bt_Module* module, bt_Value key)
{
    return bt_table_get(module->exports, key);
}

bt_Value bt_get(bt_Context* ctx, bt_Object* obj, bt_Value key)
{
    switch (BT_OBJECT_GET_TYPE(obj)) {
    case BT_OBJECT_TYPE_TABLE:
        return bt_table_get((bt_Table*)obj, key);
    case BT_OBJECT_TYPE_TYPE: {
        bt_Type* type = (bt_Type*)obj;
        return bt_table_get(type->prototype_values, key);
    } break;
    case BT_OBJECT_TYPE_ARRAY: {
        if (!BT_IS_NUMBER(key)) {
            bt_Value proto = bt_table_get(ctx->types.array->prototype_values, key);
            if (proto != BT_VALUE_NULL) return proto;
            
            bt_runtime_error(ctx->current_thread, "Attempted to index array with non-number!", NULL);
        }

        return bt_array_get(ctx, (bt_Array*)obj, (uint64_t)BT_AS_NUMBER(key));
    } break;
    case BT_OBJECT_TYPE_USERDATA: {
        bt_Userdata* userdata = (bt_Userdata*)obj;
        bt_Type* type = userdata->type;
        
        bt_FieldBuffer* fields = &type->as.userdata.fields;
        for (uint32_t i = 0; i < fields->length; i++) {
            bt_UserdataField* field = fields->elements + i;
            if (bt_value_is_equal(BT_VALUE_OBJECT(field->name), key)) {
                return field->getter(ctx, userdata->data, field->offset);
            }
        }

        bt_MethodBuffer* methods = &type->as.userdata.functions;
        for (uint32_t i = 0; i < methods->length; i++) {
            bt_UserdataMethod* method = methods->elements + i;
            if (bt_value_is_equal(BT_VALUE_OBJECT(method->name), key)) {
                return BT_VALUE_OBJECT(method->fn);
            }
        }

        assert(0 && "This should never be reached due to typechecking!");
    } break;
    case BT_OBJECT_TYPE_STRING:
        return bt_table_get(ctx->types.string->prototype_values, key);
    default: {
        uint8_t type = BT_OBJECT_GET_TYPE(obj);
        bt_runtime_error(ctx->current_thread, "Attempted to get field from fieldless type", NULL);
    } break;
    }

    return BT_VALUE_NULL;
}

void bt_set(bt_Context* ctx, bt_Object* obj, bt_Value key, bt_Value value)
{
    switch (BT_OBJECT_GET_TYPE(obj)) {
    case BT_OBJECT_TYPE_TABLE:
        bt_table_set(ctx, (bt_Table*)obj, key, value);
        break;
    case BT_OBJECT_TYPE_ARRAY: {
        if (!BT_IS_NUMBER(key)) bt_runtime_error(ctx->current_thread, "Attempted to index array with non-number!", NULL);
        bt_array_set(ctx, (bt_Array*)obj, (uint64_t)BT_AS_NUMBER(key), value);
    } break;
    case BT_OBJECT_TYPE_TYPE:
        bt_type_set_field(ctx, (bt_Type*)obj, key, value);
        break;
    case BT_OBJECT_TYPE_USERDATA: {
        bt_Userdata* userdata = (bt_Userdata*)obj;
        bt_Type* type = userdata->type;

        bt_FieldBuffer* fields = &type->as.userdata.fields;
        for (uint32_t i = 0; i < fields->length; i++) {
            bt_UserdataField* field = fields->elements + i;
            if (bt_value_is_equal(BT_VALUE_OBJECT(field->name), key)) {
                field->setter(ctx, userdata->data, field->offset, value);
                return;
            }
        }

        assert(0 && "This should never be reached due to typechecking!");
    } break;
    default: bt_runtime_error(ctx->current_thread, "Attempted to set field on fieldless type", NULL);
    }
}
