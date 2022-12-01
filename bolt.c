#include "bolt.h"

#include "bt_object.h"

static bt_Type* make_primitive_type(bt_Context* ctx, const char* name, bt_TypeSatisfier satisfier)
{
	return bt_make_type(ctx, name, satisfier, BT_TYPE_CATEGORY_PRIMITIVE, BT_FALSE);
}

void bt_open(bt_Context* context, bt_Alloc allocator, bt_Free free)
{
	context->alloc = allocator;
	context->free = free;

	context->heap = BT_BUCKETED_BUFFER_NEW(context, 256, bt_Object*);

	context->types.number = make_primitive_type(context, "number", bt_type_satisfier_same);
	context->types.boolean = make_primitive_type(context, "bool", bt_type_satisfier_same);
	context->types.string = make_primitive_type(context, "string", bt_type_satisfier_same);
	context->types.table = make_primitive_type(context, "table", bt_type_satisfier_same);

	context->types.any = make_primitive_type(context, "any", bt_type_satisfier_any);
	context->types.any->is_optional = BT_TRUE;

	context->types.null = make_primitive_type(context, "null", bt_type_satisfier_null);
	context->types.null->is_optional = BT_TRUE;

	context->types.array = make_primitive_type(context, "array", bt_type_satisfier_array);
	context->types.array->category = BT_TYPE_CATEGORY_ARRAY;
	context->types.array->as.array.inner = context->types.any;

	context->type_registry = bt_make_table(context, 16);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "number")), context->types.number);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "bool")), context->types.boolean);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "string")), context->types.string);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "table")), context->types.table);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "any")), context->types.any);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "null")), context->types.null);
	bt_register_type(context, BT_VALUE_STRING(bt_make_string_hashed(context, "array")), context->types.array);

	context->is_valid = BT_TRUE;
}

bt_Object* bt_allocate(bt_Context* context, uint32_t full_size, bt_ObjectType type)
{
	bt_Object* obj = context->alloc(full_size);
	obj->type = type;

	obj->heap_idx = bt_bucketed_buffer_insert(context, &context->heap, &obj);

	return obj;
}

void bt_register_type(bt_Context* context, bt_Value name, bt_Type* type)
{
	bt_table_set(context, context->type_registry, name, BT_VALUE_OBJECT(type));
}

bt_Type* bt_find_type(bt_Context* context, bt_Value name)
{
	return BT_AS_OBJECT(bt_table_get(context->type_registry, name));
}

/*
import sqrt from "math"

type Vec2 = { x: number, y: number }

method Vec2.__new(x: number, y: number) {
	this.x = x
	this.y = y
}

method Vec2.__add(rhs: Vec2): Vec2 {
	return new Vec2(
		this.x + rhs.x,
		this.y + rhs.y)
}

method Vec2.length: number {
	return sqrt(this.x * this.x + this.y * this.y)
}

let a = new Vec2(10, 20)
let b = new Vec2(4, 5)

print((a + b).length()) 

*/