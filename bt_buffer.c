#include "bt_buffer.h"
#include "bt_context.h"

#include <memory.h>

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
        buffer->data = NULL;
        buffer->capacity = 0;
        buffer->length = 0;
    }
}

bt_bool bt_buffer_push(bt_Context* context, bt_Buffer* buffer, void* elem)
{
    bt_bool allocated = BT_FALSE;

    if (buffer->length == buffer->capacity) {
        uint32_t new_capacity = (buffer->capacity * 3) / 2; // * 1.5 in integer form
        new_capacity = new_capacity == 0 ? 8 : new_capacity;

        void* new_data = context->alloc(buffer->element_size * (size_t)new_capacity);
        
        if (buffer->capacity > 0) {
            memcpy(new_data, buffer->data, buffer->capacity * (size_t)buffer->element_size);
            context->free(buffer->data);
        }

        buffer->data = new_data;
        buffer->capacity = new_capacity;
        allocated = BT_TRUE;
    }

    memcpy((char*)buffer->data + (buffer->length * (size_t)buffer->element_size), elem, buffer->element_size);
    buffer->length++;

    return allocated;
}

void* bt_buffer_at(bt_Buffer* buffer, uint32_t index)
{
    return (void*)((char*)buffer->data + (index * (size_t)buffer->element_size));
}

void* bt_buffer_last(bt_Buffer* buffer)
{
    return bt_buffer_at(buffer, buffer->length - 1);
}

static bt_Bucket* make_bucket(bt_Context* context, uint32_t bucket_size, uint32_t element_size, uint32_t base_index)
{
    bt_Bucket* result = context->alloc(sizeof(bt_Bucket));
    result->base_index = base_index;
    result->element_size = element_size;
    result->capacity = bucket_size;
    result->length = 0;
    result->next = NULL;

    result->data = context->alloc(element_size * bucket_size);

    return result;
}

bt_BucketedBuffer bt_bucketed_buffer_new(bt_Context* context, uint32_t bucket_size, uint32_t element_size)
{
    bt_BucketedBuffer result;
    result.bucket_size = bucket_size;
    result.element_size = element_size;
    result.root = make_bucket(context, bucket_size, element_size, 0);
    result.current = result.root;

    return result;
}

void bt_bucketed_buffer_destroy(bt_Context* context, bt_BucketedBuffer* buffer)
{
    bt_Bucket* bucket = buffer->root;
    bt_Bucket* last_bucket = NULL;

    do {
        if (last_bucket) context->free(last_bucket);

        context->free(bucket->data);
        last_bucket = bucket;
    } while (bucket = bucket->next);

    context->free(bucket);
}

void* bt_bucketed_buffer_at(bt_BucketedBuffer* buffer, uint32_t index)
{
    // fast path as current bucket is often in cache
    if (index > buffer->current->base_index && index < buffer->current->base_index + buffer->current->capacity)
    {
        return (void*)((char*)buffer->current->data + ((index - buffer->current->base_index) * (size_t)buffer->element_size));
    }

    bt_Bucket* current = buffer->root;
    while (current) {
        if (index > current->base_index && index < current->base_index + current->capacity)
        {
            return (void*)((char*)current->data + ((index - current->base_index) * (size_t)buffer->element_size));
        }

        current = current->next;
    }

    return NULL;
}

uint32_t bt_bucketed_buffer_insert(bt_Context* context, bt_BucketedBuffer* buffer, void* element)
{
    if (buffer->current->length < buffer->current->capacity) {
        memcpy((char*)buffer->current->data + buffer->current->element_size * buffer->current->length, element, buffer->element_size);
        return buffer->current->base_index + buffer->current->length++;
    }

    bt_Bucket* bucket = buffer->root;
    while (bucket) {
        if (bucket->length == bucket->capacity)
            bucket = bucket->next;
    }

    if (bucket) {
        buffer->current = bucket;
        return bt_bucketed_buffer_insert(context, buffer, element);
    }

    buffer->current->next = make_bucket(context, buffer->bucket_size, buffer->element_size, buffer->current->base_index + buffer->bucket_size);
    buffer->current = buffer->current->next;

    return bt_bucketed_buffer_insert(context, buffer, element);
}

void bt_bucketed_buffer_remove(bt_Context* context, bt_BucketedBuffer* buffer, uint32_t index)
{
    // fast path as current bucket is often in cache
    if (index > buffer->current->base_index && index < buffer->current->base_index + buffer->current->capacity)
    {
        uint32_t local_idx = index - buffer->current->base_index;
        
        memcpy((char*)buffer->current->data + buffer->current->element_size * local_idx,
            (char*)buffer->current->data + buffer->current->element_size * (buffer->current->length - 1), 
            buffer->element_size);
        
        buffer->current->length--;
        return;
    }

    bt_Bucket* current = buffer->root;
    while (current) {
        if (index > current->base_index && index < current->base_index + current->capacity)
        {
            uint32_t local_idx = index - current->base_index;
            
            memcpy((char*)current->data + current->element_size * local_idx,
                (char*)current->data + current->element_size * (current->length - 1), 
                buffer->element_size);
            
            current->length--;
        }

        current = current->next;
    }
}

bt_bool bt_buffer_pop(bt_Buffer* buffer, void* output)
{
    if (buffer->length > 0) {
        memcpy(output, (char*)buffer->data + ((--buffer->length) * (size_t)buffer->element_size), buffer->element_size);
        return BT_TRUE;
    }

    return BT_FALSE;
}
