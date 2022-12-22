#pragma once

#include "bt_prelude.h"

typedef struct {
	void* data;

	uint32_t length, capacity, element_size;
} bt_Buffer;

bt_Buffer bt_buffer_with_capacity(bt_Context* context, uint32_t element_size, uint32_t capacity);
bt_Buffer bt_buffer_new(bt_Context* context, uint32_t element_size);

void bt_buffer_destroy(bt_Context* context, bt_Buffer* buffer);

bt_Buffer bt_buffer_empty();
bt_Buffer bt_buffer_clone(bt_Context* context, bt_Buffer* buffer);
bt_Buffer bt_buffer_move(bt_Buffer* buffer);

#define BT_BUFFER_NEW(context, type) bt_buffer_new(context, sizeof(type))
#define BT_BUFFER_WITH_CAPACITY(context, type, capacity) bt_buffer_with_capacity(context, sizeof(type), capacity)

bt_bool bt_buffer_push(bt_Context* context, bt_Buffer* buffer, void* elem);
bt_bool bt_buffer_pop(bt_Buffer* buffer, void* output);

static __forceinline void* bt_buffer_at(bt_Buffer* buffer, uint32_t index)
{
	return (void*)((char*)buffer->data + (index * (size_t)buffer->element_size));
}

void* bt_buffer_last(bt_Buffer* buffer);

uint32_t bt_buffer_size(bt_Buffer* buffer);

typedef struct bt_Bucket {
	void* data;
	uint32_t base_index, length, capacity, element_size;

	struct bt_Bucket* next;
} bt_Bucket;

typedef struct {
	bt_Bucket* root;
	bt_Bucket* current;
	uint32_t bucket_size, element_size;
} bt_BucketedBuffer;

bt_BucketedBuffer bt_bucketed_buffer_new(bt_Context* context, uint32_t bucket_size, uint32_t element_size);
void bt_bucketed_buffer_destroy(bt_Context* context, bt_BucketedBuffer* buffer);

#define BT_BUCKETED_BUFFER_NEW(context, size, type) bt_bucketed_buffer_new(context, size, sizeof(type))

void* bt_bucketed_buffer_at(bt_BucketedBuffer* buffer, uint32_t index);
void* bt_bucket_at(bt_Bucket* bucket, uint32_t index);

uint32_t bt_bucketed_buffer_insert(bt_Context* context, bt_BucketedBuffer* buffer, void* element); 
bt_bool bt_bucketed_buffer_remove(bt_Context* context, bt_BucketedBuffer* buffer, uint32_t index);

bt_bool bt_bucket_remove(bt_Bucket* bucket, uint32_t index);
