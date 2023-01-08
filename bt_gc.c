#include "bt_gc.h"
#include "bt_type.h"
#include "bt_context.h"

#include <stdio.h>
#include <assert.h>

bt_GC bt_make_gc(bt_Context* ctx, bt_Object* heap, bt_Table** loaded_modules, bt_Table** registered_types, bt_Table** prelude)
{
	return (bt_GC) { ctx, heap, loaded_modules, registered_types, prelude, 0, 0 };
}

static void reference_all(bt_Object* obj)
{
	if (!obj || obj->mark) return;

	obj->mark = 1;
	switch (obj->type) {
	case BT_OBJECT_TYPE_NONE: break; // Reserved for root object
	case BT_OBJECT_TYPE_TYPE: {
		bt_Type* as_type = obj;
		if (as_type->is_optional) as_type = as_type->as.nullable.base;
		as_type->obj.mark = 1;

		switch (as_type->category) {
		case BT_TYPE_CATEGORY_ARRAY:
			reference_all(as_type->as.array.inner);
			break;
		case BT_TYPE_CATEGORY_NATIVE_FN:
		case BT_TYPE_CATEGORY_SIGNATURE: {
			reference_all(as_type->as.fn.return_type);
			reference_all(as_type->as.fn.varargs_type);
			for (uint32_t i = 0; i < as_type->as.fn.args.length; ++i) {
				bt_Type* arg = *(bt_Type**)bt_buffer_at(&as_type->as.fn.args, i);
				reference_all(arg);
			}
		} break;
		case BT_TYPE_CATEGORY_TABLESHAPE: {
			reference_all(as_type->as.table_shape.values);
			reference_all(as_type->as.table_shape.layout);
		} break;
		case BT_TYPE_CATEGORY_TYPE: {
			reference_all(as_type->as.type.boxed);
		} break;
		}
	} break;
	case BT_OBJECT_TYPE_MODULE: {
		bt_Module* mod = obj;
		reference_all(mod->type);
		reference_all(mod->exports);

		for (uint32_t i = 0; i < mod->imports.length; ++i) {
			bt_Object* import = *(bt_Object**)bt_buffer_at(&mod->imports, i);
			reference_all(import);
		}

		for (uint32_t i = 0; i < mod->constants.length; ++i) {
			bt_Value constant = bt_buffer_at(&mod->constants, i);
			if(BT_IS_OBJECT(constant)) reference_all(BT_AS_OBJECT(constant));
		}
	} break;
	case BT_OBJECT_TYPE_IMPORT: {
		bt_ModuleImport* import = obj;
		reference_all(import->type);
		reference_all(import->name);
		if(BT_IS_OBJECT(import->value)) reference_all(BT_AS_OBJECT(import->value));
	} break;
	case BT_OBJECT_TYPE_FN: {
		bt_Fn* fn = obj;
		reference_all(fn->module);
		reference_all(fn->signature);
		for (uint32_t i = 0; i < fn->constants.length; ++i) {
			bt_Value constant = bt_buffer_at(&fn->constants, i);
			if (BT_IS_OBJECT(constant)) reference_all(BT_AS_OBJECT(constant));
		};
	} break;
	case BT_OBJECT_TYPE_CLOSURE: {
		bt_Closure* cl = obj;
		reference_all(cl->fn);
		for (uint32_t i = 0; i < cl->upvals.length; ++i) {
			bt_Value upval = bt_buffer_at(&cl->upvals, i);
			if (BT_IS_OBJECT(upval)) reference_all(BT_AS_OBJECT(upval));
		};
	} break;
	case BT_OBJECT_TYPE_NATIVE_FN: {
		bt_NativeFn* ntfn = obj;
		reference_all(ntfn->type);
	} break;
	case BT_OBJECT_TYPE_TABLE: {
		bt_Table* tbl = obj;
		reference_all(tbl->prototype);
		for (uint32_t i = 0; i < tbl->pairs.length; i++) {
			bt_TablePair* pair = bt_buffer_at(&tbl->pairs, i);
			if (BT_IS_OBJECT(pair->key))   reference_all(BT_AS_OBJECT(pair->key));
			if (BT_IS_OBJECT(pair->value)) reference_all(BT_AS_OBJECT(pair->value));
		}
	} break;
	}
}

uint32_t bt_collect(bt_GC* gc, uint32_t max_collect)
{
	reference_all(gc->heap);
	reference_all(*gc->registered_types);
	reference_all(*gc->prelude);
	reference_all(*gc->loaded_modules);

	for (uint32_t i = 0; i < gc->ctx->troot_top; ++i) {
		reference_all(gc->ctx->troots[i]);
	}
	
	if (gc->ctx->current_thread) {
		bt_Thread* thr = gc->ctx->current_thread;
		uint32_t top = thr->top + thr->callstack[thr->depth - 1].size;
		for (uint32_t i = 0; i < top; ++i) {
			bt_Value val = thr->stack[i];
			if (BT_IS_OBJECT(val)) reference_all(BT_AS_OBJECT(val));
		}

		for (uint32_t i = 0; i < thr->depth; ++i) {
			bt_StackFrame* stck = &thr->callstack[i];
			reference_all(stck->callable);
		}
	}

	uint32_t n_collected = 0;
	if (max_collect == 0) max_collect = UINT32_MAX;

	if (!gc->current) {
		gc->current = gc->heap;
		gc->last = 0;
	}

	while (gc->current) {
		if (gc->current->mark) {
			gc->current->mark = 0;
			
			gc->last = gc->current;
			gc->current = BT_OBJECT_NEXT(gc->current);
		}
		else {
			bt_Object* to_free = gc->current;
				
			gc->current = BT_OBJECT_NEXT(gc->current);
			if (gc->last) BT_OBJECT_SET_NEXT(gc->last, gc->current);
				
			bt_free(gc->ctx, to_free);

			n_collected++;
			if (n_collected >= max_collect) return n_collected;
		}
	}

	return n_collected;
}
