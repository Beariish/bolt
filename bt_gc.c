#include "bt_gc.h"
#include "bt_type.h"
#include "bt_context.h"
#include "bt_compiler.h"
#include "bt_userdata.h"

#include <stdio.h>
#include <assert.h>

bt_GC bt_make_gc(bt_Context* ctx)
{
	bt_GC result;
	result.ctx = ctx;
	result.grey_cap = 32;
	result.grey_count = 0;
	result.greys = ctx->alloc(sizeof(bt_Object*) * result.grey_cap);
	result.next_cycle = 1024 * 1024 * 10;
	result.min_size = result.next_cycle;
	result.byets_allocated = 0;

	return result;
}

void bt_destroy_gc(bt_Context* ctx, bt_GC* gc)
{
	ctx->free(gc->greys);
}

static void grey(bt_GC* gc, bt_Object* obj) {
	bt_ObjectType type = obj ? BT_OBJECT_GET_TYPE(obj) :  0;
	if (!obj || BT_OBJECT_GET_MARK(obj)) return;

	BT_OBJECT_MARK(obj);

	if (gc->grey_count == gc->grey_cap) {
		gc->grey_cap *= 2;
		gc->greys = gc->ctx->realloc(gc->greys, gc->grey_cap * sizeof(bt_Object*));
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
		bt_Type* as_type = obj;
		if (as_type->is_optional) as_type = as_type->as.nullable.base;

		grey(gc, as_type->prototype_types);
		grey(gc, as_type->prototype_values);

		switch (as_type->category) {
		case BT_TYPE_CATEGORY_ARRAY:
			grey(gc, as_type->as.array.inner);
			break;
		case BT_TYPE_CATEGORY_NATIVE_FN:
		case BT_TYPE_CATEGORY_SIGNATURE: {
			grey(gc, as_type->as.fn.return_type);
			grey(gc, as_type->as.fn.varargs_type);
			for (uint32_t i = 0; i < as_type->as.fn.args.length; ++i) {
				bt_Type* arg = as_type->as.fn.args.elements[i];
				grey(gc, arg);
			}
		} break;
		case BT_TYPE_CATEGORY_TABLESHAPE: {
			grey(gc, as_type->as.table_shape.layout);
			grey(gc, as_type->as.table_shape.parent);
		} break;
		case BT_TYPE_CATEGORY_TYPE: {
			grey(gc, as_type->as.type.boxed);
		} break;
		case BT_TYPE_CATEGORY_USERDATA: {
			bt_FieldBuffer* fields = &as_type->as.userdata.fields;
			for (uint32_t i = 0; i < fields->length; i++) {
				bt_UserdataField* field = fields->elements + i;
				grey(gc, field->bolt_type);
				grey(gc, field->name);
			}

			bt_MethodBuffer* methods = &as_type->as.userdata.functions;
			for (uint32_t i = 0; i < methods->length; i++) {
				bt_UserdataMethod* method = methods->elements + i;
				grey(gc, method->name);
				grey(gc, method->fn);
			}
		} break;
		case BT_TYPE_CATEGORY_UNION: {
			bt_TypeBuffer* entries = &as_type->as.selector.types;
			for (uint32_t i = 0; i < entries->length; ++i) {
				bt_Type* type = entries->elements[i];
				grey(gc, type);
			}
		} break;
		}
	} break;
	case BT_OBJECT_TYPE_MODULE: {
		bt_Module* mod = obj;
		grey(gc, mod->type);
		grey(gc, mod->exports);

		for (uint32_t i = 0; i < mod->imports.length; ++i) {
			bt_Object* import = mod->imports.elements[i];
			grey(gc, import);
		}

		for (uint32_t i = 0; i < mod->constants.length; ++i) {
			bt_Constant* constant = mod->constants.elements + i;
			if (BT_IS_OBJECT(constant->value)) {
				grey(gc, BT_AS_OBJECT(constant->value));
			}
		}
	} break;
	case BT_OBJECT_TYPE_IMPORT: {
		bt_ModuleImport* import = obj;
		grey(gc, import->type);
		grey(gc, import->name);
		if(BT_IS_OBJECT(import->value)) grey(gc, BT_AS_OBJECT(import->value));
	} break;
	case BT_OBJECT_TYPE_FN: {
		bt_Fn* fn = obj;
		grey(gc, fn->module);
		grey(gc, fn->signature);
		for (uint32_t i = 0; i < fn->constants.length; ++i) {
			bt_Constant* constant = fn->constants.elements + i;
			if (BT_IS_OBJECT(constant->value)) {
				grey(gc, BT_AS_OBJECT(constant->value));
			}
		};
	} break;
	case BT_OBJECT_TYPE_CLOSURE: {
		bt_Closure* cl = obj;
		grey(gc, cl->fn);
		for (uint32_t i = 0; i < cl->upvals.length; ++i) {
			bt_Value upval = cl->upvals.elements[i];
			if (BT_IS_OBJECT(upval)) grey(gc, BT_AS_OBJECT(upval));
		};
	} break;
	case BT_OBJECT_TYPE_NATIVE_FN: {
		bt_NativeFn* ntfn = obj;
		grey(gc, ntfn->type);
	} break;
	case BT_OBJECT_TYPE_TABLE: {
		bt_Table* tbl = obj;
		grey(gc, tbl->prototype);
		for (uint32_t i = 0; i < tbl->pairs.length; i++) {
			bt_TablePair* pair = tbl->pairs.elements + i;
			if (BT_IS_OBJECT(pair->key))   grey(gc, BT_AS_OBJECT(pair->key));
			if (BT_IS_OBJECT(pair->value)) grey(gc, BT_AS_OBJECT(pair->value));
		}
	} break;
	case BT_OBJECT_TYPE_USERDATA: {
		bt_Userdata* userdata = obj;
		grey(gc, userdata->type);
	} break;
	}
}

uint32_t bt_collect(bt_GC* gc, uint32_t max_collect)
{
	bt_Context* ctx = gc->ctx;

	grey(gc, ctx->types.any);
	grey(gc, ctx->types.array);
	grey(gc, ctx->types.boolean);
	grey(gc, ctx->types.null);
	grey(gc, ctx->types.number);
	grey(gc, ctx->types.string);
	grey(gc, ctx->types.table);
	grey(gc, ctx->types.type);

	grey(gc, ctx->meta_names.add);
	grey(gc, ctx->meta_names.div);
	grey(gc, ctx->meta_names.mul);
	grey(gc, ctx->meta_names.sub);
	grey(gc, ctx->meta_names.format);
	grey(gc, ctx->meta_names.collect);

	grey(gc, ctx->root);
	grey(gc, ctx->type_registry);
	grey(gc, ctx->prelude);
	grey(gc, ctx->loaded_modules);

	for (uint32_t i = 0; i < gc->ctx->troot_top; ++i) {
		grey(gc, gc->ctx->troots[i]);
	}
	
	if (gc->ctx->current_thread) {
		bt_Thread* thr = gc->ctx->current_thread;
		uint32_t top = thr->top + thr->callstack[thr->depth - 1].size;

		for (uint32_t i = 0; i < thr->depth; ++i) {
			bt_StackFrame* stck = &thr->callstack[i];
			grey(gc, stck->callable);

			uint32_t ltop = thr->top + stck->size;
			top = top > ltop ? top : ltop;
		}

		for (uint32_t i = 0; i < top; ++i) {
			bt_Value val = thr->stack[i];
			if (BT_IS_OBJECT(val)) grey(gc, BT_AS_OBJECT(val));
		}

	}

	while (gc->grey_count) {
		bt_Object* obj = gc->greys[--gc->grey_count];
		blacken(gc, obj);
	}

	uint32_t n_collected = 0;

	bt_Object* prev = ctx->root;
	bt_Object* current = BT_OBJECT_NEXT(prev);

	bt_Thread gc_thread = { 0 };
	gc_thread.context = ctx;
	gc_thread.depth++;

	bt_Thread* old_thr = ctx->current_thread;
	ctx->current_thread = &gc_thread;

	while (current) {
		if (BT_OBJECT_GET_MARK(current)) {
			BT_OBJECT_CLEAR(current);

			prev = current;
			current = BT_OBJECT_NEXT(current);
		}
		else {
			bt_Object* to_free = current;
				
			current = BT_OBJECT_NEXT(current);
			BT_OBJECT_SET_NEXT(prev, current);
			bt_free(ctx, to_free);

			n_collected++;
			//if (n_collected >= max_collect) return n_collected;
		}
	}

	gc->next_cycle = (gc->byets_allocated * 175) / 100;
	if (gc->next_cycle < gc->min_size) gc->next_cycle = gc->min_size;

	//printf("------ GC performed, new heap size: %lld, next at: %lld\n", gc->byets_allocated, gc->next_cycle);

	ctx->current_thread = old_thr;

	return n_collected;
}
