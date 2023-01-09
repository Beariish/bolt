#pragma once

#include "bt_buffer.h"
#include "bt_object.h"

#include <stdint.h>

typedef enum bt_GCPhase {
	BT_GC_PHASE_MARK,
	BT_GC_PHASE_SWEEP,
} bt_GCPhase;

typedef struct bt_GC {
	size_t next_cycle, byets_allocated, min_size;
	uint32_t grey_cap, grey_count;
	bt_Object** greys;

	bt_Context* ctx;
} bt_GC;


void bt_grey_obj(bt_Context* ctx, bt_Object* obj);
bt_GC bt_make_gc(bt_Context* ctx);

uint32_t bt_collect(bt_GC* gc, uint32_t max_collect);