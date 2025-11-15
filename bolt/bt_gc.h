#pragma once

#if __cplusplus
extern "C" {
#endif

#include "bt_object.h"

#include <stdint.h>

/**
 * A structure that holds information about the freelists for a given type of object
 */
typedef struct bt_FreeList {
	uint32_t n_buckets;
	bt_Object** buckets;
} bt_FreeList;
	
/** Contains all internal state for the garbage collector, such as memory stats and pending greys */
typedef struct bt_GC {
	size_t next_cycle, bytes_allocated, min_size;
	uint32_t pause_growth_pct, cycle_growth_pct, min_cycle_growth;
	uint32_t grey_cap, grey_count;
	bt_Object** greys;
	uint32_t pause_count;

	int64_t freelist_size, freelist_min, freelist_cap, freelist_max, freelist_growth_pct;
	bt_FreeList free_strings;
	bt_FreeList free_tables;
	bt_FreeList free_closures;
	bt_bool enable_freelist;
	
	bt_Context* ctx;
} bt_GC;

/** Allocate a block of memory and track its size */
BOLT_API void* bt_gc_alloc(bt_Context* ctx, size_t size);
/** Realloc a block of memory and track the size delta */
BOLT_API void* bt_gc_realloc(bt_Context* ctx, void* ptr, size_t old_size, size_t new_size);
/** Free a block of memory and track its size */
BOLT_API void  bt_gc_free(bt_Context* ctx, void* ptr, size_t size);

/** Push an object into the temporary root set, preventing collection until it has been popped */
BOLT_API void bt_push_root(bt_Context* ctx, bt_Object* root);
/** Pop the topmost object from the temporary root set */
BOLT_API void bt_pop_root(bt_Context* ctx);

/** Add a permanent reference to `obj`, preventing its collection. Returns the new number of references */
BOLT_API uint32_t bt_add_ref(bt_Context* ctx, bt_Object* obj);
/** Remove a permanent reference to `obj`, re-enabling its collection once it hits 0. Returns the new number of references */
BOLT_API uint32_t bt_remove_ref(bt_Context* ctx, bt_Object* obj);

/** Allocate a garbage collected object of type `type` with a given size */
BOLT_API bt_Object* bt_allocate(bt_Context* context, uint32_t full_size, bt_ObjectType type);
/** Free a garbage collected object and all its owned allocations - unsafe to call if this object is still being referenced */
BOLT_API void bt_free(bt_Context* context, bt_Object* obj);

#define BT_ALLOCATE(ctx, e_type, c_type) \
	((c_type*)bt_allocate(ctx, sizeof(c_type), (BT_OBJECT_TYPE_##e_type)))

#define BT_ALLOCATE_INLINE_STORAGE(ctx, e_type, c_type, storage) \
	((c_type*)bt_allocate(ctx, sizeof(c_type) + storage, (BT_OBJECT_TYPE_##e_type)))

/** Sets up a default gc inside `ctx`. Any memory referenced by the old gc will be lost */
BOLT_API void bt_make_gc(bt_Context* ctx);
/** Destroys the gc by freeing the pending greys list */
BOLT_API void bt_destroy_gc(bt_Context* ctx, bt_GC* gc);

/** Get the allocation threshold at which the next cycle will be ran */
BOLT_API size_t bt_gc_get_next_cycle(bt_Context* ctx);
/** Manually override when the next cycle will happen. `next_cycle` is in bytes */
BOLT_API void bt_gc_set_next_cycle(bt_Context* ctx, size_t next_cycle);

/** Get the minimum size the gc cycle threshold can be */
BOLT_API size_t bt_gc_get_min_size(bt_Context* ctx);
/** Set a minimum size for the gc cycle threshold. Any cycle that leaves less than this will be rounded up */
BOLT_API void bt_gc_set_min_size(bt_Context* ctx, size_t min_size);

/** Get the internal size of the greys list */
BOLT_API uint32_t bt_gc_get_grey_cap(bt_Context* ctx);
/** Override the capacity of the internal greys list. Will allocate if needed */
BOLT_API void bt_gc_set_grey_cap(bt_Context* ctx, uint32_t grey_cap);

/** Get the percentage of slack required for the next cycle after one finishes, expressed as an integer */
BOLT_API size_t bt_gc_get_growth_pct(bt_Context* ctx);
/** Set the percentage of slack required for the next cycle after one finishes, expressed as an integer */
BOLT_API void bt_gc_set_growth_pct(bt_Context* ctx, size_t growth_pct);
	
/** Get the minimum percentage of slack required for the next cycle if churn is at maximum, expressed as an integer */
BOLT_API size_t bt_gc_get_min_growth_pct(bt_Context* ctx);
/** Get the minimum percentage of slack required for the next cycle if churn is at maximum, expressed as an integer */
BOLT_API void bt_gc_set_min_growth_pct(bt_Context* ctx, size_t growth_pct);
	
/** Get the percentage of slack given if the gc hits the threshold during a pause, expressed as an integer */
BOLT_API size_t bt_gc_get_pause_growth_pct(bt_Context* ctx);
/** Set the percentage of slack given if the gc hits the threshold during a pause, expressed as an integer */
BOLT_API void bt_gc_set_pause_growth_pct(bt_Context* ctx, size_t growth_pct);

/** Add an object to the grey set, meaning it'll be traversed during the next cycle */
BOLT_API void bt_grey_obj(bt_Context* ctx, bt_Object* obj);
/** Perform a gc cycle, stopping after `max_collect` objects. Returns the number of objects collected */
BOLT_API uint32_t bt_collect(bt_GC* gc, uint32_t max_collect);

/** Pauses the gc, stopping it from cycling even if over budget. Uses a counter internally to allow safe nesting */
BOLT_API void bt_gc_pause(bt_Context* ctx);
/** Unpauses the gc, allowing it to cycle. Uses a counter internally to allow safe nesting */
BOLT_API void bt_gc_unpause(bt_Context* ctx);
	
/** Get whether the gc freelist is enabled */
BOLT_API bt_bool bt_gc_get_freelist_enabled(bt_Context* ctx);
/** Set whether the gc freelist is enabled */
BOLT_API void bt_gc_set_freelist_enabled(bt_Context* ctx, bt_bool enabled);
	
/** Get the current capacity for the freelist */
BOLT_API size_t bt_gc_get_freelist_cap(bt_Context* ctx);
/** Set the current capacity for the freelist */
BOLT_API void bt_gc_set_freelist_cap(bt_Context* ctx, size_t freelist_cap);

/** Get the current minimum capacity for the freelist */
BOLT_API size_t bt_gc_get_freelist_min(bt_Context* ctx);
/** Set the current minimum capacity for the freelist */
BOLT_API void bt_gc_set_freelist_min(bt_Context* ctx, size_t freelist_min);
	
/** Get the current max capacity for the freelist */
BOLT_API size_t bt_gc_get_freelist_max(bt_Context* ctx);
/** Set the max capacity for the freelist */
BOLT_API void bt_gc_set_freelist_max(bt_Context* ctx, size_t freelist_max);

/** Get the freelist growth pct */
BOLT_API size_t bt_gc_get_freelist_growth_pct(bt_Context* ctx);
/** Set the freelist growth pct */
BOLT_API void bt_gc_set_freelist_growth_pct(bt_Context* ctx, size_t growth_pct);

#if __cplusplus
}
#endif