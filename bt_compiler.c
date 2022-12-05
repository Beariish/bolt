#include "bt_compiler.h"
#include "bt_value.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>

#include "bt_intrinsics.h"
#include "bt_context.h"
#include "bt_debug.h"

static const uint8_t INVALID_BINDING = 255;

// Stores the occupancy of each register at a given time, each being 256 bits / 32 bytes
typedef struct RegisterState {
    uint64_t regs[4];
} RegisterState;

typedef struct CompilerBinding {
    bt_StrSlice name;
    uint8_t loc;
} CompilerBinding;

typedef struct FunctionContext {
    uint8_t min_top_register;

    CompilerBinding bindnings[128];
    uint8_t binding_top;

    RegisterState registers;

    RegisterState temps[32];
    uint8_t temp_top;

    bt_Buffer constants;
    bt_Buffer output;

    bt_Compiler* compiler;
    bt_Context* context;
} FunctionContext;

static uint8_t get_register(FunctionContext* ctx);
static uint8_t get_registers(FunctionContext* ctx, uint8_t count);
static bt_Fn* compile_fn(bt_Compiler* compiler, bt_AstNode* fn);

static uint8_t make_binding(FunctionContext* ctx, bt_StrSlice name) 
{
    for (uint32_t i = 0; i < ctx->binding_top; ++i) {
        CompilerBinding* binding = ctx->bindnings + i;
        if (bt_strslice_compare(binding->name, name)) assert(0);
    }

    CompilerBinding new_binding;
    new_binding.loc = get_register(ctx);
    new_binding.name = name;

    ctx->bindnings[ctx->binding_top++] = new_binding;
    return new_binding.loc;
}

static uint8_t find_binding(FunctionContext* ctx, bt_StrSlice name)
{
    for (uint32_t i = 0; i < ctx->binding_top; ++i) {
        CompilerBinding* binding = ctx->bindnings + i;
        if (bt_strslice_compare(binding->name, name)) return binding->loc;
    }

    return INVALID_BINDING;
}

bt_Compiler bt_open_compiler(bt_Parser* parser)
{
    bt_Compiler result;
    result.context = parser->context;
    result.input = parser;

    return result;
}

void bt_close_compiler(bt_Compiler* compiler)
{
}

static void emit_abc(FunctionContext* ctx, bt_OpCode code, uint8_t a, uint8_t b, uint8_t c)
{
    bt_Op op;
    op.op = code;
    op.a = a;
    op.b = b;
    op.c = c;

    bt_buffer_push(ctx->context, &ctx->output, &op);
}

static void emit_aibc(FunctionContext* ctx, bt_OpCode code, uint8_t a, int16_t ibc)
{
    bt_Op op;
    op.op = code;
    op.a = a;
    op.ibc = ibc;

    bt_buffer_push(ctx->context, &ctx->output, &op);
}

static void emit_ab(FunctionContext* ctx, bt_OpCode code, uint8_t a, uint8_t b)
{
    emit_abc(ctx, code, a, b, 0);
}

static void emit_a(FunctionContext* ctx, bt_OpCode code, uint8_t a)
{
    emit_abc(ctx, code, a, 0, 0);
}

static void emit(FunctionContext* ctx, bt_OpCode code)
{
    emit_abc(ctx, code, 0, 0, 0);
}

static uint8_t push(FunctionContext* ctx, bt_Value value)
{
    for (uint8_t idx = 0; idx < ctx->constants.length; idx++)
    {
        bt_Value constant = *(bt_Value*)bt_buffer_at(&ctx->constants, idx);
        if (constant == value) return idx;
    }

    bt_buffer_push(ctx->context, &ctx->constants, &value);
    return ctx->constants.length - 1;
}

static uint8_t get_register(FunctionContext* ctx)
{
    uint8_t offset = 0;
    for (uint8_t idx = 0; idx < 4; ++idx, offset += 64) {
        uint64_t mask = ctx->registers.regs[idx];
        if (mask == UINT64_MAX) continue;
        uint8_t found = ffsll(~mask);
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
    uint32_t search_mask = 0;
    for (uint8_t i = 0; i < count; ++i) {
        search_mask |= 1 << i;
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
    if (expr->type == BT_AST_NODE_IDENTIFIER) {
        loc = find_binding(ctx, expr->source->source);
        if (loc == INVALID_BINDING) assert(0 && "Compiler error: Can't find identifier!");
    }
    else {
        loc = get_register(ctx);
        if (!compile_expression(ctx, expr, loc)) assert(0 && "Compiler error: Failed to compile operand.");
    }

    return loc;
}

static uint8_t find_binding_or_compile_loc(FunctionContext* ctx, bt_AstNode* expr, uint8_t backup_loc)
{
    uint8_t loc = INVALID_BINDING;
    if (expr->type == BT_AST_NODE_IDENTIFIER) {
        loc = find_binding(ctx, expr->source->source);
        if (loc == INVALID_BINDING) assert(0 && "Compiler error: Can't find identifier!");
    }
    else {
        loc = backup_loc;
        if (!compile_expression(ctx, expr, loc)) assert(0 && "Compiler error: Failed to compile operand.");
    }

    return loc;
}

static bt_bool compile_expression(FunctionContext* ctx, bt_AstNode* expr, uint8_t result_loc)
{
    switch (expr->type) {
    case BT_AST_NODE_LITERAL: {
        bt_Token* inner = expr->source;
        switch (inner->type) {
        case BT_TOKEN_TRUE_LITERAL:
            emit_ab(ctx, BT_OP_LOAD_BOOL, result_loc, 1);
            break;
        case BT_TOKEN_FALSE_LITERAL:
            emit_ab(ctx, BT_OP_LOAD_BOOL, result_loc, 0);
            break;
        case BT_TOKEN_NULL_LITERAL:
            emit_a(ctx, BT_OP_LOAD_NULL, result_loc);
            break;
        case BT_TOKEN_NUMBER_LITERAL: {
            bt_Literal* lit = (bt_Literal*)bt_buffer_at(&ctx->compiler->input->tokenizer->literals, inner->idx);
            bt_number num = lit->as_num;

            if (floor(num) == num && num < (bt_number)INT16_MAX && num >(bt_number)INT16_MIN) {
                emit_aibc(ctx, BT_OP_LOAD_SMALL, result_loc, (int16_t)num);
            }
            else {
                uint8_t idx = push(ctx, BT_VALUE_NUMBER(lit->as_num));
                emit_ab(ctx, BT_OP_LOAD, result_loc, idx);
            }
        } break;
        case BT_TOKEN_STRING_LITERAL: {
            bt_Literal* lit = (bt_Literal*)bt_buffer_at(&ctx->compiler->input->tokenizer->literals, inner->idx);
            uint8_t idx = push(ctx,
                BT_VALUE_STRING(bt_make_string_hashed_len(ctx->context, lit->as_str.source, lit->as_str.length)));
            emit_ab(ctx, BT_OP_LOAD, result_loc, idx);
        } break;
        }
    } break;
    case BT_AST_NODE_IDENTIFIER: { // simple copy
        uint8_t loc = find_binding(ctx, expr->source->source);
        if (loc == INVALID_BINDING) assert(0);
        emit_ab(ctx, BT_OP_MOVE, result_loc, loc);
    } break;
    case BT_AST_NODE_CALL: {
        bt_AstNode* lhs = expr->as.call.fn;
        bt_Buffer* args = &expr->as.call.args;

        push_registers(ctx);

        uint8_t start_loc = get_registers(ctx, args->length + 1);

        compile_expression(ctx, lhs, start_loc);
        for (uint8_t i = 0; i < args->length; i++) {
            compile_expression(ctx, *(bt_AstNode**)bt_buffer_at(args, i), start_loc + i + 1);
        }

        emit_abc(ctx, BT_OP_CALL, result_loc, start_loc, args->length);

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_UNARY_OP: {
        push_registers(ctx);
        bt_AstNode* operand = expr->as.unary_op.operand;

        uint8_t operand_loc = find_binding_or_compile_temp(ctx, operand);

        switch (expr->source->type) {
        case BT_TOKEN_QUESTION:
            emit_ab(ctx, BT_OP_EXISTS, result_loc, operand_loc);
            break;
        case BT_TOKEN_MINUS:
            emit_ab(ctx, BT_OP_NEG, result_loc, operand_loc);
            break;
        default: assert(0 && "Unimplemented unary operator!");
        }

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_BINARY_OP: {
        push_registers(ctx);

        bt_AstNode* lhs = expr->as.binary_op.left;
        bt_AstNode* rhs = expr->as.binary_op.right;

        uint8_t lhs_loc = find_binding_or_compile_loc(ctx, lhs, result_loc);
        uint8_t rhs_loc = find_binding_or_compile_temp(ctx, rhs);

        switch (expr->source->type) {
        case BT_TOKEN_PLUS:
            emit_abc(ctx, BT_OP_ADD, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_MINUS:
            emit_abc(ctx, BT_OP_SUB, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_MUL:
            emit_abc(ctx, BT_OP_MUL, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_DIV:
            emit_abc(ctx, BT_OP_DIV, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_AND:
            emit_abc(ctx, BT_OP_AND, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_OR:
            emit_abc(ctx, BT_OP_OR, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_NULLCOALESCE:
            emit_abc(ctx, BT_OP_COALESCE, result_loc, lhs_loc, rhs_loc);
            break;
        default: assert(0 && "Unimplemented binary operator!");
        }

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_FUNCTION: {
        bt_Fn* fn = compile_fn(ctx->compiler, expr);
        uint8_t idx = push(ctx, BT_VALUE_OBJECT(fn));
        emit_ab(ctx, BT_OP_LOAD, result_loc, idx);
    } break;
    default: assert(0);
    }

    return BT_TRUE;
}

static bt_bool compile_statement(FunctionContext* ctx, bt_AstNode* stmt)
{
    switch (stmt->type) {
    case BT_AST_NODE_LET: {
        uint8_t new_loc = make_binding(ctx, stmt->as.let.name);
        if (new_loc == INVALID_BINDING) assert(0);
        if (stmt->as.let.initializer) {
            return compile_expression(ctx, stmt->as.let.initializer, new_loc);
        }

        return BT_TRUE;
    } break;
    case BT_AST_NODE_RETURN: {
        uint8_t ret_loc = get_register(ctx);
        if(!compile_expression(ctx, stmt->as.ret.expr, ret_loc)) return BT_FALSE;
        emit_a(ctx, BT_OP_RETURN, ret_loc);
        return BT_TRUE;
    } break;
    default:
        return compile_expression(ctx, stmt, get_register(ctx));
    }

    return BT_TRUE;
}

bt_Module* bt_compile(bt_Compiler* compiler)
{
    bt_Buffer* body = &compiler->input->root->as.module.body;

    FunctionContext fn;
    fn.min_top_register = 0;
    fn.registers.regs[0] = 0;
    fn.registers.regs[1] = 0;
    fn.registers.regs[2] = 0;
    fn.registers.regs[3] = 0;
    fn.temp_top = 0;
    fn.binding_top = 0;
    fn.output = BT_BUFFER_NEW(compiler->context, bt_Op);
    fn.constants = BT_BUFFER_NEW(compiler->context, bt_Value);
    fn.context = compiler->context;
    fn.compiler = compiler;

    for (uint32_t i = 0; i < body->length; ++i)
    {
        bt_AstNode* stmt = *(bt_AstNode**)bt_buffer_at(body, i);
        compile_statement(&fn, stmt);
    }

    emit(&fn, BT_OP_HALT);

    bt_Module* result = bt_make_module(compiler->context, &fn.constants, &fn.output, fn.min_top_register);

    bt_buffer_destroy(compiler->context, &fn.constants);
    bt_buffer_destroy(compiler->context, &fn.output);

    return result;
}

static bt_Fn* compile_fn(bt_Compiler* compiler, bt_AstNode* fn) 
{
    FunctionContext ctx;
    ctx.min_top_register = 0;
    ctx.registers.regs[0] = 0;
    ctx.registers.regs[1] = 0;
    ctx.registers.regs[2] = 0;
    ctx.registers.regs[3] = 0;
    ctx.temp_top = 0;
    ctx.binding_top = 0;
    ctx.output = BT_BUFFER_NEW(compiler->context, bt_Op);
    ctx.constants = BT_BUFFER_NEW(compiler->context, bt_Value);
    ctx.context = compiler->context;
    ctx.compiler = compiler;

    bt_Buffer* args = &fn->as.fn.args;
    for (uint8_t i = 0; i < args->length; i++) {
        bt_FnArg* arg = bt_buffer_at(args, i);
        make_binding(&ctx, arg->name);
    }

    bt_Buffer* body = &fn->as.fn.body;
    for (uint32_t i = 0; i < body->length; ++i)
    {
        bt_AstNode* stmt = *(bt_AstNode**)bt_buffer_at(body, i);
        compile_statement(&ctx, stmt);
    }

    if (fn->as.fn.ret_type) {
        emit(&ctx, BT_OP_HALT);
    }
    else {
        emit(&ctx, BT_OP_RETURN);
    }

    bt_Fn* result = bt_make_fn(compiler->context, fn->resulting_type, &ctx.constants, &ctx.output, ctx.min_top_register);

    bt_debug_print_fn(result);
    printf("-----------------------------------------------------\n");

    bt_buffer_destroy(compiler->context, &ctx.constants);
    bt_buffer_destroy(compiler->context, &ctx.output);

    return result;
}