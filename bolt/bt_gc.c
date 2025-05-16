#include "bt_gc.h"
#include "bt_type.h"
#include "bt_context.h"
#include "bt_compiler.h"
#include "bt_userdata.h"

void bt_make_gc(bt_Context* ctx)
{
	bt_GC result = { 0 };
	result.ctx = ctx;
	ctx->gc = result;

	bt_gc_unpause(ctx);
	bt_gc_set_grey_cap(ctx, 32);
	bt_gc_set_next_cycle(ctx, 1024 * 1024 * 10); // 10mb
	bt_gc_set_min_size(ctx, ctx->gc.next_cycle);
	bt_gc_set_growth_pct(ctx, 175);
}

void bt_destroy_gc(bt_Context* ctx, bt_GC* gc)
{
	ctx->free(gc->greys);
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
	ctx->gc.grey_cap = grey_cap;
	ctx->gc.greys = ctx->realloc(ctx->gc.greys, ctx->gc.grey_cap * sizeof(bt_Object*));
}

size_t bt_gc_get_growth_pct(bt_Context* ctx)
{
	return ctx->gc.cycle_growth_pct;
}

void bt_gc_set_growth_pct(bt_Context* ctx, size_t growth_pct)
{
	ctx->gc.cycle_growth_pct = growth_pct;
}

static void grey(bt_GC* gc, bt_Object* obj) {
	bt_ObjectType type = obj ? BT_OBJECT_GET_TYPE(obj) :  0;
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

				bt_MethodBuffer* methods = &as_type->as.userdata.functions;
				for (uint32_t i = 0; i < methods->length; i++) {
					bt_UserdataMethod* method = methods->elements + i;
					grey(gc, (bt_Object*)method->name);
					grey(gc, (bt_Object*)method->fn);
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
	}
}

uint32_t bt_collect(bt_GC* gc, uint32_t max_collect)
{
	bt_Context* ctx = gc->ctx;

	grey(gc, (bt_Object*)ctx->types.any);
	grey(gc, (bt_Object*)ctx->types.null);
	grey(gc, (bt_Object*)ctx->types.number);
	grey(gc, (bt_Object*)ctx->types.boolean);
	grey(gc, (bt_Object*)ctx->types.string);
	grey(gc, (bt_Object*)ctx->types.array);
	grey(gc, (bt_Object*)ctx->types.table);
	//grey(gc, (bt_Object*)ctx->types.fn);
	grey(gc, (bt_Object*)ctx->types.type);
	
	grey(gc, (bt_Object*)ctx->meta_names.add);
	grey(gc, (bt_Object*)ctx->meta_names.sub);
	grey(gc, (bt_Object*)ctx->meta_names.mul);
	grey(gc, (bt_Object*)ctx->meta_names.div);
	grey(gc, (bt_Object*)ctx->meta_names.lt);
	grey(gc, (bt_Object*)ctx->meta_names.lte);
	grey(gc, (bt_Object*)ctx->meta_names.eq);
	grey(gc, (bt_Object*)ctx->meta_names.neq);
	grey(gc, (bt_Object*)ctx->meta_names.format);
	grey(gc, (bt_Object*)ctx->meta_names.collect);
	
	grey(gc, (bt_Object*)ctx->root);
	grey(gc, (bt_Object*)ctx->type_registry);
	grey(gc, (bt_Object*)ctx->prelude);
	grey(gc, (bt_Object*)ctx->loaded_modules);
	grey(gc, (bt_Object*)ctx->native_references);

	for (uint32_t i = 0; i < gc->ctx->troot_top; ++i) {
		grey(gc, (bt_Object*)gc->ctx->troots[i]);
	}
	
	if (gc->ctx->current_thread) {
		bt_Thread* thr = gc->ctx->current_thread;
		uint32_t top = thr->top + BT_STACKFRAME_GET_SIZE(thr->callstack[thr->depth - 1]) 
			+ BT_STACKFRAME_GET_USER_TOP(thr->callstack[thr->depth - 1]);

		for (uint32_t i = 0; i < thr->depth; ++i) {
			bt_StackFrame stck = thr->callstack[i];
			grey(gc, (bt_Object*)BT_STACKFRAME_GET_CALLABLE(stck));
		}

		for (uint32_t i = 0; i < top; ++i) {
			bt_Value val = thr->stack[i];
			if (BT_IS_OBJECT(val)) grey(gc, BT_AS_OBJECT(val));
		}

		grey(gc, (bt_Object*)gc->ctx->current_thread->last_error);
	}

	while (gc->grey_count) {
		bt_Object* obj = gc->greys[--gc->grey_count];
		blacken(gc, obj);
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
			bt_free(ctx, to_free);

			n_collected++;
			if (max_collect != 0 && n_collected >= max_collect) return n_collected;
		}
	}

	gc->next_cycle = (gc->byets_allocated * gc->cycle_growth_pct) / 100;
	if (gc->next_cycle < gc->min_size) gc->next_cycle = gc->min_size;

	ctx->current_thread = old_thr;

	return n_collected;
}

void bt_gc_pause(bt_Context* ctx)
{
	ctx->gc.is_paused = BT_TRUE;
}

BOLT_API void bt_gc_unpause(bt_Context* ctx)
{
	ctx->gc.is_paused = BT_FALSE;
}
