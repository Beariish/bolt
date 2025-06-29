#pragma once

#if __cplusplus
extern "C" {
#endif

#include "bt_object.h"

#include <stdint.h>

typedef enum bt_GCPhase {
	BT_GC_PHASE_MARK,
	BT_GC_PHASE_SWEEP,
} bt_GCPhase;

typedef struct bt_GC {
	size_t next_cycle, bytes_allocated, min_size;
	uint32_t pause_growth_pct, cycle_growth_pct;
	uint32_t grey_cap, grey_count;
	bt_Object** greys;
	uint32_t pause_count;

	bt_Context* ctx;
} bt_GC;

void* bt_gc_alloc(bt_Context* ctx, size_t size);
void* bt_gc_realloc(bt_Context* ctx, void* ptr, size_t old_size, size_t new_size);
void  bt_gc_free(bt_Context* ctx, void* ptr, size_t size);

BOLT_API void bt_push_root(bt_Context* ctx, bt_Object* root);
BOLT_API void bt_pop_root(bt_Context* ctx);

BOLT_API uint32_t bt_add_ref(bt_Context* ctx, bt_Object* obj);
BOLT_API uint32_t bt_remove_ref(bt_Context* ctx, bt_Object* obj);

BOLT_API bt_Object* bt_allocate(bt_Context* context, uint32_t full_size, bt_ObjectType type);
BOLT_API void bt_free(bt_Context* context, bt_Object* obj);

#define BT_ALLOCATE(ctx, e_type, c_type) \
	((c_type*)bt_allocate(ctx, sizeof(c_type), (BT_OBJECT_TYPE_##e_type)))

#define BT_ALLOCATE_INLINE_STORAGE(ctx, e_type, c_type, storage) \
	((c_type*)bt_allocate(ctx, sizeof(c_type) + storage, (BT_OBJECT_TYPE_##e_type)))

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

BOLT_API size_t bt_gc_get_pause_growth_pct(bt_Context* ctx);
BOLT_API void bt_gc_set_pause_growth_pct(bt_Context* ctx, size_t growth_pct);

BOLT_API void bt_grey_obj(bt_Context* ctx, bt_Object* obj);
BOLT_API uint32_t bt_collect(bt_GC* gc, uint32_t max_collect);

BOLT_API void bt_gc_pause(bt_Context* ctx);
BOLT_API void bt_gc_unpause(bt_Context* ctx);

#if __cplusplus
}
#endif