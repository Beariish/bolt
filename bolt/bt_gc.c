#include "bt_gc.h"

#ifdef BT_DEBUG
#include <assert.h>
#endif

#include <string.h>

#include "bt_type.h"
#include "bt_context.h"
#include "bt_compiler.h"
#include "bt_userdata.h"

void* bt_gc_alloc(bt_Context* ctx, size_t size)
{
	ctx->gc.bytes_allocated += size;
	void* ptr = ctx->alloc(size);
	return ptr;
}

void* bt_gc_realloc(bt_Context* ctx, void* ptr, size_t old_size, size_t new_size)
{
	if (old_size > ctx->gc.bytes_allocated) {
		bt_runtime_error(ctx->current_thread, "Attempted to realloc more bytes than GC is tracking!", 0);
		return NULL;
	}

	ctx->gc.bytes_allocated -= old_size;
	void* new_ptr = ctx->realloc(ptr, new_size);
	ctx->gc.bytes_allocated += new_size;

	return new_ptr;
}

void bt_gc_free(bt_Context* ctx, void* ptr, size_t size)
{
	if (size > ctx->gc.bytes_allocated) {
		bt_runtime_error(ctx->current_thread, "Attempted to free more bytes than GC is tracking!", 0);
		return;
	}

	ctx->gc.bytes_allocated -= size;
	ctx->free(ptr);
}

bt_Object* bt_allocate(bt_Context* context, uint32_t full_size, bt_ObjectType type)
{
	if (context->gc.bytes_allocated >= context->gc.next_cycle) {
		bt_collect(&context->gc, 0);
	}
	
	bt_Object* obj = bt_gc_alloc(context, full_size);
	memset(obj, 0, full_size);

	BT_OBJECT_SET_TYPE(obj, type);
	if (context->next) BT_OBJECT_SET_NEXT(context->next, obj);
	context->next = obj;
	
	return obj;
}

/** Free all owned allocations in obj */
static void free_subobjects(bt_Context* context, bt_Object* obj)
{
	switch (BT_OBJECT_GET_TYPE(obj)) {
	case BT_OBJECT_TYPE_TYPE: {
		bt_Type* type = (bt_Type*)obj;
		if (type->name) {
			switch (type->category) {
			case BT_TYPE_CATEGORY_SIGNATURE:
				bt_buffer_destroy(context, &type->as.fn.args);
				break;
			case BT_TYPE_CATEGORY_UNION:
				bt_buffer_destroy(context, &type->as.selector.types);
				break;
			case BT_TYPE_CATEGORY_USERDATA:
				bt_buffer_destroy(context, &type->as.userdata.fields);
				break;
			}
			bt_gc_free(context, type->name, 0); // Length is not tracked
		}
	} break;
	case BT_OBJECT_TYPE_MODULE: {
		bt_Module* mod = (bt_Module*)obj;
		bt_buffer_destroy(context, &mod->constants);
		bt_buffer_destroy(context, &mod->instructions);
		bt_buffer_destroy(context, &mod->imports);

		if (mod->debug_locs) {
			bt_buffer_destroy(context, mod->debug_locs);

			for (uint32_t i = 0; i < mod->debug_tokens.length; i++) {
				bt_gc_free(context, mod->debug_tokens.elements[i], sizeof(bt_Token));
			}

			bt_buffer_destroy(context, &mod->debug_tokens);
			bt_gc_free(context, mod->debug_locs, sizeof(bt_DebugLocBuffer));
			context->free(mod->debug_source); // FIXME: Size of this is not tracked
		}
	} break;
	case BT_OBJECT_TYPE_FN: {
		bt_Fn* fn = (bt_Fn*)obj;
		bt_buffer_destroy(context, &fn->constants);
		bt_buffer_destroy(context, &fn->instructions);
		if (fn->debug) {
			bt_buffer_destroy(context, fn->debug);
			bt_gc_free(context, fn->debug, sizeof(bt_DebugLocBuffer));
		}
	} break;
	case BT_OBJECT_TYPE_TABLE: {
		bt_Table* tbl = (bt_Table*)obj;
		if (!tbl->is_inline && tbl->capacity > 0) {
			bt_gc_free(context, tbl->outline, tbl->capacity * sizeof(bt_TablePair));
		}
	} break;
	case BT_OBJECT_TYPE_STRING: {
		bt_String* str = (bt_String*)obj;
		if (str->len <= BT_STRINGTABLE_MAX_LEN) {
			bt_remove_interned(context, str);
		}
	} break;
	case BT_OBJECT_TYPE_ARRAY: {
		bt_Array* arr = (bt_Array*)obj;
		bt_gc_free(context, arr->items, arr->capacity * sizeof(bt_Value));
	} break;
	case BT_OBJECT_TYPE_USERDATA: {
		bt_Userdata* userdata = (bt_Userdata*)obj;
		if (userdata->finalizer) {
			userdata->finalizer(context, userdata);
		}
	} break;
	}
}

/** Specifically returns the inline allocation size of the object, not that of owned allocations */
static size_t get_object_size(bt_Object* obj)
{
	switch (BT_OBJECT_GET_TYPE(obj)) {
	case BT_OBJECT_TYPE_NONE: return sizeof(bt_Object);
	case BT_OBJECT_TYPE_TYPE: return sizeof(bt_Type);
	case BT_OBJECT_TYPE_STRING: return sizeof(bt_String) + ((bt_String*)obj)->len;
	case BT_OBJECT_TYPE_MODULE: return sizeof(bt_Module);
	case BT_OBJECT_TYPE_IMPORT: return sizeof(bt_ModuleImport);
	case BT_OBJECT_TYPE_FN: return sizeof(bt_Fn);
	case BT_OBJECT_TYPE_NATIVE_FN: return sizeof(bt_NativeFn);
	case BT_OBJECT_TYPE_CLOSURE: return sizeof(bt_Closure) + ((bt_Closure*)obj)->num_upv * sizeof(bt_Value);
	case BT_OBJECT_TYPE_ARRAY: return sizeof(bt_Array);
	case BT_OBJECT_TYPE_TABLE: return sizeof(bt_Table) + sizeof(bt_TablePair) * ((bt_Table*)obj)->inline_capacity - sizeof(bt_Value);
	case BT_OBJECT_TYPE_USERDATA: return sizeof(bt_Userdata) + ((bt_Userdata*)obj)->size;
	case BT_OBJECT_TYPE_ANNOTATION: return sizeof(bt_Annotation);
	default:
#ifdef BT_DEBUG
		assert(0 && "Attempted to get size of unrecognized type!");
#endif
		return 0;
	}

}

void bt_free(bt_Context* context, bt_Object* obj)
{
	free_subobjects(context, obj);
	bt_gc_free(context, obj, get_object_size(obj));
}

void bt_make_gc(bt_Context* ctx)
{
	bt_GC result = { 0 };
	result.ctx = ctx;
	bt_buffer_empty(&result.active_parsers);
	ctx->gc = result;

	bt_gc_set_grey_cap(ctx, 256);
	bt_gc_set_next_cycle(ctx, 1024 * 1024 * 32); // 32mb
	bt_gc_set_min_size(ctx, ctx->gc.next_cycle);
	bt_gc_set_growth_pct(ctx, 150);
	bt_gc_set_pause_growth_pct(ctx, 115);
}

void bt_destroy_gc(bt_Context* ctx, bt_GC* gc)
{
	bt_gc_free(ctx, gc->greys, gc->grey_cap * sizeof(bt_Object*));
}

size_t bt_gc_get_next_cycle(bt_Context* ctx)
{
	return ctx->gc.next_cycle;
}

void bt_gc_set_next_cycle(bt_Context* ctx, size_t next_cycle)
{
	ctx->gc.next_cycle = next_cycle;
}

size_t bt_gc_get_min_size(bt_Context* ctx)
{
	return ctx->gc.min_size;
}

void bt_gc_set_min_size(bt_Context* ctx, size_t min_size)
{
	ctx->gc.min_size = min_size;
}

uint32_t bt_gc_get_grey_cap(bt_Context* ctx)
{
	return ctx->gc.grey_cap;
}

void bt_gc_set_grey_cap(bt_Context* ctx, uint32_t grey_cap)
{
	uint32_t old_cap = ctx->gc.grey_cap;
	ctx->gc.grey_cap = grey_cap;
	ctx->gc.greys = (bt_Object**)bt_gc_realloc(ctx, ctx->gc.greys, old_cap * sizeof(bt_Object*), ctx->gc.grey_cap * sizeof(bt_Object*));
}

size_t bt_gc_get_growth_pct(bt_Context* ctx)
{
	return ctx->gc.cycle_growth_pct;
}

void bt_gc_set_growth_pct(bt_Context* ctx, size_t growth_pct)
{
	ctx->gc.cycle_growth_pct = (uint32_t)growth_pct;
}

size_t bt_gc_get_pause_growth_pct(bt_Context* ctx)
{
	return ctx->gc.pause_growth_pct;
}

void bt_gc_set_pause_growth_pct(bt_Context* ctx, size_t growth_pct)
{
	ctx->gc.pause_growth_pct = (uint32_t)growth_pct;
}

static void grey(bt_GC* gc, bt_Object* obj) {
	if (!obj || BT_OBJECT_GET_MARK(obj)) return;

	BT_OBJECT_MARK(obj);

	if (gc->grey_count == gc->grey_cap) {
		bt_gc_set_grey_cap(gc->ctx, gc->grey_cap * 2);
	}

	gc->greys[gc->grey_count++] = obj;
}

void bt_grey_obj(bt_Context* ctx, bt_Object* obj)
{
	grey(&ctx->gc, obj);
}

static void blacken(bt_GC* gc, bt_Object* obj)
{
	switch (BT_OBJECT_GET_TYPE(obj)) {
	case BT_OBJECT_TYPE_NONE: break; // Reserved for root object
	case BT_OBJECT_TYPE_TYPE: {
		bt_Type* as_type = (bt_Type*)obj;

		grey(gc, (bt_Object*)as_type->prototype);
		grey(gc, (bt_Object*)as_type->prototype_types);
		grey(gc, (bt_Object*)as_type->prototype_values);
		grey(gc, (bt_Object*)as_type->annotations);

		switch (as_type->category) {
		case BT_TYPE_CATEGORY_ARRAY:
			grey(gc, (bt_Object*)as_type->as.array.inner);
			break;
		case BT_TYPE_CATEGORY_NATIVE_FN:
		case BT_TYPE_CATEGORY_SIGNATURE: {
				grey(gc, (bt_Object*)as_type->as.fn.return_type);
				grey(gc, (bt_Object*)as_type->as.fn.varargs_type);
				for (uint32_t i = 0; i < as_type->as.fn.args.length; ++i) {
					bt_Type* arg = as_type->as.fn.args.elements[i];
					grey(gc, (bt_Object*)arg);
				}
		} break;
		case BT_TYPE_CATEGORY_TABLESHAPE: {
				grey(gc, (bt_Object*)as_type->as.table_shape.tmpl);
				grey(gc, (bt_Object*)as_type->as.table_shape.layout);
				grey(gc, (bt_Object*)as_type->as.table_shape.key_layout);
				grey(gc, (bt_Object*)as_type->as.table_shape.parent);
				grey(gc, (bt_Object*)as_type->as.table_shape.key_type);
				grey(gc, (bt_Object*)as_type->as.table_shape.value_type);
		} break;
		case BT_TYPE_CATEGORY_TYPE: {
				grey(gc, (bt_Object*)as_type->as.type.boxed);
		} break;
		case BT_TYPE_CATEGORY_USERDATA: {
				bt_FieldBuffer* fields = &as_type->as.userdata.fields;
				for (uint32_t i = 0; i < fields->length; i++) {
					bt_UserdataField* field = fields->elements + i;
					grey(gc, (bt_Object*)field->bolt_type);
					grey(gc, (bt_Object*)field->name);
				}
		} break;
		case BT_TYPE_CATEGORY_UNION: {
				bt_TypeBuffer* entries = &as_type->as.selector.types;
				for (uint32_t i = 0; i < entries->length; ++i) {
					bt_Type* type = entries->elements[i];
					grey(gc, (bt_Object*)type);
				}
		} break;
		case BT_TYPE_CATEGORY_ENUM: {
				grey(gc, (bt_Object*)as_type->as.enum_.name);
				grey(gc, (bt_Object*)as_type->as.enum_.options);
		} break;
		}	
	} break;
	case BT_OBJECT_TYPE_MODULE: {
		bt_Module* mod = (bt_Module*)obj;
		grey(gc, (bt_Object*)mod->type);
		grey(gc, (bt_Object*)mod->exports);
		grey(gc, (bt_Object*)mod->name);
		grey(gc, (bt_Object*)mod->path);
		grey(gc, (bt_Object*)mod->storage);

		for (uint32_t i = 0; i < mod->imports.length; ++i) {
			bt_Object* import = (bt_Object*)mod->imports.elements[i];
			grey(gc, import);
		}

		for (uint32_t i = 0; i < mod->constants.length; ++i) {
			bt_Value constant = mod->constants.elements[i];
			if (BT_IS_OBJECT(constant)) {
				grey(gc, BT_AS_OBJECT(constant));
			}
		}
	} break;
	case BT_OBJECT_TYPE_IMPORT: {
		bt_ModuleImport* import = (bt_ModuleImport*)obj;
		grey(gc, (bt_Object*)import->type);
		grey(gc, (bt_Object*)import->name);
		if(BT_IS_OBJECT(import->value)) grey(gc, BT_AS_OBJECT(import->value));
	} break;
	case BT_OBJECT_TYPE_FN: {
		bt_Fn* fn = (bt_Fn*)obj;
		grey(gc, (bt_Object*)fn->module);
		grey(gc, (bt_Object*)fn->signature);
		for (uint32_t i = 0; i < fn->constants.length; ++i) {
			bt_Value constant = fn->constants.elements[i];
			if (BT_IS_OBJECT(constant)) {
				grey(gc, BT_AS_OBJECT(constant));
			}
		};
	} break;
	case BT_OBJECT_TYPE_CLOSURE: {
		bt_Closure* cl = (bt_Closure*)obj;
		grey(gc, (bt_Object*)cl->fn);
		for (uint32_t i = 0; i < cl->num_upv; ++i) {
			bt_Value upval = BT_CLOSURE_UPVALS(cl)[i];
			if (BT_IS_OBJECT(upval)) grey(gc, BT_AS_OBJECT(upval));
		};
	} break;
	case BT_OBJECT_TYPE_NATIVE_FN: {
		bt_NativeFn* ntfn = (bt_NativeFn*)obj;
		grey(gc, (bt_Object*)ntfn->type);
	} break;
	case BT_OBJECT_TYPE_TABLE: {
		bt_Table* tbl = (bt_Table*)obj;
		grey(gc, (bt_Object*)tbl->prototype);
		for (uint32_t i = 0; i < tbl->length; i++) {
			bt_TablePair* pair = BT_TABLE_PAIRS(tbl) + i;
			if (BT_IS_OBJECT(pair->key))   grey(gc, BT_AS_OBJECT(pair->key));
			if (BT_IS_OBJECT(pair->value)) grey(gc, BT_AS_OBJECT(pair->value));
		}
	} break;
	case BT_OBJECT_TYPE_USERDATA: {
		bt_Userdata* userdata = (bt_Userdata*)obj;
		grey(gc, (bt_Object*)userdata->type);
	} break;
	case BT_OBJECT_TYPE_ARRAY: {
		bt_Array* arr = (bt_Array*)obj;
		for (uint32_t i = 0; i < arr->length; i++) {
			if (BT_IS_OBJECT(arr->items[i])) grey(gc, BT_AS_OBJECT(arr->items[i]));
		}
	} break;
	case BT_OBJECT_TYPE_ANNOTATION: {
		bt_Annotation* anno = (bt_Annotation*)obj;
		grey(gc, (bt_Object*)anno->name);
		grey(gc, (bt_Object*)anno->args);
		grey(gc, (bt_Object*)anno->next);
	} break;
	}
}

static void calc_next_cycle(bt_GC* gc, size_t growth_factor)
{
	gc->next_cycle = (gc->bytes_allocated * growth_factor) / 100;
	if (gc->next_cycle < gc->min_size) gc->next_cycle = gc->min_size;
}

uint32_t bt_collect(bt_GC* gc, uint32_t max_collect)
{
	if (gc->pause_count > 0) {
		calc_next_cycle(gc, gc->pause_growth_pct);
		return 0;
	}
	
	bt_Context* ctx = gc->ctx;

	grey(gc, (bt_Object*)ctx->types.any);
	grey(gc, (bt_Object*)ctx->types.null);
	grey(gc, (bt_Object*)ctx->types.number);
	grey(gc, (bt_Object*)ctx->types.boolean);
	grey(gc, (bt_Object*)ctx->types.string);
	grey(gc, (bt_Object*)ctx->types.array);
	grey(gc, (bt_Object*)ctx->types.table);
	grey(gc, (bt_Object*)ctx->types.type);
	grey(gc, (bt_Object*)ctx->types.module);
	
	grey(gc, (bt_Object*)ctx->meta_names.add);
	grey(gc, (bt_Object*)ctx->meta_names.sub);
	grey(gc, (bt_Object*)ctx->meta_names.mul);
	grey(gc, (bt_Object*)ctx->meta_names.div);
	grey(gc, (bt_Object*)ctx->meta_names.lt);
	grey(gc, (bt_Object*)ctx->meta_names.lte);
	grey(gc, (bt_Object*)ctx->meta_names.eq);
	grey(gc, (bt_Object*)ctx->meta_names.neq);
	grey(gc, (bt_Object*)ctx->meta_names.format);
	
	grey(gc, (bt_Object*)ctx->root);
	grey(gc, (bt_Object*)ctx->type_registry);
	grey(gc, (bt_Object*)ctx->prelude);
	grey(gc, (bt_Object*)ctx->loaded_modules);
	grey(gc, (bt_Object*)ctx->native_references);

	for (uint32_t i = 0; i < gc->ctx->troot_top; ++i) {
		grey(gc, (bt_Object*)gc->ctx->troots[i]);
	}

	for (uint32_t i = 0; i < gc->active_parsers.length; ++i) {
		bt_ObjectBuffer* objects = &gc->active_parsers.elements[i]->owned_objects;
		for (uint32_t j = 0; j < objects->length; ++j) {
			grey(gc, objects->elements[j]);
		}
	}
	
	if (gc->ctx->current_thread) {
		bt_Thread* thr = gc->ctx->current_thread;
		
		uint32_t user_bottom = thr->top + BT_STACKFRAME_GET_SIZE(thr->callstack[thr->depth - 1]);
		uint32_t user_top = user_bottom + BT_STACKFRAME_GET_USER_TOP(thr->callstack[thr->depth - 1]);

		bt_Callable* current = BT_STACKFRAME_GET_CALLABLE(thr->callstack[thr->depth - 1]);
		uint32_t top = thr->top + bt_get_top_at(current, thr->ip);
		
		for (uint32_t i = 0; i < thr->depth; ++i) {
			bt_StackFrame stck = thr->callstack[i];
			grey(gc, (bt_Object*)BT_STACKFRAME_GET_CALLABLE(stck));
		}

		for (uint32_t i = 0; i < top; ++i) {
			bt_Value val = thr->stack[i];
			if (BT_IS_OBJECT(val)) grey(gc, BT_AS_OBJECT(val));
		}

		for (uint32_t i = user_bottom; i < user_top; ++i) {
			bt_Value val = thr->stack[i];
			if (BT_IS_OBJECT(val)) grey(gc, BT_AS_OBJECT(val));
		}

		grey(gc, (bt_Object*)gc->ctx->current_thread->last_error);
	}

	while (gc->grey_count) {
		bt_Object* obj = gc->greys[--gc->grey_count];
		blacken(gc, obj);
	}

	// Clear interned strings that are no longer referenced from the string table
	for (uint32_t i = 0; i < BT_STRINGTABLE_SIZE; i++) {
		bt_StringTableBucket* bucket = &ctx->string_table[i];
		for (uint32_t idx = 0; idx < bucket->length; ++idx) {
			if (!BT_OBJECT_GET_MARK((bt_Object*)bucket->elements[idx].string)) {
				bucket->elements[idx] = bucket->elements[--bucket->length];
				idx--;				
			}
		}
	}
	
	uint32_t n_collected = 0;

	bt_Object* prev = ctx->root;
	bt_Object* current = (bt_Object*)BT_OBJECT_NEXT(prev);

	bt_Thread gc_thread = { 0 };
	gc_thread.context = ctx;
	gc_thread.depth++;

	bt_Thread* old_thr = ctx->current_thread;
	ctx->current_thread = &gc_thread;

	while (current) {
		if (BT_OBJECT_GET_MARK(current)) {
			BT_OBJECT_CLEAR(current);

			prev = current;
			current = (bt_Object*)BT_OBJECT_NEXT(current);
		}
		else {
			bt_Object* to_free = current;
				
			current = (bt_Object*)BT_OBJECT_NEXT(current);
			BT_OBJECT_SET_NEXT(prev, current);

			if (ctx->next == to_free) {
				ctx->next = prev;
			}
			
			bt_free(ctx, to_free);

			n_collected++;
			if (max_collect != 0 && n_collected >= max_collect) return n_collected;
		}
	}

	calc_next_cycle(gc, gc->cycle_growth_pct);
	ctx->current_thread = old_thr;

	return n_collected;
}

void bt_gc_pause(bt_Context* ctx)
{
	ctx->gc.pause_count += 1;
}

BOLT_API void bt_gc_unpause(bt_Context* ctx)
{
	if (ctx->gc.pause_count > 0) {
		ctx->gc.pause_count -= 1;
	} else {
		bt_runtime_error(ctx->current_thread, "GC unpause requested with zero pending pauses!", 0);
	}
}

void bt_gc_register_parser(bt_Context* ctx, struct bt_Parser* parse)
{
	bt_buffer_push(ctx, &ctx->gc.active_parsers, parse);	
}

void bt_gc_unregister_parser(bt_Context* ctx, struct bt_Parser* parse)
{
	int32_t idx = -1;
	for (int32_t i = 0; i < ctx->gc.active_parsers.length; ++i) {
		if (parse == ctx->gc.active_parsers.elements[i]) {
			idx = i;
			break;
		}
	}

	if (idx == -1) {
		#ifdef BT_DEBUG
		assert(0 && "Failed to find parser!");
		#endif
		return;
	}

	ctx->gc.active_parsers.elements[idx] = bt_buffer_last(&ctx->gc.active_parsers);
	bt_buffer_pop(&ctx->gc.active_parsers);
}
