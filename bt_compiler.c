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

typedef enum StorageClass {
    STORAGE_INVALID,
    STORAGE_REGISTER,
    STORAGE_UPVAL
} StorageClass;

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

    bt_Module* module;
    bt_AstNode* fn;

    struct FunctionContext* outer;
} FunctionContext;

static uint8_t get_register(FunctionContext* ctx);
static uint8_t get_registers(FunctionContext* ctx, uint8_t count);
static bt_Fn* compile_fn(bt_Compiler* compiler, FunctionContext* parent, bt_AstNode* fn);

static bt_Module* find_module(FunctionContext* ctx)
{
    while (ctx) {
        if (ctx->module) return ctx->module;
        ctx = ctx->outer;
    }

    assert(0 && "Internal compiler error - function has no module context!");
    return NULL;
}

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

static uint8_t find_upval(FunctionContext* ctx, bt_StrSlice name)
{
    bt_AstNode* fn = ctx->fn;
    if (!fn) return INVALID_BINDING;

    for (uint32_t i = 0; i < fn->as.fn.upvals.length; i++) {
        bt_ParseBinding* bind = bt_buffer_at(&fn->as.fn.upvals, i);
        if (bt_strslice_compare(bind->name, name)) return i;
    }

    return INVALID_BINDING;
}

static uint16_t find_import(FunctionContext* ctx, bt_StrSlice name)
{
    bt_Module* mod = find_module(ctx);

    for (uint32_t i = 0; i < mod->imports.length; ++i) {
        bt_ModuleImport* import = *(bt_ModuleImport**)bt_buffer_at(&mod->imports, i);
        if (bt_strslice_compare(bt_as_strslice(import->name), name)) {
            return i;
        }
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

static bt_Op* op_at(FunctionContext* ctx, uint32_t idx)
{
    return bt_buffer_at(&ctx->output, idx);
}

static uint32_t emit_abc(FunctionContext* ctx, bt_OpCode code, uint8_t a, uint8_t b, uint8_t c)
{
    bt_Op op;
    op.op = code;
    op.a = a;
    op.b = b;
    op.c = c;

    bt_buffer_push(ctx->context, &ctx->output, &op);

    return ctx->output.length - 1;
}

static uint32_t emit_aibc(FunctionContext* ctx, bt_OpCode code, uint8_t a, int16_t ibc)
{
    bt_Op op;
    op.op = code;
    op.a = a;
    op.ibc = ibc;

    bt_buffer_push(ctx->context, &ctx->output, &op);

    return ctx->output.length - 1;
}

static uint32_t emit_ab(FunctionContext* ctx, bt_OpCode code, uint8_t a, uint8_t b)
{
    return emit_abc(ctx, code, a, b, 0);
}

static uint32_t emit_a(FunctionContext* ctx, bt_OpCode code, uint8_t a)
{
    return emit_abc(ctx, code, a, 0, 0);
}

static uint32_t emit(FunctionContext* ctx, bt_OpCode code)
{
    return emit_abc(ctx, code, 0, 0, 0);
}

static uint8_t push(FunctionContext* ctx, bt_Value value)
{
    for (uint8_t idx = 0; idx < ctx->constants.length; idx++)
    {
        bt_Value constant = *(bt_Value*)bt_buffer_at(&ctx->constants, idx);
        if (bt_value_is_equal(constant, value)) return idx;
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

    if(expr->type == BT_AST_NODE_IDENTIFIER) {
        loc = find_binding(ctx, expr->source->source);
    }

    if (loc == INVALID_BINDING) {
        loc = get_register(ctx);
        if (!compile_expression(ctx, expr, loc)) assert(0 && "Compiler error: Failed to compile operand.");
    }

    if (loc == INVALID_BINDING) {
        assert(0 && "Compiler error: Can't find identifier!");
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
    
    return STORAGE_INVALID;
}

static uint8_t find_binding_or_compile_loc(FunctionContext* ctx, bt_AstNode* expr, uint8_t backup_loc)
{
    uint8_t loc = INVALID_BINDING;
    loc = find_binding(ctx, expr->source->source);
    
    if (loc == INVALID_BINDING) {
        loc = backup_loc;
        if (!compile_expression(ctx, expr, loc)) assert(0 && "Compiler error: Failed to compile operand.");
    }

    if (loc == INVALID_BINDING) {
        assert(0 && "Compiler error: Can't find identifier!");
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
        case BT_TOKEN_IDENTIFER_LITERAL: {
            uint8_t idx = push(ctx,
                BT_VALUE_STRING(bt_make_string_hashed_len(ctx->context, expr->source->source.source, expr->source->source.length)));
            emit_ab(ctx, BT_OP_LOAD, result_loc, idx);
        } break;
        }
    } break;
    case BT_AST_NODE_IDENTIFIER: { // simple copy
        uint8_t loc = find_binding(ctx, expr->source->source);
        if (loc != INVALID_BINDING) {
            emit_ab(ctx, BT_OP_MOVE, result_loc, loc); 
            break;
        }

        loc = find_upval(ctx, expr->source->source);
        if (loc != INVALID_BINDING) {
            emit_ab(ctx, BT_OP_LOADUP, result_loc, loc);
            break;
        }
         
        assert(0 && "Cannot find identifier!");
    } break;
    case BT_AST_NODE_IMPORT_REFERENCE: {
        uint16_t loc = find_import(ctx, expr->source->source);
        if (loc == INVALID_BINDING) assert(0);
        emit_ab(ctx, BT_OP_LOAD_IMPORT, result_loc, loc);
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

        StorageClass storage = STORAGE_REGISTER;
        if (is_assigning(expr->source->type)) {
            storage = get_storage(ctx, lhs);
            if (storage == STORAGE_INVALID) {
                assert(0 && "Unassignable lhs!");
            }
            else if (storage == STORAGE_REGISTER) {
                result_loc = lhs_loc;
            }
        }

        switch (expr->source->type) {
        case BT_TOKEN_PLUS:
        case BT_TOKEN_PLUSEQ:
            emit_abc(ctx, BT_OP_ADD, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_MINUS:
        case BT_TOKEN_MINUSEQ:
            emit_abc(ctx, BT_OP_SUB, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_MUL:
        case BT_TOKEN_MULEQ:
            emit_abc(ctx, BT_OP_MUL, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_DIV:
        case BT_TOKEN_DIVEQ:
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
        case BT_TOKEN_PERIOD:
            emit_abc(ctx, BT_OP_LOAD_IDX, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_EQUALS:
            emit_abc(ctx, BT_OP_EQ, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_NOTEQ:
            emit_abc(ctx, BT_OP_NEQ, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_LT:
            emit_abc(ctx, BT_OP_LT, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_LTE:
            emit_abc(ctx, BT_OP_LTE, result_loc, lhs_loc, rhs_loc);
            break;
        case BT_TOKEN_GT:
            emit_abc(ctx, BT_OP_LT, result_loc, rhs_loc, lhs_loc);
            break;
        case BT_TOKEN_GTE:
            emit_abc(ctx, BT_OP_LTE, result_loc, rhs_loc, lhs_loc);
            break;
        case BT_TOKEN_ASSIGN:
            emit_ab(ctx, BT_OP_MOVE, result_loc, rhs_loc);
            break;
        default: assert(0 && "Unimplemented binary operator!");
        }

        if (storage == STORAGE_UPVAL) {
            uint8_t upval_idx = find_upval(ctx, lhs->source->source);
            emit_ab(ctx, BT_OP_STOREUP, upval_idx, result_loc);
        }

        restore_registers(ctx);
    } break;
    case BT_AST_NODE_FUNCTION: {
        bt_Fn* fn = compile_fn(ctx->compiler, ctx, expr);
        uint8_t idx = push(ctx, BT_VALUE_OBJECT(fn));

        if (expr->as.fn.upvals.length == 0) {
            emit_ab(ctx, BT_OP_LOAD, result_loc, idx);
        }
        else {
            uint8_t start = get_registers(ctx, expr->as.fn.upvals.length + 1);

            emit_ab(ctx, BT_OP_LOAD, start, idx);

            for (uint8_t i = 0; i < expr->as.fn.upvals.length; ++i) {
                bt_ParseBinding* binding = bt_buffer_at(&expr->as.fn.upvals, i);
                uint8_t loc = find_binding(ctx, binding->name);
                if (loc != INVALID_BINDING) {
                    emit_ab(ctx, BT_OP_MOVE, start + i + 1, loc);
                    continue;
                }

                loc = find_upval(ctx, binding->name);
                if (loc != INVALID_BINDING) {
                    emit_ab(ctx, BT_OP_LOADUP, start + i + 1, loc);
                    continue;
                }
                
                assert(0 && "Cannot find identifier!");
            }

            emit_abc(ctx, BT_OP_CLOSE, result_loc, start, expr->as.fn.upvals.length);
        }
    } break;
    default: assert(0);
    }

    return BT_TRUE;
}

static bt_bool compile_statement(FunctionContext* ctx, bt_AstNode* stmt);

static bt_bool compile_body(FunctionContext* ctx, bt_Buffer* body) 
{
    for (uint32_t i = 0; i < body->length; ++i)
    {
        bt_AstNode* stmt = *(bt_AstNode**)bt_buffer_at(body, i);
        if (!compile_statement(ctx, stmt)) return BT_FALSE;
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
        uint8_t ret_loc = find_binding_or_compile_temp(ctx, stmt->as.ret.expr);
        emit_a(ctx, BT_OP_RETURN, ret_loc);
        return BT_TRUE;
    } break;
    case BT_AST_NODE_EXPORT: {
        uint8_t type_lit = push(ctx, BT_VALUE_OBJECT(stmt->resulting_type));
        uint8_t name_lit = push(ctx,
            BT_VALUE_STRING(bt_make_string_len(ctx->context,
                stmt->as.exp.name->source.source,
                stmt->as.exp.name->source.length)));

        uint8_t type_loc = get_register(ctx);
        emit_ab(ctx, BT_OP_LOAD, type_loc, type_lit);

        uint8_t name_loc = get_register(ctx);
        emit_ab(ctx, BT_OP_LOAD, name_loc, name_lit);

        uint8_t value_loc = get_register(ctx);
        compile_expression(ctx, stmt->as.exp.value, value_loc);
        emit_abc(ctx, BT_OP_EXPORT, name_loc, value_loc, type_loc);
    } break;
    case BT_AST_NODE_IF: {
        uint32_t end_points[64];
        uint8_t end_top = 0;

        bt_AstNode* current = stmt;

        while (current) {
            push_registers(ctx);

            uint32_t jump_loc = 0;

            if (current->as.branch.condition) {
                uint8_t condition_loc = find_binding_or_compile_temp(ctx, current->as.branch.condition);
                jump_loc = emit_a(ctx, BT_OP_JMPF, condition_loc);
            }

            compile_body(ctx, &current->as.branch.body);
            if (current->as.branch.next) end_points[end_top++] = emit(ctx, BT_OP_JMP);
        
            if (current->as.branch.condition) {
                bt_Op* jmpf = op_at(ctx, jump_loc);
                jmpf->ibc = ctx->output.length - jump_loc - 1;
            }

            current = current->as.branch.next;
            restore_registers(ctx);
        }

        for (uint8_t i = 0; i < end_top; ++i) {
            bt_Op* jmp = op_at(ctx, end_points[i]);
            jmp->ibc = ctx->output.length - end_points[i] - 1;
        }
    } break;
    default:
        push_registers(ctx);
        bt_bool result = compile_expression(ctx, stmt, get_register(ctx));
        restore_registers(ctx);
        return result;
    }

    return BT_TRUE;
}

bt_Module* bt_compile(bt_Compiler* compiler)
{
    bt_Buffer* body = &compiler->input->root->as.module.body;
    bt_Buffer* imports = &compiler->input->root->as.module.imports;

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
    fn.fn = 0;

    bt_Module* result = bt_make_module(compiler->context, imports);
    fn.module = result;

    compile_body(&fn, body);

    emit(&fn, BT_OP_END);


    result->stack_size = fn.min_top_register;
    result->constants = bt_buffer_clone(compiler->context, &fn.constants);
    result->instructions = bt_buffer_clone(compiler->context, &fn.output);

    bt_buffer_destroy(compiler->context, &fn.constants);
    bt_buffer_destroy(compiler->context, &fn.output);

    return result;
}

static bt_Fn* compile_fn(bt_Compiler* compiler, FunctionContext* parent, bt_AstNode* fn) 
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
    ctx.outer = parent;
    ctx.module = 0;
    ctx.fn = fn;

    bt_Buffer* args = &fn->as.fn.args;
    for (uint8_t i = 0; i < args->length; i++) {
        bt_FnArg* arg = bt_buffer_at(args, i);
        make_binding(&ctx, arg->name);
    }

    bt_Buffer* body = &fn->as.fn.body;
    compile_body(&ctx, body);

    if (!fn->as.fn.ret_type) {
        emit(&ctx, BT_OP_END);
    }

    bt_Module* mod = find_module(&ctx);
    bt_Fn* result = bt_make_fn(compiler->context, mod, fn->resulting_type, &ctx.constants, &ctx.output, ctx.min_top_register);

    bt_debug_print_fn(compiler->context, result);
    printf("-----------------------------------------------------\n");

    bt_buffer_destroy(compiler->context, &ctx.constants);
    bt_buffer_destroy(compiler->context, &ctx.output);

    return result;
}