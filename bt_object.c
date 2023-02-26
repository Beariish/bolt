#include "bt_object.h"

#include "bt_context.h"
#include "bt_userdata.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

static uint64_t MurmurOAAT64(const char* key, uint32_t len)
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
    if (BT_IS_OBJECT(value) && BT_AS_OBJECT(value)->type == BT_OBJECT_TYPE_STRING) return BT_AS_OBJECT(value);

    char buffer[4096];
    int32_t len = bt_to_string_inplace(ctx, buffer, 4096, value);
    return bt_make_string_len(ctx, buffer, len);
}

int32_t bt_to_string_inplace(bt_Context* ctx, char* buffer, uint32_t size, bt_Value value)
{
    int32_t len = 0;

    if (BT_IS_NUMBER(value)) {
        len = sprintf_s(buffer, size, "%f", BT_AS_NUMBER(value));
    }
    else {
        switch (BT_TYPEOF(value)) {
        case BT_TYPE_BOOL:
            if (BT_IS_TRUE(value)) len = sprintf_s(buffer, size, "true");
            else                   len = sprintf_s(buffer, size, "false");
            break;
        case BT_TYPE_NULL: len = sprintf_s(buffer, size, "null"); break;
        default: {
            bt_Object* obj = BT_AS_OBJECT(value);
            switch (obj->type) {
            case BT_OBJECT_TYPE_STRING: {
                bt_String* str = BT_AS_OBJECT(value);
                len = str->len;
                memcpy(buffer, str->str, len);
            } break;
            case BT_OBJECT_TYPE_TYPE:      len = sprintf_s(buffer, size, "Type(%s)", ((bt_Type*)obj)->name); break;
            case BT_OBJECT_TYPE_FN:        len = sprintf_s(buffer, size, "<0x%llx: %s>", value, ((bt_Fn*)obj)->signature->name); break;
            case BT_OBJECT_TYPE_NATIVE_FN: len = sprintf_s(buffer, size, "<Native(0x%llx): %s>", value, ((bt_NativeFn*)obj)->type->name); break;
            case BT_OBJECT_TYPE_TABLE: {
                bt_Table* tbl = obj;
                bt_Value format_fn = bt_table_get(tbl, BT_VALUE_OBJECT(ctx->meta_names.format));

                if (format_fn != BT_VALUE_NULL && ctx->current_thread) {
                    bt_push(ctx->current_thread, format_fn);
                    bt_push(ctx->current_thread, value);
                    bt_call(ctx->current_thread, 1);
                    bt_String* result = BT_AS_OBJECT(bt_pop(ctx->current_thread));
                    memcpy(buffer, result->str, result->len);
                    len = result->len;
                }
                else {
                    len = sprintf_s(buffer, size, "<0x%llx: table>", value);
                }
            } break;
            case BT_OBJECT_TYPE_IMPORT: {
                bt_ModuleImport* import = BT_AS_OBJECT(value);
                len = sprintf_s(buffer, size, "<0x%llx: Import(>", value);
                len += bt_to_string_inplace(ctx, buffer + len, size - len, BT_VALUE_OBJECT(import->name));
            } break;
            default: len = sprintf_s(buffer, size, "<0x%llx: object>", value); break;
            }
        }
        }
    }

    return len;
}

bt_String* bt_make_string(bt_Context* ctx, const char* str)
{
    return bt_make_string_len(ctx, str, strlen(str));
}

bt_String* bt_make_string_len(bt_Context* ctx, const char* str, uint32_t len)
{
    bt_String* result = BT_ALLOCATE(ctx, STRING, bt_String);
    result->str = ctx->alloc(len + 1);
    memcpy(result->str, str, len);
    result->str[len] = 0;
    result->len = len;
    result->hash = 0;
    return result;
}

bt_String* bt_make_string_hashed(bt_Context* ctx, const char* str)
{
    return bt_make_string_hashed_len(ctx, str, strlen(str));
}

bt_String* bt_make_string_hashed_len(bt_Context* ctx, const char* str, uint32_t len)
{
    bt_String* result = bt_make_string_len(ctx, str, len);
    return bt_hash_string(result);
}

bt_String* bt_make_string_moved(bt_Context* ctx, const char* str, uint32_t len)
{
    bt_String* result = BT_ALLOCATE(ctx, STRING, bt_String);
    result->str = str;
    result->len = len;
    result->hash = 0;
    return result;
}

bt_String* bt_hash_string(bt_String* str)
{
    if (str->hash == 0) {
        str->hash = MurmurOAAT64(str->str, str->len);
    }

    return str;
}

bt_StrSlice bt_as_strslice(bt_String* str)
{
    bt_StrSlice result;
    result.source = str->str;
    result.length = str->len;
    return result;
}

bt_Table* bt_make_table(bt_Context* ctx, uint16_t initial_size)
{
    bt_Table* table = BT_ALLOCATE(ctx, TABLE, bt_Table);
    table->pairs = BT_BUFFER_WITH_CAPACITY(ctx, bt_TablePair, initial_size);
    table->prototype = NULL;

    return table;
}

bt_bool bt_table_set(bt_Context* ctx, bt_Table* tbl, bt_Value key, bt_Value value)
{
    //if (bt_value_is_equal(key, BT_VALUE_OBJECT(ctx->meta_names.add))) __debugbreak();

    for (uint32_t i = 0; i < tbl->pairs.length; ++i) {
        bt_TablePair* pair = bt_buffer_at(&tbl->pairs, i);
        if (bt_value_is_equal(pair->key, key)) {
            pair->value = value;
            return BT_TRUE;
        }
    }

    bt_TablePair newpair;
    newpair.key = key;
    newpair.value = value;
    bt_buffer_push(ctx, &tbl->pairs, &newpair);

    return BT_FALSE;
}

bt_bool bt_table_set_cstr(bt_Context* ctx, bt_Table* tbl, const char* key, bt_Value value)
{
    bt_Value str = BT_VALUE_OBJECT(bt_make_string_hashed(ctx, key));
    return bt_table_set(ctx, tbl, str, value);
}

bt_Value bt_table_get(bt_Table* tbl, bt_Value key)
{
    for (uint32_t i = 0; i < tbl->pairs.length; ++i) {
        bt_TablePair* pair = bt_buffer_at(&tbl->pairs, i);
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

bt_Array* bt_make_array(bt_Context* ctx, uint16_t initial_capacity)
{
    bt_Array* arr = BT_ALLOCATE(ctx, ARRAY, bt_Array);
    arr->items = bt_buffer_new(ctx, sizeof(bt_Value));
    bt_buffer_reserve(ctx, &arr->items, initial_capacity);

    return arr;
}

uint64_t bt_array_push(bt_Context* ctx, bt_Array* arr, bt_Value value)
{
    bt_buffer_push(ctx, &arr->items, &value);
    return arr->items.length;
}

bt_Value bt_array_pop(bt_Array* arr)
{
    bt_Value result;
    bt_buffer_pop(&arr->items, &result);
    return result;
}

uint64_t bt_array_length(bt_Array* arr)
{
    return arr->items.length;
}

bt_bool bt_array_set(bt_Context* ctx, bt_Array* arr, uint64_t index, bt_Value value)
{
    if (index >= arr->items.length) bt_runtime_error(ctx->current_thread, "Array index out of bounds!");
    bt_Value* ref = bt_buffer_at(&arr->items, index);
    *ref = value;
    return BT_TRUE;
}

bt_Value bt_array_get(bt_Context* ctx, bt_Array* arr, uint64_t index)
{
    if (index >= arr->items.length) bt_runtime_error(ctx->current_thread, "Array index out of bounds!");
    return *(bt_Value*)bt_buffer_at(&arr->items, index);
}

bt_Fn* bt_make_fn(bt_Context* ctx, bt_Module* module, bt_Type* signature, bt_Buffer* constants, bt_Buffer* instructions, uint8_t stack_size)
{
    bt_Fn* result = BT_ALLOCATE(ctx, FN, bt_Fn);
    
    result->signature = signature;
    result->stack_size = stack_size;

    result->module = module;

    result->constants = bt_buffer_clone(ctx, constants);
    result->instructions = bt_buffer_clone(ctx, instructions);

    return result;
}

bt_Module* bt_make_module(bt_Context* ctx, bt_Buffer* imports)
{
    bt_Module* result = BT_ALLOCATE(ctx, MODULE, bt_Module);

    result->imports = bt_buffer_clone(ctx, imports);
    result->exports = bt_make_table(ctx, 0);
    result->type = bt_make_tableshape(ctx, "<module>", BT_TRUE);

    return result;
}

bt_Module* bt_make_user_module(bt_Context* ctx)
{
    bt_Module* result = BT_ALLOCATE(ctx, MODULE, bt_Module);
    
    result->stack_size = 0;
    result->imports = bt_buffer_empty();
    result->instructions = bt_buffer_empty();
    result->constants = bt_buffer_empty();
    result->exports = bt_make_table(ctx, 0);
    result->type = bt_make_tableshape(ctx, "<module>", BT_TRUE);

    return result;
}

bt_NativeFn* bt_make_native(bt_Context* ctx, bt_Type* signature, bt_NativeProc proc)
{
    bt_NativeFn* result = BT_ALLOCATE(ctx, NATIVE_FN, bt_NativeFn);
    result->type = signature;
    result->fn = proc;

    return result;
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
    bt_tableshape_add_layout(ctx, module->type, key, BT_AS_OBJECT(type));
    bt_table_set(ctx, module->exports, key, value);
}

bt_Value bt_get(bt_Context* ctx, bt_Object* obj, bt_Value key)
{
    switch (obj->type) {
    case BT_OBJECT_TYPE_TABLE:
        return bt_table_get(obj, key);
    case BT_OBJECT_TYPE_TYPE: {
        bt_Type* type = obj;
        return bt_table_get(type->prototype_values, key);
    } break;
    case BT_OBJECT_TYPE_ARRAY: {
        if (!BT_IS_NUMBER(key)) {
            bt_Value proto = bt_table_get(ctx->types.array->prototype_values, key);
            if (proto != BT_VALUE_NULL) return proto;
            
            bt_runtime_error(ctx->current_thread, "Attempted to index array with non-number!");
        }

        return bt_array_get(ctx, obj, BT_AS_NUMBER(key));
    } break;
    case BT_OBJECT_TYPE_USERDATA: {
        bt_Userdata* userdata = obj;
        bt_Type* type = userdata->type;
        
        bt_Buffer* fields = &type->as.userdata.fields;
        for (uint32_t i = 0; i < fields->length; i++) {
            bt_UserdataField* field = bt_buffer_at(fields, i);
            if (bt_value_is_equal(BT_VALUE_OBJECT(field->name), key)) {
                return field->getter(ctx, userdata->data, field->offset);
            }
        }

        bt_Buffer* methods = &type->as.userdata.functions;
        for (uint32_t i = 0; i < methods->length; i++) {
            bt_UserdataMethod* method = bt_buffer_at(methods, i);
            if (bt_value_is_equal(BT_VALUE_OBJECT(method->name), key)) {
                return BT_VALUE_OBJECT(method->fn);
            }
        }

        assert(0 && "This should never be reached due to typechecking!");
    } break;
    case BT_OBJECT_TYPE_STRING:
        return bt_table_get(ctx->types.string->prototype_values, key);
    default: {
        __debugbreak();
    } break;
    }
}

void bt_set(bt_Context* ctx, bt_Object* obj, bt_Value key, bt_Value value)
{
    switch (obj->type) {
    case BT_OBJECT_TYPE_TABLE:
        bt_table_set(ctx, obj, key, value);
        break;
    case BT_OBJECT_TYPE_ARRAY: {
        if (!BT_IS_NUMBER(key)) bt_runtime_error(ctx->current_thread, "Attempted to index array with non-number!");
        bt_array_set(ctx, obj, BT_AS_NUMBER(key), value);
    } break;
    case BT_OBJECT_TYPE_TYPE:
        bt_type_set_field(ctx, obj, key, value);
        break;
    case BT_OBJECT_TYPE_USERDATA: {
        bt_Userdata* userdata = obj;
        bt_Type* type = userdata->type;

        bt_Buffer* fields = &type->as.userdata.fields;
        for (uint32_t i = 0; i < fields->length; i++) {
            bt_UserdataField* field = bt_buffer_at(fields, i);
            if (bt_value_is_equal(BT_VALUE_OBJECT(field->name), key)) {
                field->setter(ctx, userdata->data, field->offset, value);
                return;
            }
        }

        assert(0 && "This should never be reached due to typechecking!");
    } break;
    default: __debugbreak();
    }
}
