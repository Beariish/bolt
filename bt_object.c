#include "bt_object.h"

#include "bt_context.h"
#include <string.h>

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
    for (uint32_t i = 0; i < tbl->pairs.length; ++i) {
        bt_TablePair* pair = (bt_TablePair*)bt_buffer_at(&tbl->pairs, i);
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
    bt_Value str = BT_VALUE_STRING(bt_make_string_hashed(ctx, key));
    return bt_table_set(ctx, tbl, str, value);
}

bt_Value bt_table_get(bt_Table* tbl, bt_Value key)
{
    for (uint32_t i = 0; i < tbl->pairs.length; ++i) {
        bt_TablePair* pair = (bt_TablePair*)bt_buffer_at(&tbl->pairs, i);
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
    bt_Value str = BT_VALUE_STRING(bt_make_string_hashed(ctx, key));
    return bt_table_get(tbl, str);
}

bt_Fn* bt_make_fn(bt_Context* ctx, bt_Type* signature, bt_Buffer* constants, bt_Buffer* instructions, uint8_t stack_size)
{
    bt_Fn* result = BT_ALLOCATE(ctx, FN, bt_Fn);
    
    result->signature = signature;
    result->stack_size = stack_size;

    result->constants = bt_buffer_clone(ctx, constants);
    result->instructions = bt_buffer_clone(ctx, instructions);

    return result;
}

bt_Module* bt_make_module(bt_Context* ctx, bt_Buffer* imports, bt_Buffer* constants, bt_Buffer* instructions, uint8_t stack_size)
{
    bt_Module* result = BT_ALLOCATE(ctx, MODULE, bt_Module);

    result->stack_size = stack_size;
    result->imports = bt_buffer_clone(ctx, imports);
    result->constants = bt_buffer_clone(ctx, constants);
    result->instructions = bt_buffer_clone(ctx, instructions);

    return result;
}
