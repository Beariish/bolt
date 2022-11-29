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


bt_String* bt_make_string(bt_Context* ctx, char* str)
{
    return bt_make_string_len(ctx, str, strlen(str));
}

bt_String* bt_make_string_len(bt_Context* ctx, char* str, uint32_t len)
{
    bt_String* result = BT_ALLOCATE(ctx, STRING, bt_String);
    result->str = ctx->alloc(len);
    memcpy(result->str, str, len);
    result->len = len;
    result->hash = 0;
    return result;
}

bt_String* bt_make_string_hashed(bt_Context* ctx, char* str)
{
    return bt_make_string_hashed_len(ctx, str, strlen(str));
}

bt_String* bt_make_string_hashed_len(bt_Context* ctx, char* str, uint32_t len)
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
