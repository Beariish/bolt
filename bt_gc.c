#include "bt_gc.h"
#include "bt_type.h"
#include "bt_context.h"

#include <stdio.h>
#include <assert.h>

bt_GC bt_make_gc(bt_Context* ctx, bt_BucketedBuffer* vheap, bt_Table** loaded_modules, bt_Table** registered_types, bt_Table** prelude)
{
	return (bt_GC) { ctx, vheap, loaded_modules, registered_types, prelude, BT_GC_PHASE_MARK, vheap->root, 0 };
}

static void reference_all(bt_Object* obj)
{
	if (!obj || obj->mark) return;

	obj->mark = 1;
	switch (obj->type) {
	case BT_OBJECT_TYPE_NONE: assert(0 && "What.");
	case BT_OBJECT_TYPE_TYPE: {
		bt_Type* as_type = obj;
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
			reference_all(as_type->as.table_shape.proto);
			reference_all(as_type->as.table_shape.layout);
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
			if(BT_IS_REFERENCE(constant)) reference_all(BT_AS_OBJECT(constant));
		}
	} break;
	case BT_OBJECT_TYPE_IMPORT: {
		bt_ModuleImport* import = obj;
		reference_all(import->type);
		reference_all(import->name);
		if(BT_IS_REFERENCE(import->value)) reference_all(BT_AS_OBJECT(import->value));
	} break;
	case BT_OBJECT_TYPE_FN: {
		bt_Fn* fn = obj;
		reference_all(fn->module);
		reference_all(fn->signature);
		for (uint32_t i = 0; i < fn->constants.length; ++i) {
			bt_Value constant = bt_buffer_at(&fn->constants, i);
			if (BT_IS_REFERENCE(constant)) reference_all(BT_AS_OBJECT(constant));
		};
	} break;
	case BT_OBJECT_TYPE_CLOSURE: {
		bt_Closure* cl = obj;
		reference_all(cl->fn);
		for (uint32_t i = 0; i < cl->upvals.length; ++i) {
			bt_Value upval = bt_buffer_at(&cl->upvals, i);
			if (BT_IS_REFERENCE(upval)) reference_all(BT_AS_OBJECT(upval));
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
			if (BT_IS_REFERENCE(pair->key))   reference_all(BT_AS_OBJECT(pair->key));
			if (BT_IS_REFERENCE(pair->value)) reference_all(BT_AS_OBJECT(pair->value));
		}
	} break;
	}
}

uint32_t bt_collect(bt_GC* gc, uint32_t max_collect)
{
	if (gc->phase == BT_GC_PHASE_MARK) {
		if (gc->progress == 0) {
			reference_all(*gc->registered_types);
			gc->progress++;
			return 1;
		}
		else if (gc->progress == 1) {
			reference_all(*gc->prelude);
			gc->progress++;
			return 1;
		}
		else if (gc->progress == 2) {
			reference_all(*gc->loaded_modules);
			gc->progress = 0;
			gc->phase = BT_GC_PHASE_SWEEP;
			return 1;
		}
	}
	else {
		uint32_t n_collected = 0;
		if (max_collect == 0) max_collect = UINT32_MAX;

		while (gc->current) {
			for (uint32_t i = 0; i < gc->current->length; ++i) {
				bt_Object* obj = *(bt_Object**)bt_bucket_at(gc->current, i);
				if (obj->mark) obj->mark = 0;
				else {
					bt_free_from(gc->ctx, gc->current, obj);
					n_collected++;
					if (n_collected >= max_collect) return n_collected;
					i--;
				}
			}

			gc->current = gc->current->next;
		}

		if (!gc->current) {
			gc->phase = BT_GC_PHASE_MARK;
			gc->current = gc->vheap->root;
		}

		return n_collected;
	}

	return 0;
}
