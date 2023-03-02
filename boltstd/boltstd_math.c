#include "boltstd_math.h"

#include "../bt_embedding.h"

#define _USE_MATH_DEFINES
#include <math.h>

static void bt_max(bt_Context* ctx, bt_Thread* thread)
{
	uint8_t argc = bt_argc(thread);
	bt_number max = BT_AS_NUMBER(bt_arg(thread, 0));
	for (uint8_t i = 1; i < argc; ++i) {
		bt_number arg = BT_AS_NUMBER(bt_arg(thread, i));
		max = max > arg ? max : arg;
	}

	bt_return(thread, BT_VALUE_NUMBER(max));
}

static void bt_min(bt_Context* ctx, bt_Thread* thread)
{
	uint8_t argc = bt_argc(thread);
	bt_number min = BT_AS_NUMBER(bt_arg(thread, 0));
	for (uint8_t i = 1; i < argc; ++i) {
		bt_number arg = BT_AS_NUMBER(bt_arg(thread, i));
		min = min < arg ? min : arg;
	}

	bt_return(thread, BT_VALUE_NUMBER(min));
}

static double deg(double x) { return (x * 180.0) / M_PI; }
static double rad(double x) { return (x / 180.0) * M_PI; }

#define SIMPLE_OP(name, op)                               \
static void bt_##name(bt_Context* ctx, bt_Thread* thread) \
{                                                         \
	bt_number num = BT_AS_NUMBER(bt_arg(thread, 0));      \
	bt_return(thread, BT_VALUE_NUMBER(op(num)));          \
} 

SIMPLE_OP(sqrt, sqrt);
SIMPLE_OP(abs, fabs);
SIMPLE_OP(round, round);
SIMPLE_OP(ceil, ceil);
SIMPLE_OP(floor, floor);
SIMPLE_OP(trunc, trunc);
SIMPLE_OP(sign, signbit);

SIMPLE_OP(sin, sin);
SIMPLE_OP(cos, cos);
SIMPLE_OP(tan, tan);

SIMPLE_OP(asin, asin);
SIMPLE_OP(acos, acos);
SIMPLE_OP(atan, atan);

SIMPLE_OP(sinh, sinh);
SIMPLE_OP(cosh, cosh);
SIMPLE_OP(tanh, tanh);

SIMPLE_OP(asinh, asinh);
SIMPLE_OP(acosh, acosh);
SIMPLE_OP(atanh, atanh);

SIMPLE_OP(log, log);
SIMPLE_OP(log10, log10);
SIMPLE_OP(log2, log2);
SIMPLE_OP(exp, exp);

SIMPLE_OP(deg, deg);
SIMPLE_OP(rad, rad);

#define COMPLEX_OP(name, op)                              \
static void bt_##name(bt_Context* ctx, bt_Thread* thread) \
{                                                         \
	bt_number num1 = BT_AS_NUMBER(bt_arg(thread, 0));     \
	bt_number num2 = BT_AS_NUMBER(bt_arg(thread, 1));     \
	bt_return(thread, BT_VALUE_NUMBER(op(num1, num2)));   \
} 

COMPLEX_OP(pow, pow);
COMPLEX_OP(mod, fmod);
COMPLEX_OP(atan2, atan2);

void boltstd_open_math(bt_Context* context)
{
	bt_Module* module = bt_make_user_module(context);

	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "pi"), BT_VALUE_NUMBER(M_PI));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "tau"), BT_VALUE_NUMBER(M_PI*2.0));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "huge"), BT_VALUE_NUMBER(_HUGE_ENUF));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "infinity"), BT_VALUE_NUMBER(INFINITY));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "nan"), BT_VALUE_NUMBER(NAN));

	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "e"), BT_VALUE_NUMBER(M_E));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "ln2"), BT_VALUE_NUMBER(M_LN2));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "ln10"), BT_VALUE_NUMBER(M_LN10));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "log2e"), BT_VALUE_NUMBER(M_LOG2E));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "log10e"), BT_VALUE_NUMBER(M_LOG10E));
	
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "sqrt2"), BT_VALUE_NUMBER(M_SQRT2));
	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "sqrthalf"), BT_VALUE_NUMBER(M_SQRT1_2));

	bt_module_export(context, module, context->types.number, BT_VALUE_CSTRING(context, "epsilon"), BT_VALUE_NUMBER(BT_EPSILON));

	bt_Type* single_num_arg[] = { context->types.number };
	bt_Type* double_num_arg[] = { context->types.number, context->types.number };

	bt_Type* min_max_sig = bt_make_vararg(context, bt_make_signature(context, context->types.number, single_num_arg, 1), context->types.number);

	bt_module_export(context, module, min_max_sig, BT_VALUE_CSTRING(context, "min"), BT_VALUE_OBJECT(bt_make_native(context, min_max_sig, bt_min)));
	bt_module_export(context, module, min_max_sig, BT_VALUE_CSTRING(context, "max"), BT_VALUE_OBJECT(bt_make_native(context, min_max_sig, bt_max)));

	bt_Type* num_to_num_sig = bt_make_signature(context, context->types.number, single_num_arg, 1);
	bt_Type* two_num_to_num_sig = bt_make_signature(context, context->types.number, double_num_arg, 2);

#define IMPL_SIMPLE_OP(name) \
bt_module_export(context, module, num_to_num_sig, BT_VALUE_CSTRING(context, #name), BT_VALUE_OBJECT(bt_make_native(context, num_to_num_sig, bt_##name)));

#define IMPL_COMPLEX_OP(name) \
bt_module_export(context, module, two_num_to_num_sig, BT_VALUE_CSTRING(context, #name), BT_VALUE_OBJECT(bt_make_native(context, two_num_to_num_sig, bt_##name)));

	IMPL_SIMPLE_OP(sqrt);
	IMPL_SIMPLE_OP(abs);
	IMPL_SIMPLE_OP(round);
	IMPL_SIMPLE_OP(ceil);
	IMPL_SIMPLE_OP(floor);
	IMPL_SIMPLE_OP(trunc);
	IMPL_SIMPLE_OP(sign);

	IMPL_SIMPLE_OP(sin);
	IMPL_SIMPLE_OP(cos);
	IMPL_SIMPLE_OP(tan);

	IMPL_SIMPLE_OP(asin);
	IMPL_SIMPLE_OP(acos);
	IMPL_SIMPLE_OP(atan);

	IMPL_SIMPLE_OP(sinh);
	IMPL_SIMPLE_OP(cosh);
	IMPL_SIMPLE_OP(tanh);

	IMPL_SIMPLE_OP(asinh);
	IMPL_SIMPLE_OP(acosh);
	IMPL_SIMPLE_OP(atanh);

	IMPL_SIMPLE_OP(log);
	IMPL_SIMPLE_OP(log10);
	IMPL_SIMPLE_OP(log2);
	IMPL_SIMPLE_OP(exp);

	IMPL_SIMPLE_OP(deg);
	IMPL_SIMPLE_OP(rad);

	IMPL_COMPLEX_OP(pow);
	IMPL_COMPLEX_OP(mod);
	IMPL_COMPLEX_OP(atan2);

	bt_register_module(context, BT_VALUE_CSTRING(context, "math"), module);
}