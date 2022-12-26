#pragma once

#include "bt_buffer.h"
#include "bt_object.h"

#include <stdint.h>

typedef enum bt_GCPhase {
	BT_GC_PHASE_MARK,
	BT_GC_PHASE_SWEEP,
} bt_GCPhase;

typedef struct bt_GC {
	bt_Context* ctx;
	
	bt_Object* heap;
	bt_Table** loaded_modules;
	bt_Table** registered_types;
	bt_Table** prelude;

	bt_Object* current;
	bt_Object* last;
} bt_GC;


bt_GC bt_make_gc(bt_Context* ctx, bt_Object* heap, bt_Table** loaded_modules, bt_Table** registered_types, bt_Table** prelude);

uint32_t bt_collect(bt_GC* gc, uint32_t max_collect);