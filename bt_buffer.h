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
void bt_buffer_reserve(bt_Context* ctx, bt_Buffer* buffer, size_t cap);

#define BT_BUFFER_NEW(context, type) bt_buffer_new(context, sizeof(type))
#define BT_BUFFER_WITH_CAPACITY(context, type, capacity) bt_buffer_with_capacity(context, sizeof(type), capacity)

bt_bool bt_buffer_push(bt_Context* context, bt_Buffer* buffer, void* elem);
bt_bool bt_buffer_pop(bt_Buffer* buffer, void* output);
void bt_buffer_append(bt_Context* context, bt_Buffer* dst, bt_Buffer* src);

static BT_FORCE_INLINE void* bt_buffer_at(bt_Buffer* buffer, uint32_t index)
{
	return (void*)((char*)buffer->data + (index * (size_t)buffer->element_size));
}

void* bt_buffer_last(bt_Buffer* buffer);

uint32_t bt_buffer_size(bt_Buffer* buffer);