#pragma once

#include "bt_buffer.h"
#include "bt_object.h"

#include <stdint.h>

typedef enum bt_GCPhase {
	BT_GC_PHASE_MARK,
	BT_GC_PHASE_SWEEP,
} bt_GCPhase;

typedef struct bt_GC {
	size_t next_cycle, byets_allocated, min_size, cycle_growth_pct;
	uint32_t grey_cap, grey_count;
	bt_Object** greys;

	bt_Context* ctx;
} bt_GC;


BOLT_API void bt_make_gc(bt_Context* ctx);
BOLT_API void bt_destroy_gc(bt_Context* ctx, bt_GC* gc);

BOLT_API size_t bt_gc_get_next_cycle(bt_Context* ctx);
BOLT_API void bt_gc_set_next_cycle(bt_Context* ctx, size_t next_cycle);

BOLT_API size_t bt_gc_get_min_size(bt_Context* ctx);
BOLT_API void bt_gc_set_min_size(bt_Context* ctx, size_t min_size);

BOLT_API uint32_t bt_gc_get_grey_cap(bt_Context* ctx);
BOLT_API void bt_gc_set_grey_cap(bt_Context* ctx, uint32_t grey_cap);

BOLT_API size_t bt_gc_get_growth_pct(bt_Context* ctx);
BOLT_API void bt_gc_set_growth_pct(bt_Context* ctx, size_t growth_pct);

BOLT_API void bt_grey_obj(bt_Context* ctx, bt_Object* obj);
BOLT_API uint32_t bt_collect(bt_GC* gc, uint32_t max_collect);