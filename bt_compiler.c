#include "bt_compiler.h"
#include "bt_value.h"

#include <stdio.h>
#include <assert.h>

#include "bt_intrinsics.h"
#include "bt_context.h"

static const uint8_t INVALID_BINDING = 255;

// Stores the occupancy of each register at a given time, each being 256 bits / 32 bytes
typedef struct RegisterState {
    uint64_t regs[4];
} RegisterState;

typedef struct CompilerBinding {
    bt_StrSlice name;
    uint8_t loc;
} CompilerBinding;

typedef struct BlockContext {
    CompilerBinding bindnings[128];
    uint8_t binding_top;

    RegisterState registers;
    uint8_t register_top;

    RegisterState temps[64];
    uint8_t temp_top;

    struct FunctionContext* fn;
} BlockContext;

typedef struct FunctionContext {
    uint8_t min_top_register;

    BlockContext blocks[16];
    uint8_t block_top;
} FunctionContext;

static uint8_t get_register(BlockContext* ctx);

static uint8_t make_binding(BlockContext* ctx, bt_StrSlice name) 
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

static uint8_t find_binding(BlockContext* ctx, bt_StrSlice name)
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

    result.output = BT_BUFFER_NEW(result.context, bt_Op);
    result.constants = BT_BUFFER_NEW(result.context, bt_Value);

    return result;
}

void bt_close_compiler(bt_Compiler* compiler)
{
    bt_buffer_destroy(compiler->context, &compiler->output);
    bt_buffer_destroy(compiler->context, &compiler->constants);
}

static void emit_abc(bt_Compiler* compiler, bt_OpCode code, uint8_t a, uint8_t b, uint8_t c)
{
    bt_Op op;
    op.op = code;
    op.a = a;
    op.b = b;
    op.c = c;

    bt_buffer_push(compiler->context, &compiler->output, &op);
}

static void emit_ab(bt_Compiler* compiler, bt_OpCode code, uint8_t a, uint8_t b)
{
    emit_abc(compiler, code, a, b, 0);
}

static void emit_a(bt_Compiler* compiler, bt_OpCode code, uint8_t a)
{
    emit_abc(compiler, code, a, 0, 0);
}

static uint8_t push(bt_Compiler* compiler, bt_Value value)
{
    for (uint8_t idx = 0; idx < compiler->constants.length; idx++)
    {
        bt_Value constant = *(bt_Value*)bt_buffer_at(&compiler->constants, idx);
        if (constant == value) return idx;
    }

    bt_buffer_push(compiler->context, &compiler->constants, &value);
    return compiler->constants.length - 1;
}

static uint8_t get_register(BlockContext* ctx)
{
    uint8_t offset = 0;
    for (uint8_t idx = 0; idx < 4; ++idx, offset += 64) {
        uint64_t mask = ctx->registers.regs[idx];
        if (mask == UINT64_MAX) continue;
        uint8_t found = ffsll(~mask);
        mask |= (1ull << (found - 1));
        ctx->registers.regs[idx] = mask;

        uint8_t result = offset + found;
        if (result > ctx->register_top) ctx->register_top = result;
        return result - 1;
    }

    return UINT8_MAX;
}

static void push_registers(BlockContext* ctx)
{
    ctx->temps[ctx->temp_top++] = ctx->registers;
}

static void restore_registers(BlockContext* ctx)
{
    ctx->registers = ctx->temps[--ctx->temp_top];
}

static bt_bool compile_expression(bt_Compiler* compiler, BlockContext* ctx, bt_AstNode* expr, uint8_t result_loc)
{
    switch (expr->type) {
    case BT_AST_NODE_LITERAL: {
        bt_Token* inner = expr->source;
        switch (inner->type) {
        case BT_TOKEN_TRUE_LITERAL:
            emit_ab(compiler, BT_OP_LOAD_BOOL, result_loc, 1);
            break;
        case BT_TOKEN_FALSE_LITERAL:
            emit_ab(compiler, BT_OP_LOAD_BOOL, result_loc, 0);
            break;
        case BT_TOKEN_NULL_LITERAL:
            emit_a(compiler, BT_OP_LOAD_NULL, result_loc);
            break;
        case BT_TOKEN_NUMBER_LITERAL: {
            bt_Literal* lit = (bt_Literal*)bt_buffer_at(&compiler->input->tokenizer->literals, inner->idx);
            uint8_t idx = push(compiler, BT_VALUE_NUMBER(lit->as_num));
            emit_ab(compiler, BT_OP_LOAD, result_loc, idx);
            // TODO - small constant optimisation
        } break;
        case BT_TOKEN_STRING_LITERAL: {
            assert(0); // implement context string hashing
        } break;
        }
    } break;
    case BT_AST_NODE_IDENTIFIER: { // simple copy
        uint8_t loc = find_binding(ctx, expr->source->source);
        if (loc == INVALID_BINDING) assert(0);
        emit_ab(compiler, BT_OP_MOVE, result_loc, loc);
    } break;
    case BT_AST_NODE_BINARY_OP: {
        push_registers(ctx);

        bt_AstNode* lhs = expr->as.binary_op.left;
        bt_AstNode* rhs = expr->as.binary_op.right;

        uint8_t lhs_loc = result_loc;
        if (lhs->type == BT_AST_NODE_IDENTIFIER) {
            lhs_loc = find_binding(ctx, lhs->source->source);
            if (lhs_loc == INVALID_BINDING) assert(0);
        }
        else {
            if (!compile_expression(compiler, ctx, expr->as.binary_op.left, lhs_loc)) assert(0);
        }

        uint8_t rhs_loc = INVALID_BINDING;
        
        if (rhs->type == BT_AST_NODE_IDENTIFIER) {
            rhs_loc = find_binding(ctx, rhs->source->source);
            if (rhs_loc == INVALID_BINDING) assert(0);
        }
        else {
            rhs_loc = get_register(ctx);
            if (!compile_expression(compiler, ctx, expr->as.binary_op.right, rhs_loc)) assert(0);
        }

        switch (expr->source->type) {
        case BT_TOKEN_PLUS:
            emit_abc(compiler, BT_OP_ADD, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_MINUS:
            emit_abc(compiler, BT_OP_SUB, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_MUL:
            emit_abc(compiler, BT_OP_MUL, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_DIV:
            emit_abc(compiler, BT_OP_DIV, result_loc, lhs_loc, rhs_loc);
            break;
        default: assert(0);
        }

        restore_registers(ctx);
    } break;
    default: assert(0);
    }

    return BT_TRUE;
}

static bt_bool compile_statement(bt_Compiler* compiler, BlockContext* ctx, bt_AstNode* stmt)
{
    switch (stmt->type) {
    case BT_AST_NODE_LET: {
        uint8_t new_loc = make_binding(ctx, stmt->as.let.name);
        if (new_loc == INVALID_BINDING) assert(0);
        if (stmt->as.let.initializer) {
            return compile_expression(compiler, ctx, stmt->as.let.initializer, new_loc);
        }

        return BT_TRUE;
    } break;
    default:
        return compile_expression(compiler, ctx, stmt, get_register(ctx));
    }

    return BT_TRUE;
}

bt_bool bt_compile(bt_Compiler* compiler)
{
    bt_Buffer* body = &compiler->input->root->as.module.body;

    FunctionContext* fn = compiler->context->alloc(sizeof(FunctionContext));
    fn->min_top_register = 0;
    fn->block_top = 0;
    fn->blocks[0].fn = fn;
    fn->blocks[0].registers.regs[0] = 0;
    fn->blocks[0].registers.regs[1] = 0;
    fn->blocks[0].registers.regs[2] = 0;
    fn->blocks[0].registers.regs[3] = 0;
    fn->blocks[0].temp_top = 0;
    fn->blocks[0].register_top = 0;
    fn->blocks[0].binding_top = 0;

    for (uint32_t i = 0; i < body->length; ++i)
    {
        bt_AstNode* stmt = *(bt_AstNode**)bt_buffer_at(body, i);
        compile_statement(compiler, &fn->blocks[0], stmt);
    }

    return BT_TRUE;
}
