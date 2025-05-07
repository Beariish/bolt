#include "bt_compiler.h"
#include "bt_value.h"

#ifdef BT_DEBUG
#include <assert.h>
#endif

#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "bt_context.h"
#include "bt_debug.h"
#include "bt_object.h"

static const uint8_t INVALID_BINDING = 255;

typedef struct CompilerBinding {
    bt_StrSlice name;
    uint8_t loc;
} CompilerBinding;

typedef enum StorageClass {
    STORAGE_INVALID,
    STORAGE_REGISTER,
    STORAGE_UPVAL,
    STORAGE_INDEX
} StorageClass;

typedef struct RegisterState {
    uint64_t regs[4];
} RegisterState;

typedef struct FunctionContext {
    CompilerBinding bindnings[128];
    uint8_t binding_tops[32];

    RegisterState registers;
    RegisterState temps[32];

    uint16_t loop_starts[16];
    uint16_t pending_breaks[16][16];
    uint8_t break_counts[16];

    bt_Buffer(bt_Constant) constants;
    bt_InstructionBuffer output;
    bt_DebugLocBuffer debug;

    bt_Compiler* compiler;
    bt_Context* context;

    bt_Module* module;
    bt_AstNode* fn;

    struct FunctionContext* outer;
    
    uint8_t loop_depth;
    uint8_t temp_top;
    uint8_t scope_depth;
    uint8_t binding_top;
    uint8_t min_top_register;
} FunctionContext;

static uint8_t get_register(FunctionContext* ctx);
static uint8_t get_registers(FunctionContext* ctx, uint8_t count);
static bt_Fn* compile_fn(bt_Compiler* compiler, FunctionContext* parent, bt_AstNode* fn);
static uint8_t find_upval(FunctionContext* ctx, bt_StrSlice name);
static uint8_t find_binding(FunctionContext* ctx, bt_StrSlice name);
static uint8_t find_named(FunctionContext* ctx, bt_StrSlice name);
static uint8_t push(FunctionContext* ctx, bt_Value value);
static uint8_t push_load(FunctionContext* ctx, bt_Value value);

// ffsll intrinsic isn't on all platforms. some complicated platform defines could speed this up
// but it's good enough for now
static uint8_t internal_ffsll(uint64_t mask)
{
    uint8_t bit;

    if (mask == 0) return 0;

    for (bit = 1; !(mask & 1); bit++) {
        mask >>= 1;
    }

    return bit;
}

static void table_ensure_template_made(bt_Context* ctx, bt_Type* tblshp)
{
    if (tblshp->as.table_shape.tmpl == 0)
    {
        bt_Table* layout = tblshp->as.table_shape.layout;

        bt_Table* result = bt_make_table(ctx, layout ? layout->length : 0);
        if (layout) {
            for (uint8_t i = 0; i < layout->length; ++i)
            {
                bt_TablePair* pair = BT_TABLE_PAIRS(layout) + i;
                bt_table_set(ctx, result, pair->key, BT_VALUE_NULL);
            }
        }

        result->prototype = bt_type_get_proto(ctx, tblshp);

        tblshp->as.table_shape.tmpl = result;
    }
}

static uint32_t emit_op(FunctionContext* ctx, bt_Op op)
{
    bt_buffer_push(ctx->context, &ctx->output, op);

    if (ctx->compiler->options.generate_debug_info) {
        if (ctx->compiler->debug_top) {
            bt_AstNode* node = ctx->compiler->debug_stack[ctx->compiler->debug_top - 1];
            if (node->source) {
                bt_buffer_push(ctx->context, &ctx->debug, node->source->idx);
            }
            else {
#ifdef BT_DEBUG
                //assert(0 && "AST node is missing source!");
#endif
                bt_buffer_push(ctx->context, &ctx->debug, 0);
            }
        } else {
            bt_buffer_push(ctx->context, &ctx->debug, 0);
        }
    }

    return ctx->output.length - 1;
}

static uint32_t emit_abc(FunctionContext* ctx, bt_OpCode code, uint8_t a, uint8_t b, uint8_t c, bt_bool is_accelerated)
{
    bt_Op op = BT_MAKE_OP_ABC(code, a, b, c);
    if (is_accelerated) op = BT_ACCELERATE_OP(op);
    return emit_op(ctx, op);
}

static uint32_t emit_aibc(FunctionContext* ctx, bt_OpCode code, uint8_t a, int16_t ibc)
{
    bt_Op op = BT_MAKE_OP_AIBC(code, a, ibc);
    return emit_op(ctx, op);
}

static uint32_t emit_ab(FunctionContext* ctx, bt_OpCode code, uint8_t a, uint8_t b, bt_bool is_accelerated)
{
    return emit_abc(ctx, code, a, b, 0, is_accelerated);
}

static uint32_t emit_a(FunctionContext* ctx, bt_OpCode code, uint8_t a)
{
    return emit_abc(ctx, code, a, 0, 0, BT_FALSE);
}

static uint32_t emit(FunctionContext* ctx, bt_OpCode code)
{
    return emit_abc(ctx, code, 0, 0, 0, BT_FALSE);
}

static void compile_error(bt_Compiler* compiler, const char* message, uint16_t line, uint16_t col)
{
    compiler->context->on_error(BT_ERROR_COMPILE, compiler->input->tokenizer->source_name, message, line, col);
    compiler->has_errored = BT_TRUE;
}

static void compile_error_fmt(bt_Compiler* compiler, const char* format, uint16_t line, uint16_t col, ...)
{
    va_list va;
    va_start(va, col);

    char message[4096];
#ifdef _MSC_VER
    message[vsprintf_s(message, sizeof(message) - 1, format, va)] = 0;
#else
    message[vsprintf(message, format, va)] = 0;
#endif
    va_end(va);

    compile_error(compiler, message, line, col);
}

static void compile_error_token(bt_Compiler* compiler, const char* format, bt_Token* source)
{
    compile_error_fmt(compiler, format, source->line, source->col, source->source.length, source->source.source);
}

static void load_fn(FunctionContext* ctx, bt_AstNode* expr, bt_Fn* fn, uint8_t result_loc) {
    uint8_t idx = push(ctx, BT_VALUE_OBJECT(fn));

    if (expr->as.fn.upvals.length == 0) {
        emit_ab(ctx, BT_OP_LOAD, result_loc, idx, BT_FALSE);
    }
    else {
        uint8_t start = get_registers(ctx, expr->as.fn.upvals.length + 1);

        emit_ab(ctx, BT_OP_LOAD, start, idx, BT_FALSE);

        for (uint8_t i = 0; i < expr->as.fn.upvals.length; ++i) {
            bt_ParseBinding* binding = expr->as.fn.upvals.elements + i;
            uint8_t loc = find_binding(ctx, binding->name);
            if (loc != INVALID_BINDING) {
                emit_ab(ctx, BT_OP_MOVE, start + i + 1, loc, BT_FALSE);
                continue;
            }

            loc = find_upval(ctx, binding->name);
            if (loc != INVALID_BINDING) {
                emit_ab(ctx, BT_OP_LOADUP, start + i + 1, loc, BT_FALSE);
                continue;
            }

            loc = find_named(ctx, binding->name);
            if (loc != INVALID_BINDING) {
                emit_ab(ctx, BT_OP_LOAD, start + i + 1, loc, BT_FALSE);
                continue;
            }

            compile_error_fmt(ctx->compiler, "Failed to find identifier '%.*s'", expr->source->line, expr->source->col,
                binding->name.length, binding->name.source);
        }

        emit_abc(ctx, BT_OP_CLOSE, result_loc, start, expr->as.fn.upvals.length, BT_FALSE);
    }
}

static bt_Module* find_module(FunctionContext* ctx)
{
    while (ctx) {
        if (ctx->module) return ctx->module;
        ctx = ctx->outer;
    }

    compile_error(ctx->compiler, "Internal compiler error - function has no module context", 0, 0);
    return NULL;
}

static void push_scope(FunctionContext* ctx)
{
    ctx->binding_tops[ctx->scope_depth++] = ctx->binding_top;
}

static void pop_scope(FunctionContext* ctx)
{
    ctx->binding_top = ctx->binding_tops[--ctx->scope_depth];
}

static uint8_t make_binding_at_loc(FunctionContext* ctx, bt_StrSlice name, uint8_t loc)
{
    for (uint32_t i = ctx->binding_tops[ctx->scope_depth - 1]; i < ctx->binding_top; ++i) {
        CompilerBinding* binding = ctx->bindnings + i;
        if (bt_strslice_compare(binding->name, name)) {
            compile_error(ctx->compiler, "Binding '%.*s' already exists in this scope", 0, 0);
        }
    }

    CompilerBinding new_binding;
    new_binding.loc = loc;
    new_binding.name = name;

    ctx->bindnings[ctx->binding_top++] = new_binding;
    
    return new_binding.loc;
}

static uint8_t make_binding(FunctionContext* ctx, bt_StrSlice name) 
{
    return make_binding_at_loc(ctx, name, get_register(ctx));
}

static uint8_t find_binding(FunctionContext* ctx, bt_StrSlice name)
{
    for (int32_t i = ctx->binding_top - 1; i >= 0; --i) {
        CompilerBinding* binding = ctx->bindnings + i;
        if (bt_strslice_compare(binding->name, name)) {
            return binding->loc;
        }
    }

    return INVALID_BINDING;
}

static uint8_t find_upval(FunctionContext* ctx, bt_StrSlice name)
{
    bt_AstNode* fn = ctx->fn;
    if (!fn) {
        return INVALID_BINDING;
    }

    for (uint32_t i = 0; i < fn->as.fn.upvals.length; i++) {
        bt_ParseBinding* bind = fn->as.fn.upvals.elements + i;
        if (bt_strslice_compare(bind->name, name)) {
            return i;
        }
    }

    return INVALID_BINDING;
}

static uint16_t find_import(FunctionContext* ctx, bt_StrSlice name)
{
    bt_Module* mod = find_module(ctx);

    for (uint32_t i = 0; i < mod->imports.length; ++i) {
        bt_ModuleImport* import = mod->imports.elements[i];
        if (bt_strslice_compare(bt_as_strslice(import->name), name)) {
            return i;
        }
    }

    return INVALID_BINDING;
}

bt_Compiler bt_open_compiler(bt_Parser* parser, bt_CompilerOptions options)
{
    bt_Compiler result;
    result.context = parser->context;
    result.input = parser;
    result.options = options;
    result.debug_top = 0;
    result.has_errored = BT_FALSE;
    memset(result.debug_stack, 0, sizeof(bt_AstNode*) * 128);

    return result;
}

void bt_close_compiler(bt_Compiler* compiler)
{
    // The compiler doesn't actually own any memory yet, but this is here for parity with other parts for now
}

static bt_Op* op_at(FunctionContext* ctx, uint32_t idx)
{
    return ctx->output.elements + idx;
}

static uint32_t op_count(FunctionContext* ctx)
{
    return ctx->output.length;    
}

static uint8_t push(FunctionContext* ctx, bt_Value value)
{
    for (uint8_t idx = 0; idx < ctx->constants.length; idx++)
    {
        bt_Constant* constant = ctx->constants.elements + idx;
        if (constant->value == value) {
            return idx;
        }

        if (bt_is_object(constant->value) && bt_is_object(value)) {
            bt_Object* obja = BT_AS_OBJECT(constant->value);
            bt_Object* objb = BT_AS_OBJECT(value);
            if (BT_OBJECT_GET_TYPE(obja) == BT_OBJECT_TYPE_STRING && BT_OBJECT_GET_TYPE(objb) == BT_OBJECT_TYPE_STRING) {
                if (bt_value_is_equal(constant->value, value)) {
                    return idx;
                }
            }
        }
    }

    bt_Constant con;
    con.name.length = 0;
    con.value = value;

    bt_buffer_push(ctx->context, &ctx->constants, con);
    return ctx->constants.length - 1;
}

static uint8_t push_load(FunctionContext* ctx, bt_Value value)
{
    uint8_t constant_idx = push(ctx, value);
    uint8_t destination = get_register(ctx);
    emit_aibc(ctx, BT_OP_LOAD, destination, constant_idx);
    return destination;
}

static uint8_t push_named(FunctionContext* ctx, bt_StrSlice name, bt_Value value)
{
    for (uint8_t idx = 0; idx < ctx->constants.length; idx++)
    {
        bt_Constant* constant = ctx->constants.elements + idx;
        if (bt_strslice_compare(constant->name, name)) { return idx; }
        if (bt_value_is_equal(constant->value, value)) { return idx; }
    }

    bt_Constant con;
    con.value = value;
    con.name = name;

    bt_buffer_push(ctx->context, &ctx->constants, con);
    uint32_t ret = ctx->constants.length - 1;

    return ret;
}

static uint8_t find_named(FunctionContext* ctx, bt_StrSlice name)
{
    for (uint8_t idx = 0; idx < ctx->constants.length; idx++)
    {
        bt_Constant* constant = ctx->constants.elements + idx;
        if (bt_strslice_compare(constant->name, name)) return idx;
    }

    return INVALID_BINDING;
}

static uint8_t get_register(FunctionContext* ctx)
{
    uint8_t offset = 0;
    for (uint8_t idx = 0; idx < 4; ++idx, offset += 64) {
        uint64_t mask = ctx->registers.regs[idx];
        if (mask == UINT64_MAX) continue;
        uint8_t found = internal_ffsll(~mask);
        mask |= (1ull << (found - 1));
        ctx->registers.regs[idx] = mask;

        uint8_t result = offset + found;
        if (result > ctx->min_top_register) ctx->min_top_register = result;
        return result - 1;
    }

    return UINT8_MAX;
}

static uint8_t get_registers(FunctionContext* ctx, uint8_t count)
{
    uint64_t search_mask = 0;
    for (uint8_t i = 0; i < count; ++i) {
        search_mask |= 1ull << i;
    }

    uint8_t offset = 0;
    for (uint8_t idx = 0; idx < 4; ++idx, offset += 64) {
        uint64_t mask = ctx->registers.regs[idx];
        if (mask == UINT64_MAX) continue;

        uint8_t found = UINT8_MAX;
        for (uint8_t j = 0; j < 64 - count; j++) {
            if (((~mask) & search_mask) == search_mask) {
                found = j;

                ctx->registers.regs[idx] |= search_mask << j;

                break;
            }

            mask >>= 1;
        }

        if (found != UINT8_MAX) {
            uint8_t result = offset + found;
            if (result + count > ctx->min_top_register) ctx->min_top_register = result + count;
            return result;
        }
    }

    return UINT8_MAX;
}

static void push_registers(FunctionContext* ctx)
{
    ctx->temps[ctx->temp_top++] = ctx->registers;
}

static void restore_registers(FunctionContext* ctx)
{
    ctx->registers = ctx->temps[--ctx->temp_top];
}

static bt_bool compile_expression(FunctionContext* ctx, bt_AstNode* expr, uint8_t result_loc);

static uint8_t find_binding_or_compile_temp(FunctionContext* ctx, bt_AstNode* expr)
{
    uint8_t loc = INVALID_BINDING;

    if(expr->type == BT_AST_NODE_IDENTIFIER) {
        loc = find_named(ctx, expr->source->source);
        if (loc != INVALID_BINDING) {
            uint8_t rloc = get_register(ctx);
            emit_ab(ctx, BT_OP_LOAD, rloc, loc, BT_FALSE);
            return rloc;
        }

        loc = find_binding(ctx, expr->source->source);
        
        if (loc == INVALID_BINDING) {
            loc = get_register(ctx);
            if (!compile_expression(ctx, expr, loc)) compile_error_token(ctx->compiler, "Failed to compile operand", expr->source);
        }
    }

    if (loc == INVALID_BINDING) {
        loc = get_register(ctx);
        if (!compile_expression(ctx, expr, loc)) compile_error_token(ctx->compiler, "Failed to compile operand", expr->source);
    }

    if (loc == INVALID_BINDING) {
        compile_error_token(ctx->compiler, "Cannot find binding '%.*s'", expr->source);
    }

    return loc;
}

static StorageClass get_storage(FunctionContext* ctx, bt_AstNode* expr)
{
    uint8_t loc = find_binding(ctx, expr->source->source);
    if (loc != INVALID_BINDING) {
        return STORAGE_REGISTER;
    }

    loc = find_upval(ctx, expr->source->source);
    if (loc != INVALID_BINDING) {
        return STORAGE_UPVAL;
    }

    if (expr->type == BT_AST_NODE_BINARY_OP && expr->source->type == BT_TOKEN_PERIOD) {
        return STORAGE_INDEX;
    }
    
    return STORAGE_INVALID;
}

static uint8_t find_binding_or_compile_loc(FunctionContext* ctx, bt_AstNode* expr, uint8_t backup_loc)
{
    uint8_t loc = INVALID_BINDING;
    if (expr->type == BT_AST_NODE_IDENTIFIER) {
        loc = find_binding(ctx, expr->source->source);
    }

    if (loc == INVALID_BINDING) {
        loc = backup_loc;
        if (!compile_expression(ctx, expr, loc)) compile_error_token(ctx->compiler, "Failed to compile operand", expr->source);
    }

    if (loc == INVALID_BINDING) {
        compile_error_token(ctx->compiler, "Cannot find binding '%.*s'", expr->source);
    }

    return loc;
}

static bt_bool is_assigning(bt_TokenType op) {
    switch (op) {
    case BT_TOKEN_ASSIGN:
    case BT_TOKEN_PLUSEQ:
    case BT_TOKEN_MINUSEQ:
    case BT_TOKEN_MULEQ:
    case BT_TOKEN_DIVEQ:
        return BT_TRUE;
    }

    return BT_FALSE;
}

static bt_Value get_from_proto(bt_Type* type, bt_Value key)
{
    if (!type) return BT_VALUE_NULL;

    bt_Table* proto = type->prototype_values;
    while (!proto && type->prototype) {
        proto = type->prototype->prototype_values;
        type = type->prototype;
    }

    if (!proto) {
        return BT_VALUE_NULL;
    }

    return bt_table_get(proto, key);
}

static bt_bool compile_expression(FunctionContext* ctx, bt_AstNode* expr, uint8_t result_loc)
{
    if (ctx->compiler->options.generate_debug_info) {
        ctx->compiler->debug_stack[ctx->compiler->debug_top++] = expr;
    }

    switch (expr->type) {
    case BT_AST_NODE_LITERAL: {
        bt_Token* inner = expr->source;
        switch (inner->type) {
        case BT_TOKEN_TRUE_LITERAL:
            emit_ab(ctx, BT_OP_LOAD_BOOL, result_loc, 1, BT_FALSE);
            break;
        case BT_TOKEN_FALSE_LITERAL:
            emit_ab(ctx, BT_OP_LOAD_BOOL, result_loc, 0, BT_FALSE);
            break;
        case BT_TOKEN_NULL_LITERAL:
            emit_a(ctx, BT_OP_LOAD_NULL, result_loc);
            break;
        case BT_TOKEN_NUMBER_LITERAL: {
            bt_Literal* lit = ctx->compiler->input->tokenizer->literals.elements + inner->idx;
            bt_number num = lit->as_num;

            if (floor(num) == num && num < (bt_number)INT16_MAX && num >(bt_number)INT16_MIN) {
                emit_aibc(ctx, BT_OP_LOAD_SMALL, result_loc, (int16_t)num);
            }
            else {
                uint8_t idx = push(ctx, BT_VALUE_NUMBER(lit->as_num));
                emit_ab(ctx, BT_OP_LOAD, result_loc, idx, BT_FALSE);
            }
        } break;
        case BT_TOKEN_STRING_LITERAL: {
            bt_Literal* lit = ctx->compiler->input->tokenizer->literals.elements + inner->idx;
            uint8_t idx = push(ctx,
                BT_VALUE_OBJECT(bt_make_string_hashed_len_escape(ctx->context, lit->as_str.source, lit->as_str.length)));
            emit_ab(ctx, BT_OP_LOAD, result_loc, idx, BT_FALSE);
        } break;
        case BT_TOKEN_IDENTIFER_LITERAL: {
            uint8_t idx = push(ctx,
                BT_VALUE_OBJECT(bt_make_string_hashed_len(ctx->context, expr->source->source.source, expr->source->source.length)));
            emit_ab(ctx, BT_OP_LOAD, result_loc, idx, BT_FALSE);
        } break;
        }
    } break;
    case BT_AST_NODE_ENUM_LITERAL: {
        uint8_t idx = push(ctx, expr->as.enum_literal.value);
        emit_ab(ctx, BT_OP_LOAD, result_loc, idx, BT_FALSE);
    } break;
    case BT_AST_NODE_IDENTIFIER: { // simple copy
        uint8_t loc = find_binding(ctx, expr->source->source);
        if (loc != INVALID_BINDING) {
            emit_ab(ctx, BT_OP_MOVE, result_loc, loc, BT_FALSE);
            break;
        }

        loc = find_upval(ctx, expr->source->source);
        if (loc != INVALID_BINDING) {
            emit_ab(ctx, BT_OP_LOADUP, result_loc, loc, BT_FALSE);
            break;
        }
         
        loc = find_named(ctx, expr->source->source);
        if (loc != INVALID_BINDING) {
            emit_ab(ctx, BT_OP_LOAD, result_loc, loc, BT_FALSE);
            break;
        }

        compile_error_token(ctx->compiler, "Cannot find binding '%.*s'", expr->source);
    } break;
    case BT_AST_NODE_IMPORT_REFERENCE: {
        uint16_t loc = find_import(ctx, expr->source->source);
        if (loc == INVALID_BINDING) compile_error_token(ctx->compiler, "Cannot find import '%.*s'", expr->source);
        emit_ab(ctx, BT_OP_LOAD_IMPORT, result_loc, (uint8_t)loc, BT_FALSE);
    } break;
    case BT_AST_NODE_CALL: {
        bt_AstNode* lhs = expr->as.call.fn;
        bt_AstBuffer* args = &expr->as.call.args;

        push_registers(ctx);

        uint8_t start_loc = get_registers(ctx, args->length + 1);

        // TODO(bearish): factor this out with common table indexing code
        if (expr->as.call.is_methodcall) {
            if (lhs->source->type != BT_TOKEN_PERIOD) 
                compile_error_token(ctx->compiler, "Expected methodcall to come from index operation '%.*s'", lhs->source); 
        
            uint8_t obj_loc = start_loc + 1;
            compile_expression(ctx, lhs->as.binary_op.left, obj_loc);

            bt_AstNode* rhs = lhs->as.binary_op.right;

            methodcall_hoist_fail:
            if (lhs->as.binary_op.hoistable && ctx->compiler->options.allow_method_hoisting) {
                bt_Value hoisted = get_from_proto(lhs->as.binary_op.from, lhs->as.binary_op.key);
                if (hoisted == BT_VALUE_NULL) {
                    lhs->as.binary_op.hoistable = BT_FALSE;
                    goto methodcall_hoist_fail;
                }
                uint8_t idx = push(ctx, hoisted);
                emit_ab(ctx, BT_OP_LOAD, start_loc, idx, BT_FALSE);
            }
            else if (lhs->as.binary_op.accelerated && ctx->compiler->options.predict_hash_slots) {
                if (lhs->as.binary_op.left->resulting_type->category != BT_TYPE_CATEGORY_ARRAY) {
                    
                    uint8_t idx = push(ctx,
                        BT_VALUE_OBJECT(bt_make_string_hashed_len(ctx->context, rhs->source->source.source, rhs->source->source.length)));
                    emit_abc(ctx, BT_OP_LOAD_IDX, start_loc, obj_loc, lhs->as.binary_op.idx, BT_TRUE);
                    emit_aibc(ctx, BT_OP_IDX_EXT, 0, idx);
                }
            }
            else if (rhs->type == BT_AST_NODE_LITERAL && rhs->resulting_type == ctx->context->types.string && rhs->source->type == BT_TOKEN_IDENTIFER_LITERAL) {
                uint8_t idx = push(ctx,
                    BT_VALUE_OBJECT(bt_make_string_hashed_len(ctx->context, rhs->source->source.source, rhs->source->source.length)));
                bt_Value is_prototypical = get_from_proto(lhs->as.binary_op.from, lhs->as.binary_op.key);

                emit_abc(ctx, (is_prototypical == BT_VALUE_NULL || !ctx->compiler->options.predict_hash_slots) ? BT_OP_LOAD_IDX_K : BT_OP_LOAD_PROTO, start_loc, obj_loc, idx, BT_FALSE);
            }
        }
        else {
            compile_expression(ctx, lhs, start_loc);
        }

        for (uint8_t i = expr->as.call.is_methodcall; i < args->length; i++) {
            compile_expression(ctx, args->elements[i], start_loc + i + 1);
        }

        emit_abc(ctx, BT_OP_CALL, result_loc, start_loc, args->length, BT_FALSE);

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_RECURSIVE_CALL: {
        bt_AstBuffer* args = &expr->as.call.args;

        push_registers(ctx);

        uint8_t start_loc = get_registers(ctx, args->length);

        for (uint8_t i = expr->as.call.is_methodcall; i < args->length; i++) {
            compile_expression(ctx, args->elements[i], start_loc + i);
        }

        emit_abc(ctx, BT_OP_REC_CALL, result_loc, start_loc, args->length - 1, BT_FALSE);

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_UNARY_OP: {
        push_registers(ctx);
        bt_AstNode* operand = expr->as.unary_op.operand;

        uint8_t operand_loc = find_binding_or_compile_temp(ctx, operand);

        switch (expr->source->type) {
        case BT_TOKEN_QUESTION:
            emit_ab(ctx, BT_OP_EXISTS, result_loc, operand_loc, BT_FALSE);
            break;
        case BT_TOKEN_BANG:
            emit_ab(ctx, BT_OP_EXPECT, result_loc, operand_loc, BT_FALSE);
            break;
        case BT_TOKEN_MINUS:
            emit_ab(ctx, BT_OP_NEG, result_loc, operand_loc, expr->as.unary_op.accelerated && ctx->compiler->options.accelerate_arithmetic);
            break;
        case BT_TOKEN_NOT: 
            emit_ab(ctx, BT_OP_NOT, result_loc, operand_loc, BT_FALSE);
            break;
#ifdef BT_DEBUG
        default: assert(0 && "Unimplemented unary operator!");
#endif
        }

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_BINARY_OP: {
        push_registers(ctx);

        bt_AstNode* lhs = expr->as.binary_op.left;
        bt_AstNode* rhs = expr->as.binary_op.right;

        uint8_t lhs_loc = find_binding_or_compile_loc(ctx, lhs, result_loc);

        uint8_t handled = 0;
        uint8_t test = 0;
        switch (expr->source->type) {
        case BT_TOKEN_AND:
            test = 1;
        case BT_TOKEN_OR:
            uint32_t instruction_idx = emit_aibc(ctx, BT_OP_TEST, result_loc, 0);
            uint8_t rhs_loc = find_binding_or_compile_loc(ctx, rhs, result_loc);

            bt_Op* test_op = op_at(ctx, instruction_idx);
            uint32_t jmp_loc = op_count(ctx);

            *test_op = BT_MAKE_OP_AIBC(BT_OP_TEST, result_loc, jmp_loc - instruction_idx - 1);
            if (!test) *test_op = BT_ACCELERATE_OP(*test_op);
            handled = 1;
            break;
        }

        if (handled) break;
            
        StorageClass storage = STORAGE_REGISTER;
        if (is_assigning(expr->source->type)) {
            storage = get_storage(ctx, lhs);
            if (storage == STORAGE_INVALID) {
                compile_error_token(ctx->compiler, "Lhs is not an assignable binding: '%.*s'", lhs->source);
            }
            else if (storage == STORAGE_REGISTER || storage == STORAGE_INDEX) {
                result_loc = lhs_loc;
            }
        }

        if (expr->source->type == BT_TOKEN_PERIOD) {
        hoist_fail:
            if (expr->as.binary_op.hoistable && ctx->compiler->options.allow_method_hoisting) {
                bt_StrSlice name = { expr->as.binary_op.from->name, (uint16_t)strlen(expr->as.binary_op.from->name) };
                bt_Value hoisted = bt_table_get(expr->as.binary_op.from->prototype_values, expr->as.binary_op.key);
                if (hoisted == BT_VALUE_NULL) {
                    expr->as.binary_op.hoistable = BT_FALSE;
                    goto hoist_fail;
                }
                uint8_t idx = push(ctx, hoisted);
                emit_ab(ctx, BT_OP_LOAD, result_loc, idx, BT_FALSE);
                goto try_store;
            }
            else if (expr->as.binary_op.accelerated && ctx->compiler->options.predict_hash_slots) {
                if (expr->as.binary_op.left->resulting_type->category != BT_TYPE_CATEGORY_ARRAY) {
                    uint8_t idx = push(ctx,
                        BT_VALUE_OBJECT(bt_make_string_hashed_len(ctx->context, rhs->source->source.source, rhs->source->source.length)));
                    
                    emit_abc(ctx, BT_OP_LOAD_IDX, result_loc, lhs_loc, expr->as.binary_op.idx, BT_TRUE);
                    emit_aibc(ctx, BT_OP_IDX_EXT, 0, idx);
                    goto try_store;
                }
            }
            else if (rhs->type == BT_AST_NODE_LITERAL && rhs->resulting_type == ctx->context->types.string && rhs->source->type == BT_TOKEN_IDENTIFER_LITERAL) {
                uint8_t idx = push(ctx,
                    BT_VALUE_OBJECT(bt_make_string_hashed_len(ctx->context, rhs->source->source.source, rhs->source->source.length)));

                bt_Value is_prototypical = get_from_proto(expr->as.binary_op.from, expr->as.binary_op.key);
                emit_abc(ctx, (is_prototypical == BT_VALUE_NULL || !ctx->compiler->options.predict_hash_slots) ? BT_OP_LOAD_IDX_K : BT_OP_LOAD_PROTO, result_loc, lhs_loc, idx, BT_FALSE);

                goto try_store;
            }
        }

        uint8_t rhs_loc = find_binding_or_compile_temp(ctx, rhs);

#define HOISTABLE_OP(unhoisted) \
        if (expr->as.binary_op.hoistable && ctx->compiler->options.allow_method_hoisting) {                                                                        \
            bt_StrSlice name = { expr->as.binary_op.from->name, (uint16_t)strlen(expr->as.binary_op.from->name) }; \
            bt_Value hoisted = bt_table_get(expr->as.binary_op.from->prototype_values, expr->as.binary_op.key);    \
            if(hoisted == BT_VALUE_NULL) goto unhoisted;                                                           \
            uint8_t idx = push(ctx, hoisted);                                                                      \
                                                                                                                   \
            if (lhs_loc != result_loc + 1 || rhs_loc != result_loc + 2) {                                          \
                push_registers(ctx);                                                                               \
                                                                                                                   \
                uint8_t fn_loc = get_registers(ctx, 3);                                                            \
                emit_ab(ctx, BT_OP_LOAD, fn_loc, idx, BT_FALSE);                                                   \
                emit_ab(ctx, BT_OP_MOVE, fn_loc + 1, lhs_loc, BT_FALSE);                                           \
                emit_ab(ctx, BT_OP_MOVE, fn_loc + 2, rhs_loc, BT_FALSE);                                           \
                emit_abc(ctx, BT_OP_CALL, result_loc, fn_loc, 2, BT_FALSE);                                        \
                                                                                                                   \
                restore_registers(ctx);                                                                            \
            }                                                                                                      \
            else {                                                                                                 \
                emit_ab(ctx, BT_OP_LOAD, result_loc, idx, BT_FALSE);                                               \
                emit_abc(ctx, BT_OP_CALL, result_loc, result_loc, 2, BT_FALSE);                                    \
            }                                                                                                      \
        }                                                                                                        

        switch (expr->source->type) {
        case BT_TOKEN_PLUS:
        case BT_TOKEN_PLUSEQ:
            HOISTABLE_OP(unhoist_add)
            else { unhoist_add: emit_abc(ctx, BT_OP_ADD, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic); }
            break;
        case BT_TOKEN_MINUS:
        case BT_TOKEN_MINUSEQ:
            HOISTABLE_OP(unhoist_sub)
            else { unhoist_sub: emit_abc(ctx, BT_OP_SUB, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic); }
            break;
        case BT_TOKEN_MUL:
        case BT_TOKEN_MULEQ:
            HOISTABLE_OP(unhoist_mul)
            else { unhoist_mul: emit_abc(ctx, BT_OP_MUL, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic); }
            break;
        case BT_TOKEN_DIV:
        case BT_TOKEN_DIVEQ:
            HOISTABLE_OP(unhoist_div)
            else { unhoist_div: emit_abc(ctx, BT_OP_DIV, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic); }
            break;
        case BT_TOKEN_NULLCOALESCE:
            emit_abc(ctx, BT_OP_COALESCE, result_loc, lhs_loc, rhs_loc, BT_FALSE);
            break;
        case BT_TOKEN_IS:
            emit_abc(ctx, BT_OP_TCHECK, result_loc, lhs_loc, rhs_loc, BT_FALSE);
            break;
        case BT_TOKEN_SATISFIES:
            emit_abc(ctx, BT_OP_TSATIS, result_loc, lhs_loc, rhs_loc, BT_FALSE);
            break;
        case BT_TOKEN_AS:
            emit_abc(ctx, BT_OP_TCAST, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated);
            break;
        case BT_TOKEN_COMPOSE:
            emit_abc(ctx, BT_OP_COMPOSE, result_loc, lhs_loc, rhs_loc, BT_FALSE);
            break;
        case BT_TOKEN_PERIOD:
            if (expr->as.binary_op.accelerated && expr->as.binary_op.left->resulting_type->category == BT_TYPE_CATEGORY_ARRAY && ctx->compiler->options.typed_array_subscript) {
                emit_abc(ctx, BT_OP_LOAD_SUB_F, result_loc, lhs_loc, rhs_loc, BT_FALSE);
            } else emit_abc(ctx, BT_OP_LOAD_IDX, result_loc, lhs_loc, rhs_loc, BT_FALSE);
            break;
        case BT_TOKEN_EQUALS:
            emit_abc(ctx, BT_OP_EQ, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic);
            break;
        case BT_TOKEN_NOTEQ:
            emit_abc(ctx, BT_OP_NEQ, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic);
            break;
        case BT_TOKEN_LT:
            emit_abc(ctx, BT_OP_LT, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic);
            break;
        case BT_TOKEN_LTE:
            emit_abc(ctx, BT_OP_LTE, result_loc, lhs_loc, rhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic);
            break;
        case BT_TOKEN_GT:
            emit_abc(ctx, BT_OP_LT, result_loc, rhs_loc, lhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic);
            break;
        case BT_TOKEN_GTE:
            emit_abc(ctx, BT_OP_LTE, result_loc, rhs_loc, lhs_loc, expr->as.binary_op.accelerated && ctx->compiler->options.accelerate_arithmetic);
            break;
        case BT_TOKEN_ASSIGN:
            emit_ab(ctx, BT_OP_MOVE, result_loc, rhs_loc, BT_FALSE);
            break;
#ifdef BT_DEBUG
        default: assert(0 && "Unimplemented binary operator!");
#endif
        }

    try_store:
        if (storage == STORAGE_UPVAL) {
            uint8_t upval_idx = find_upval(ctx, lhs->source->source);
            emit_ab(ctx, BT_OP_STOREUP, upval_idx, result_loc, BT_FALSE);
        }
        else if (storage == STORAGE_INDEX) {
            push_registers(ctx);
            uint8_t tbl_loc = find_binding_or_compile_temp(ctx, lhs->as.binary_op.left);

            if (lhs->as.binary_op.accelerated) {
                if (lhs->as.binary_op.left->resulting_type->category == BT_TYPE_CATEGORY_ARRAY) {
                    if (!ctx->compiler->options.typed_array_subscript) goto failed_array;

                    uint8_t idx_loc = find_binding_or_compile_temp(ctx, lhs->as.binary_op.right);
                    emit_abc(ctx, BT_OP_STORE_SUB_F, tbl_loc, idx_loc, result_loc, BT_FALSE);
                }
                else if (ctx->compiler->options.predict_hash_slots)
                {
                    bt_Token* source = lhs->as.binary_op.right->source;
                    uint8_t idx = push(ctx,
                        BT_VALUE_OBJECT(bt_make_string_hashed_len(ctx->context, source->source.source, source->source.length)));
                    
                    emit_abc(ctx, BT_OP_STORE_IDX, tbl_loc, lhs->as.binary_op.idx, result_loc, BT_TRUE);
                    emit_aibc(ctx, BT_OP_IDX_EXT, 0, idx);
                }
                else goto failed_fast;
                goto stored_fast;
            }
            else if (lhs->as.binary_op.right->type == BT_AST_NODE_LITERAL && lhs->as.binary_op.right->resulting_type == ctx->context->types.string &&
                lhs->as.binary_op.right->source->type == BT_TOKEN_IDENTIFER_LITERAL) {
            failed_fast:;
                bt_Token* source = lhs->as.binary_op.right->source;
                uint8_t idx = push(ctx,
                    BT_VALUE_OBJECT(bt_make_string_hashed_len(ctx->context, source->source.source, source->source.length)));
                emit_abc(ctx, BT_OP_STORE_IDX_K, tbl_loc, idx, result_loc, BT_FALSE);
                goto stored_fast;
            }

        failed_array:;
            uint8_t idx_loc = find_binding_or_compile_temp(ctx, lhs->as.binary_op.right);
            emit_abc(ctx, BT_OP_STORE_IDX, tbl_loc, idx_loc, result_loc, BT_FALSE);
        stored_fast:
            restore_registers(ctx);
        }

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_FUNCTION: {
        bt_Fn* fn = compile_fn(ctx->compiler, ctx, expr);
        load_fn(ctx, expr, fn, result_loc);
    } break;
    case BT_AST_NODE_METHOD: {
        push_registers(ctx);
        bt_Fn* fn = compile_fn(ctx->compiler, ctx, expr->as.method.fn);
        uint8_t type_idx = push_load(ctx, BT_VALUE_OBJECT(expr->as.method.containing_type));
        uint8_t name_idx = push_load(ctx, BT_VALUE_OBJECT(expr->as.method.name));
        load_fn(ctx, expr->as.method.fn, fn, result_loc);
        emit_abc(ctx, BT_OP_TSET, type_idx, name_idx, result_loc, BT_FALSE);
        restore_registers(ctx);
    } break;
    case BT_AST_NODE_TABLE: {
        push_registers(ctx);

        bt_AstBuffer* fields = &expr->as.table.fields;
        bt_Type* resulting = expr->resulting_type;

        if (expr->as.table.typed) {
            uint8_t t_idx = push(ctx, BT_VALUE_OBJECT(expr->resulting_type));

            push_registers(ctx);
            uint8_t t_loc = get_register(ctx);
            emit_ab(ctx, BT_OP_LOAD, t_loc, t_idx, BT_FALSE);
            emit_abc(ctx, BT_OP_TABLE, result_loc, fields->length, t_loc, BT_TRUE);
            restore_registers(ctx);

            table_ensure_template_made(ctx->context, resulting);
        }
        else {
            emit_aibc(ctx, BT_OP_TABLE, result_loc, fields->length);
        }

        uint8_t val_loc = get_register(ctx);

        for (uint32_t i = 0; i < fields->length; ++i) {
            bt_AstNode* entry = fields->elements[i];
            compile_expression(ctx, entry->as.table_field.value_expr, val_loc);

            if (expr->as.table.typed && ctx->compiler->options.predict_hash_slots) {
                bt_Table* layout = resulting->as.table_shape.layout;
                int16_t idx = bt_table_get_idx(layout, entry->as.table_field.key);
                if (idx == -1 || idx > UINT8_MAX)
                    compile_error_token(ctx->compiler, "Failed to compile table literal", expr->source);
                uint8_t key_idx = push(ctx, entry->as.table_field.key);
                emit_abc(ctx, BT_OP_STORE_IDX, result_loc, (uint8_t)idx, val_loc, BT_TRUE);
                emit_aibc(ctx, BT_OP_IDX_EXT, 0, key_idx);
            }
            else {
                uint8_t key_idx = push(ctx, entry->as.table_field.key);
                emit_abc(ctx, BT_OP_STORE_IDX_K, result_loc, key_idx, val_loc, BT_FALSE);
            }
        }

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_ARRAY: {
        push_registers(ctx);
        bt_AstBuffer* items = &expr->as.arr.items;
        emit_aibc(ctx, BT_OP_ARRAY, result_loc, items->length);

        uint8_t idx_loc = get_register(ctx);
        uint8_t val_loc = get_register(ctx);
        
        uint8_t one_loc;
        if (items->length >= INT16_MAX) {
            one_loc = get_register(ctx);
            emit_aibc(ctx, BT_OP_LOAD_SMALL, one_loc, 1);
        }

        for (uint32_t i = 0; i < items->length; ++i) {
            bt_AstNode* entry = items->elements[i];

            if (i < INT16_MAX) {
                emit_aibc(ctx, BT_OP_LOAD_SMALL, idx_loc, i);
            }
            else {
                emit_abc(ctx, BT_OP_ADD, idx_loc, idx_loc, one_loc, ctx->compiler->options.accelerate_arithmetic);
            }
            compile_expression(ctx, entry, val_loc);

            emit_abc(ctx, BT_OP_STORE_IDX, result_loc, idx_loc, val_loc, BT_FALSE);
        }
        restore_registers(ctx);
    } break;
    case BT_AST_NODE_TYPE: {
        uint8_t type_idx = push(ctx, BT_VALUE_OBJECT(expr->resulting_type));
        emit_ab(ctx, BT_OP_LOAD, result_loc, type_idx, BT_FALSE);
    } break;
#ifdef BT_DEBUG
    default: assert(0);
#endif
    }

    if (ctx->compiler->options.generate_debug_info) {
        --ctx->compiler->debug_top;
    }
    return BT_TRUE;
}

static bt_bool compile_statement(FunctionContext* ctx, bt_AstNode* stmt);

static bt_bool compile_body(FunctionContext* ctx, bt_AstBuffer* body) 
{
    push_scope(ctx);
    for (uint32_t i = 0; i < body->length; ++i)
    {
        bt_AstNode* stmt = body->elements[i];
        if (!stmt) continue;

        if (stmt->type == BT_AST_NODE_CONTINUE) {
            if (ctx->loop_depth <= 0) {
                compile_error_token(ctx->compiler, "Cannot compile 'continue' - not inside loop", stmt->source);
            }

            emit_aibc(ctx, BT_OP_JMP, 0, ctx->loop_starts[ctx->loop_depth - 1] - ctx->output.length - 1);
        } else if (stmt->type == BT_AST_NODE_BREAK) {
            uint16_t break_loc = emit(ctx, BT_OP_JMP);
            ctx->pending_breaks[ctx->loop_depth - 1][ctx->break_counts[ctx->loop_depth - 1]++] = break_loc;
        }
        else if (!compile_statement(ctx, stmt)) { pop_scope(ctx); return BT_FALSE; }
    }
    pop_scope(ctx);

    return BT_TRUE;
}

static void setup_loop(FunctionContext* ctx, uint16_t loop_start)
{
    ctx->loop_starts[ctx->loop_depth] = loop_start;
    ctx->break_counts[ctx->loop_depth] = 0;
    ctx->loop_depth++;
}

static void resolve_breaks(FunctionContext* ctx)
{
    ctx->loop_depth--;
    for (uint8_t i = 0; i < ctx->break_counts[ctx->loop_depth]; i++) {
        uint16_t loc = ctx->pending_breaks[ctx->loop_depth][i];
        bt_Op* op = op_at(ctx, loc);
        BT_SET_IBC(*op, ctx->output.length - loc - 1);
    }
}

static bt_bool compile_statement(FunctionContext* ctx, bt_AstNode* stmt)
{
    if (ctx->compiler->options.generate_debug_info) {
        ctx->compiler->debug_stack[ctx->compiler->debug_top++] = stmt;
    }

    switch (stmt->type) {
    case BT_AST_NODE_LET: {
        uint8_t new_loc = make_binding(ctx, stmt->as.let.name);
        if (new_loc == INVALID_BINDING) compile_error_token(ctx->compiler, "Failed to make binding for '%.*s'", stmt->source);
        if (stmt->as.let.initializer) {
            if (ctx->compiler->options.generate_debug_info) {
                --ctx->compiler->debug_top;
            }
            return compile_expression(ctx, stmt->as.let.initializer, new_loc);
        }

        if (ctx->compiler->options.generate_debug_info) {
            --ctx->compiler->debug_top;
        }
        return BT_TRUE;
    } break;
    case BT_AST_NODE_RETURN: {
        if (stmt->as.ret.expr) {
            uint8_t ret_loc = find_binding_or_compile_temp(ctx, stmt->as.ret.expr);
            emit_a(ctx, BT_OP_RETURN, ret_loc);
        }
        else {
            emit(ctx, BT_OP_END);
        }
        
        if (ctx->compiler->options.generate_debug_info) {
            --ctx->compiler->debug_top;
        }
        return BT_TRUE;
    } break;
    case BT_AST_NODE_EXPORT: {
        push_registers(ctx);

        uint8_t type_lit = push(ctx, BT_VALUE_OBJECT(stmt->resulting_type));
        uint8_t name_lit = push(ctx,
            BT_VALUE_OBJECT(bt_make_string_hashed_len(ctx->context,
                stmt->as.exp.name.source,
                stmt->as.exp.name.length)));

        uint8_t type_loc = get_register(ctx);
        emit_ab(ctx, BT_OP_LOAD, type_loc, type_lit, BT_FALSE);

        uint8_t name_loc = get_register(ctx);
        emit_ab(ctx, BT_OP_LOAD, name_loc, name_lit, BT_FALSE);

        if (stmt->as.exp.value->type != BT_AST_NODE_IDENTIFIER) {
            compile_statement(ctx, stmt->as.exp.value);
        }

        if (stmt->as.exp.value->type == BT_AST_NODE_ALIAS) {
            uint8_t alias_loc = find_named(ctx, stmt->as.exp.name);
            if (alias_loc == INVALID_BINDING) {
                compile_error_token(ctx->compiler, "Failed to find identifer '%.*s' for export", stmt->source);
            }

            uint8_t export_loc = get_register(ctx);
            emit_ab(ctx, BT_OP_LOAD, export_loc, alias_loc, BT_FALSE);
            emit_abc(ctx, BT_OP_EXPORT, name_loc, export_loc, type_loc, BT_FALSE);
        }
        else {
            uint8_t binding_loc = find_binding(ctx, stmt->as.exp.name);
            if (binding_loc == INVALID_BINDING) {
                uint8_t alias_loc = find_named(ctx, stmt->as.exp.name);
                if (alias_loc == INVALID_BINDING) {
                    compile_error_token(ctx->compiler, "Failed to find identifer '%.*s' for export", stmt->source);
                }

                binding_loc = get_register(ctx);
                emit_ab(ctx, BT_OP_LOAD, binding_loc, alias_loc, BT_FALSE);
            }


            emit_abc(ctx, BT_OP_EXPORT, name_loc, binding_loc, type_loc, BT_FALSE);
        }

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_IF: {
        uint32_t end_points[64];
        uint8_t end_top = 0;

        bt_AstNode* current = stmt;

        while (current) {
            push_registers(ctx);

            uint32_t jump_loc = 0;

            if (current->as.branch.is_let) {
                push_scope(ctx);
                uint8_t bind_loc = make_binding(ctx, current->as.branch.identifier->source);
                compile_expression(ctx, current->as.branch.condition, bind_loc);
                uint8_t test_loc = get_register(ctx);

                emit_ab(ctx, BT_OP_EXISTS, test_loc, bind_loc, BT_FALSE);
                jump_loc = emit_a(ctx, BT_OP_JMPF, test_loc);
            }
            else if (current->as.branch.condition) {
                uint8_t condition_loc = find_binding_or_compile_temp(ctx, current->as.branch.condition);
                jump_loc = emit_a(ctx, BT_OP_JMPF, condition_loc);
            }

            compile_body(ctx, &current->as.branch.body);
            if (current->as.branch.next) end_points[end_top++] = emit(ctx, BT_OP_JMP);
        
            if (current->as.branch.is_let) {
                pop_scope(ctx);
            }

            if (current->as.branch.condition) {
                bt_Op* jmpf = op_at(ctx, jump_loc);
                BT_SET_IBC(*jmpf, ctx->output.length - jump_loc - 1);
            }

            current = current->as.branch.next;
            restore_registers(ctx);
        }

        for (uint8_t i = 0; i < end_top; ++i) {
            bt_Op* jmp = op_at(ctx, end_points[i]);
            BT_SET_IBC(*jmp, ctx->output.length - end_points[i] - 1);
        }
    } break;
    case BT_AST_NODE_LOOP_ITERATOR: {
        push_registers(ctx);
        push_scope(ctx);

        uint8_t base_loc = get_registers(ctx, 2);
        uint8_t it_loc = make_binding_at_loc(ctx, stmt->as.loop_iterator.identifier->source->source, base_loc);
        uint8_t closure_loc = base_loc + 1;
        compile_expression(ctx, stmt->as.loop_iterator.iterator, closure_loc);

        uint32_t loop_start = ctx->output.length;
        uint32_t skip_loc = emit_aibc(ctx, BT_OP_ITERFOR, base_loc, 0);

        setup_loop(ctx, loop_start);

        compile_body(ctx, &stmt->as.loop_iterator.body);

        emit_aibc(ctx, BT_OP_JMP, 0, loop_start - ctx->output.length - 1);
        bt_Op* skip_op = op_at(ctx, skip_loc);
        BT_SET_IBC(*skip_op, ctx->output.length - skip_loc - 1);

        resolve_breaks(ctx);
        
        pop_scope(ctx);
        restore_registers(ctx);
    } break;
    case BT_AST_NODE_LOOP_NUMERIC: {
        push_registers(ctx);
        push_scope(ctx);

        uint8_t base_loc = get_registers(ctx, 3);

        uint8_t it_loc = make_binding_at_loc(ctx, stmt->as.loop_numeric.identifier->source->source, base_loc);
        uint8_t step_loc = base_loc + 1;
        uint8_t stop_loc = base_loc + 2;

        compile_expression(ctx, stmt->as.loop_numeric.start, it_loc);
        compile_expression(ctx, stmt->as.loop_numeric.step, step_loc);
        compile_expression(ctx, stmt->as.loop_numeric.stop, stop_loc);
        emit_aibc(ctx, BT_OP_JMP, 0, 1);

        uint32_t loop_start = ctx->output.length;
        uint32_t skip_loc = emit_aibc(ctx, BT_OP_NUMFOR, it_loc, 0);

        setup_loop(ctx, loop_start);

        compile_body(ctx, &stmt->as.loop_iterator.body);

        emit_aibc(ctx, BT_OP_JMP, 0, loop_start - ctx->output.length - 1);
        bt_Op* skip_op = op_at(ctx, skip_loc);
        BT_SET_IBC(*skip_op, ctx->output.length - skip_loc - 1);

        resolve_breaks(ctx);

        pop_scope(ctx);
        restore_registers(ctx);
    } break;
    case BT_AST_NODE_LOOP_WHILE: {
        push_registers(ctx);
        push_scope(ctx);

        uint8_t condition_loc = get_register(ctx);

        uint32_t loop_start = ctx->output.length;
        compile_expression(ctx, stmt->as.loop_while.condition, condition_loc);
        uint32_t skip_loc = emit_aibc(ctx, BT_OP_JMPF, condition_loc, 0);

        setup_loop(ctx, loop_start);

        compile_body(ctx, &stmt->as.loop_while.body);

        emit_aibc(ctx, BT_OP_JMP, 0, loop_start - ctx->output.length - 1);
        bt_Op* skip_op = op_at(ctx, skip_loc);
        BT_SET_IBC(*skip_op, ctx->output.length - skip_loc - 1);

        resolve_breaks(ctx);
        pop_scope(ctx);
        restore_registers(ctx);
    } break;
    case BT_AST_NODE_ALIAS: {
        push_named(ctx, stmt->source->source, BT_VALUE_OBJECT(stmt->as.alias.type));
    } break;
    default:
        push_registers(ctx);
        bt_bool result = compile_expression(ctx, stmt, get_register(ctx));
        restore_registers(ctx);
        if (ctx->compiler->options.generate_debug_info) {
            --ctx->compiler->debug_top;
        }
        return result;
    }

    if (ctx->compiler->options.generate_debug_info) {
        --ctx->compiler->debug_top;
    }
    return BT_TRUE;
}

bt_Module* bt_compile(bt_Compiler* compiler)
{
    bt_AstBuffer* body = &compiler->input->root->as.module.body;
    bt_ImportBuffer* imports = &compiler->input->root->as.module.imports;

    FunctionContext fn = {0};
    fn.context = compiler->context;
    fn.compiler = compiler;

    push_scope(&fn);

    bt_Module* result = bt_make_module(compiler->context, imports);
    fn.module = result;

    compile_body(&fn, body);

    emit(&fn, BT_OP_END);
    
    if (compiler->has_errored) {
        return NULL;
    }

    if (compiler->options.generate_debug_info) {
        bt_module_set_debug_info(result, compiler->input->tokenizer);
        result->debug_locs = compiler->context->alloc(sizeof(bt_DebugLocBuffer));
        bt_buffer_move(result->debug_locs, &fn.debug);
    }

    bt_ValueBuffer fn_constants;
    bt_buffer_with_capacity(&fn_constants, compiler->context, fn.constants.length);
    for (uint32_t i = 0; i < fn.constants.length; ++i) {
        bt_buffer_push(compiler->context, &fn_constants, fn.constants.elements[i].value);
    }

    result->stack_size = fn.min_top_register;
    bt_buffer_clone(compiler->context, &result->constants, &fn_constants);
    bt_buffer_clone(compiler->context, &result->instructions , &fn.output);

    bt_buffer_destroy(compiler->context, &fn_constants);
    bt_buffer_destroy(compiler->context, &fn.constants);
    bt_buffer_destroy(compiler->context, &fn.output);

    return result;
}

static bt_Fn* compile_fn(bt_Compiler* compiler, FunctionContext* parent, bt_AstNode* fn) 
{
    FunctionContext ctx = {0};
    ctx.context = compiler->context;
    ctx.compiler = compiler;
    ctx.outer = parent;
    ctx.fn = fn;

    push_scope(&ctx);

    bt_ArgBuffer* args = &fn->as.fn.args;
    for (uint8_t i = 0; i < args->length; i++) {
        bt_FnArg* arg = args->elements + i;
        make_binding(&ctx, arg->name);
    }

    bt_AstBuffer* body = &fn->as.fn.body;
    compile_body(&ctx, body);

    if (!fn->as.fn.ret_type) {
        emit(&ctx, BT_OP_END);
    }

    bt_Module* mod = find_module(&ctx);

    bt_ValueBuffer fn_constants;
    bt_buffer_with_capacity(&fn_constants, compiler->context, ctx.constants.length);
    for (uint32_t i = 0; i < ctx.constants.length; ++i) {
        bt_buffer_push(compiler->context, &fn_constants, ctx.constants.elements[i].value);
    }
    
    bt_Fn* result = bt_make_fn(compiler->context, mod, fn->resulting_type, &fn_constants, &ctx.output, ctx.min_top_register);
    
    if (compiler->options.generate_debug_info) {
        result->debug = compiler->context->alloc(sizeof(bt_DebugLocBuffer));
        bt_buffer_move(result->debug, &ctx.debug);
    }

    bt_buffer_destroy(compiler->context, &fn_constants);
    bt_buffer_destroy(compiler->context, &ctx.constants);
    bt_buffer_destroy(compiler->context, &ctx.output);

    return result;
}