#include "bt_buffer.h"
#include "bt_context.h"

#include <memory.h>
#include <assert.h>

bt_Buffer bt_buffer_with_capacity(bt_Context* context, uint32_t element_size, uint32_t capacity)
{
    bt_Buffer new_buffer;
    new_buffer.element_size = element_size;
    new_buffer.data = capacity > 0 ? context->alloc((size_t)element_size * capacity) : NULL;
    new_buffer.capacity = capacity;
    new_buffer.length = 0;

    return new_buffer;
}

bt_Buffer bt_buffer_new(bt_Context* context, uint32_t element_size)
{
    return bt_buffer_with_capacity(context, element_size, 0);
}

void bt_buffer_destroy(bt_Context* context, bt_Buffer* buffer)
{
    if (buffer->capacity > 0) {
        context->free(buffer->data);
        //buffer->data = NULL;
        buffer->capacity = 0;
        buffer->length = 0;
    }
}

bt_Buffer bt_buffer_empty()
{
    bt_Buffer result;
    result.capacity = 0;
    result.element_size = 0;
    result.data = 0;
    result.length = 0;

    return result;
}

bt_Buffer bt_buffer_clone(bt_Context* context, bt_Buffer* buffer)
{
    bt_Buffer result = bt_buffer_with_capacity(context, buffer->element_size, buffer->length);
    result.length = buffer->length;
    memcpy(result.data, buffer->data, bt_buffer_size(&result));
    return result;
}

bt_Buffer bt_buffer_move(bt_Buffer* buffer)
{
    bt_Buffer result;
    memcpy(&result, buffer, sizeof(bt_Buffer));
    buffer->data = 0;
    buffer->capacity = 0;
    buffer->length = 0;

    return result;
}

void bt_buffer_reserve(bt_Context* ctx, bt_Buffer* buffer, size_t cap)
{
    if (buffer->capacity >= cap) return;

    void* new_data = ctx->alloc(buffer->element_size * (size_t)cap);

    if (buffer->capacity > 0) {
        memcpy(new_data, buffer->data, buffer->capacity * (size_t)buffer->element_size);
        ctx->free(buffer->data);
    }

    buffer->data = new_data;
    buffer->capacity = cap;
}

bt_bool bt_buffer_push(bt_Context* context, bt_Buffer* buffer, void* elem)
{
    bt_bool allocated = BT_FALSE;

    if (buffer->length >= buffer->capacity) {
        uint32_t new_capacity = ((buffer->capacity * 3) / 2) + 1; // * 1.5 in integer form
        new_capacity = new_capacity == 0 ? 8 : new_capacity;

        bt_buffer_reserve(context, buffer, new_capacity);

        allocated = BT_TRUE;
    }

    memcpy((char*)buffer->data + (buffer->length * (size_t)buffer->element_size), elem, buffer->element_size);
    buffer->length++;

    return allocated;
}

void* bt_buffer_last(bt_Buffer* buffer)
{
    return bt_buffer_at(buffer, buffer->length - 1);
}

uint32_t bt_buffer_size(bt_Buffer* buffer)
{
    return buffer->element_size * buffer->length;
}

bt_bool bt_buffer_pop(bt_Buffer* buffer, void* output)
{
    if (buffer->length > 0) {
        memcpy(output, (char*)buffer->data + ((--buffer->length) * (size_t)buffer->element_size), buffer->element_size);
        return BT_TRUE;
    }

    return BT_FALSE;
}

void bt_buffer_append(bt_Context* context, bt_Buffer* dst, bt_Buffer* src)
{
    assert(dst->element_size == src->element_size);
    bt_buffer_reserve(context, dst, dst->length + src->length);
    memcpy((char*)dst->data + dst->length * dst->element_size, src->data, src->length * src->element_size);
    dst->length += src->length;
}
