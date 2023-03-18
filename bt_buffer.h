#pragma once

#include "bt_prelude.h"

#define bt_Buffer(T) struct {  \
	T* elements;			   \
	uint32_t length, capacity; \
}

#define bt_buffer_unpack(b) \
	(char**)&(b)->elements, &(b)->length, &(b)->capacity, sizeof(*(b)->elements)

#define bt_buffer_empty(b) \
	{ (b)->elements = (b)->length = (b)->capacity = 0; }

#define bt_buffer_with_capacity(b, ctx, cap) \
	{ bt_buffer_reserve(ctx, bt_buffer_unpack(b), cap); }

#define bt_buffer_destroy(ctx, b) \
	{ bt_buffer_free(ctx, bt_buffer_unpack(b)); }

#define bt_buffer_clone(ctx, dst, src) \
	{ bt_buffer_clone_(ctx, bt_buffer_unpack(dst), bt_buffer_unpack(src)); }

#define bt_buffer_push(ctx, b, elem) \
	{ bt_buffer_expand(ctx, bt_buffer_unpack(b), 1); \
		(b)->elements[(b)->length++] = elem; }

#define bt_buffer_append(ctx, dst, src) \
	{ bt_buffer_append_(ctx, bt_buffer_unpack(dst), bt_buffer_unpack(src)); }

#define bt_buffer_pop(b) \
	(b)->elements[--(b)->length]

#define bt_buffer_last(b) \
	(b)->elements[(b)->length - 1]

#define bt_buffer_size(b) \
	(sizeof(*(b)->elements) * (b)->capacity)

#define bt_buffer_move(dst, src) \
	{ bt_buffer_move_(bt_buffer_unpack(dst), bt_buffer_unpack(src)); }


void bt_buffer_reserve(bt_Context* ctx, char** data, uint32_t* length, uint32_t* capacity, size_t element_size, size_t new_cap);
void bt_buffer_expand(bt_Context* ctx, char** data, uint32_t* length, uint32_t* capacity, size_t element_size, size_t by);
void bt_buffer_free(bt_Context* ctx, char** data, uint32_t* length, uint32_t* capacity, size_t element_size);

void bt_buffer_clone_(bt_Context* ctx, char** data1, uint32_t* length1, uint32_t* capacity1, size_t element_size1, 
									 char** data2, uint32_t* length2, uint32_t* capacity2, size_t element_size2);

void bt_buffer_append_(bt_Context* ctx, char** data1, uint32_t* length1, uint32_t* capacity1, size_t element_size1, 
									 char** data2, uint32_t* length2, uint32_t* capacity2, size_t element_size2);

void bt_buffer_move_(char** data1, uint32_t* length1, uint32_t* capacity1, size_t element_size1,
					 char** data2, uint32_t* length2, uint32_t* capacity2, size_t element_size2);
