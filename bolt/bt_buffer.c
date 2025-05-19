#include "bt_buffer.h"
#include "bt_context.h"

#include <memory.h>
#include <assert.h>

void bt_buffer_reserve_(bt_Context* ctx, char** data, uint32_t* length, uint32_t* capacity, size_t element_size, size_t new_cap)
{
    if (*capacity >= new_cap) return;
    *data = bt_gc_realloc(ctx, *data, element_size * (*capacity), element_size * new_cap);
    *capacity = (uint32_t)new_cap;
}

void bt_buffer_expand(bt_Context* ctx, char** data, uint32_t* length, uint32_t* capacity, size_t element_size, size_t by)
{
    if ((*length) + by > *capacity) {
        bt_buffer_reserve_(ctx, data, length, capacity, element_size, (*capacity) * 2 + 1);
    }
}

void bt_buffer_free(bt_Context* ctx, char** data, uint32_t* length, uint32_t* capacity, size_t element_size)
{
    if (*length > 0) {
        bt_gc_free(ctx, *data, (*capacity) * element_size);
        *data = 0; *length = 0; *capacity = 0;
    }
}

void bt_buffer_clone_(bt_Context* ctx, char** data1, uint32_t* length1, uint32_t* capacity1, size_t element_size1, char** data2, uint32_t* length2, uint32_t* capacity2, size_t element_size2)
{
    assert(element_size1 == element_size2);
    bt_buffer_reserve_(ctx, data1, length1, capacity1, element_size1, *length2);
    memcpy(*data1, *data2, *length2 * element_size1);
    *length1 = *length2;
}

void bt_buffer_append_(bt_Context* ctx, char** data1, uint32_t* length1, uint32_t* capacity1, size_t element_size1, char** data2, uint32_t* length2, uint32_t* capacity2, size_t element_size2)
{
    assert(element_size1 == element_size2);
    bt_buffer_reserve_(ctx, data1, length1, capacity1, element_size1, *length1 + *length2);
    memcpy(*data1 + (*length1 * element_size1), *data2, *length2 * element_size1);
    *length1 += *length2;
}

void bt_buffer_move_(char** data1, uint32_t* length1, uint32_t* capacity1, size_t element_size1, char** data2, uint32_t* length2, uint32_t* capacity2, size_t element_size2)
{
    *data1 = *data2;
    *length1 = *length2;
    *capacity1 = *capacity2;

    *data2 = 0;
    *length2 = 0;
    *capacity2 = 0;
}
