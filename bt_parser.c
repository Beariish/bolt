#include "bt_parser.h"

#include "bt_context.h"
#include "bt_object.h"
#include "bt_debug.h"

#include <assert.h>

static void parse_block(bt_Buffer* result, bt_Parser* parse);
static bt_AstNode* parse_statement(bt_Parser* parse);
static bt_AstNode* type_check(bt_Parser* parse, bt_AstNode* node);

bt_Parser bt_open_parser(bt_Tokenizer* tkn)
{
    bt_Parser result;
    result.context = tkn->context;
    result.tokenizer = tkn;
    result.root = NULL;
    result.scope = NULL;

    return result;
}

void bt_close_parser(bt_Parser* parse)
{
}

static void push_scope(bt_Parser* parser, bt_bool is_fn_boundary)
{
    bt_ParseScope* new_scope = parser->context->alloc(sizeof(bt_ParseScope));
    new_scope->last = parser->scope;
    new_scope->is_fn_boundary = is_fn_boundary;

    parser->scope = new_scope;

    new_scope->bindings = BT_BUFFER_NEW(parser->context, bt_ParseBinding);
}

static void pop_scope(bt_Parser* parser)
{
    bt_ParseScope* old_scope = parser->scope;
    parser->scope = old_scope->last;
    bt_buffer_destroy(parser->context, &old_scope->bindings);
    parser->context->free(old_scope);
}

static void push_local(bt_Parser* parse, bt_AstNode* node) 
{
    bt_ParseBinding new_binding;
    new_binding.source = node;

    switch (node->type) {
    case BT_AST_NODE_LET: {
        new_binding.is_const = node->as.let.is_const;
        new_binding.name = node->as.let.name;
        new_binding.type = node->resulting_type;
    } break;
    case BT_AST_NODE_ALIAS: {
        new_binding.is_const = BT_TRUE;
        new_binding.name = node->source->source;
        new_binding.type = parse->context->types.type;
    } break;
    default: assert(0);
    }

    bt_ParseScope* topmost = parse->scope;
    for (uint32_t i = 0; i < topmost->bindings.length; ++i) {
        bt_ParseBinding* binding = (bt_ParseBinding*)bt_buffer_at(&topmost->bindings, i);
        if (bt_strslice_compare(binding->name, new_binding.name)) {
            assert(0); // Binding redifinition
        }
    }

    bt_buffer_push(parse->context, &topmost->bindings, &new_binding);
}

static void push_arg(bt_Parser* parse, bt_FnArg* arg) {
    bt_ParseBinding new_binding;
    new_binding.is_const = BT_TRUE;
    new_binding.name = arg->name;
    new_binding.type = arg->type;

    bt_ParseScope* topmost = parse->scope;
    for (uint32_t i = 0; i < topmost->bindings.length; ++i) {
        bt_ParseBinding* binding = (bt_ParseBinding*)bt_buffer_at(&topmost->bindings, i);
        if (bt_strslice_compare(binding->name, new_binding.name)) {
            assert(0); // Binding redifinition
        }
    }

    bt_buffer_push(parse->context, &topmost->bindings, &new_binding);
}

static bt_ParseBinding* find_local(bt_Parser* parse, bt_AstNode* identifier)
{
    assert(identifier->type == BT_AST_NODE_IDENTIFIER);

    bt_ParseScope* current = parse->scope;

    while (current) {
        for (uint32_t i = 0; i < current->bindings.length; ++i) {
            bt_ParseBinding* binding = (bt_ParseBinding*)bt_buffer_at(&current->bindings, i);
            if (bt_strslice_compare(binding->name, identifier->source->source)) {
                return binding;
            }
        }

        current = current->is_fn_boundary ? NULL : current->last;
    }

    return NULL;
}

static bt_ParseBinding* find_local_fast(bt_Parser* parse, bt_StrSlice identifier)
{
    bt_ParseScope* current = parse->scope;

    while (current) {
        for (uint32_t i = 0; i < current->bindings.length; ++i) {
            bt_ParseBinding* binding = (bt_ParseBinding*)bt_buffer_at(&current->bindings, i);
            if (bt_strslice_compare(binding->name, identifier)) {
                return binding;
            }
        }

        current = current->is_fn_boundary ? NULL : current->last;
    }

    return NULL;
}

static bt_ModuleImport* find_import(bt_Parser* parser, bt_AstNode* identifier)
{
    assert(identifier->type == BT_AST_NODE_IDENTIFIER);

    bt_Buffer* imports = &parser->root->as.module.imports;
    for (uint32_t i = 0; i < imports->length; ++i) {
        bt_ModuleImport* import = *(bt_ModuleImport**)bt_buffer_at(imports, i);
        if (bt_strslice_compare(bt_as_strslice(import->name), identifier->source->source)) {
            identifier->type = BT_AST_NODE_IMPORT_REFERENCE;
            return import;
        }
    }

    // Import not found, _should_ we import from prelude?
    bt_Table* prelude = parser->context->prelude;
    for (uint32_t i = 0; i < prelude->pairs.length; ++i) {
        bt_ModuleImport* entry = BT_AS_OBJECT(((bt_TablePair*)bt_buffer_at(&prelude->pairs, i))->value);

        if (bt_strslice_compare(bt_as_strslice(entry->name), identifier->source->source)) {
            bt_buffer_push(parser->context, imports, &entry);
            identifier->type = BT_AST_NODE_IMPORT_REFERENCE;
            return *(bt_ModuleImport**)bt_buffer_last(imports);
        }

    }

    return NULL;
}

static bt_ModuleImport* find_import_fast(bt_Parser* parser, bt_StrSlice identifier)
{
    bt_Buffer* imports = &parser->root->as.module.imports;
    for (uint32_t i = 0; i < imports->length; ++i) {
        bt_ModuleImport* import = *(bt_ModuleImport**)bt_buffer_at(imports, i);
        if (bt_strslice_compare(bt_as_strslice(import->name), identifier)) {
            return import;
        }
    }

    // Import not found, _should_ we import from prelude?
    bt_Table* prelude = parser->context->prelude;
    for (uint32_t i = 0; i < prelude->pairs.length; ++i) {
        bt_ModuleImport* entry = BT_AS_OBJECT(((bt_TablePair*)bt_buffer_at(&prelude->pairs, i))->value);

        if (bt_strslice_compare(bt_as_strslice(entry->name), identifier)) {
            bt_buffer_push(parser->context, imports, &entry);
            return *(bt_ModuleImport**)bt_buffer_last(imports);
        }

    }

    return NULL;
}

static bt_AstNode* make_node(bt_Context* ctx, bt_AstNodeType type)
{
    bt_AstNode* new_node = ctx->alloc(sizeof(bt_AstNode));
    new_node->type = type;
    new_node->resulting_type = NULL;
    new_node->source = 0;

    return new_node;
}

static bt_AstNode* token_to_node(bt_Parser* parse, bt_Token* token)
{
    bt_AstNode* result = NULL;
    bt_Context* ctx = parse->context;
    switch (token->type)
    {
    case BT_TOKEN_TRUE_LITERAL:
    case BT_TOKEN_FALSE_LITERAL:
        result = make_node(ctx, BT_AST_NODE_LITERAL);
        result->source = token;
        result->resulting_type = ctx->types.boolean;
        return result;

    case BT_TOKEN_STRING_LITERAL:
        result = make_node(ctx, BT_AST_NODE_LITERAL);
        result->source = token;
        result->resulting_type = ctx->types.string;
        return result;

    case BT_TOKEN_NUMBER_LITERAL:
        result = make_node(ctx, BT_AST_NODE_LITERAL);
        result->source = token;
        result->resulting_type = ctx->types.number;
        return result;

    case BT_TOKEN_NULL_LITERAL:
        result = make_node(ctx, BT_AST_NODE_LITERAL);
        result->source = token;
        result->resulting_type = ctx->types.null;
        return result;

    case BT_TOKEN_IDENTIFIER: {
        bt_Token* next = bt_tokenizer_peek(parse->tokenizer);
        result = make_node(ctx, BT_AST_NODE_IDENTIFIER);
        result->source = token;
        return result;
    } break;
    default:
        assert(0);
    }

    return NULL;
}

static bt_bool is_operator(bt_Token* token)
{
    switch (token->type)
    {
    case BT_TOKEN_PLUS: case BT_TOKEN_MINUS:
    case BT_TOKEN_MUL: case BT_TOKEN_DIV:
    case BT_TOKEN_AND: case BT_TOKEN_OR: case BT_TOKEN_NOT:
    case BT_TOKEN_EQUALS: case BT_TOKEN_NOTEQ:
    case BT_TOKEN_NULLCOALESCE: case BT_TOKEN_ASSIGN:
    case BT_TOKEN_PLUSEQ: case BT_TOKEN_MINUSEQ:
    case BT_TOKEN_MULEQ: case BT_TOKEN_DIVEQ:
    case BT_TOKEN_PERIOD: case BT_TOKEN_QUESTION: case BT_TOKEN_BANG:
    case BT_TOKEN_LEFTBRACKET: case BT_TOKEN_LEFTPAREN:
    case BT_TOKEN_LT: case BT_TOKEN_LTE:
    case BT_TOKEN_GT: case BT_TOKEN_GTE:
        return BT_TRUE;
    default:
        return BT_FALSE;
    }
}

static uint8_t prefix_binding_power(bt_Token* token)
{
    switch (token->type)
    {
    case BT_TOKEN_PLUS: case BT_TOKEN_MINUS: return 13;
    case BT_TOKEN_NOT: return 14;
    default:
        return 0;
    }
}

static uint8_t postfix_binding_power(bt_Token* token)
{
    switch (token->type)
    {
    case BT_TOKEN_LEFTPAREN: return 14;
    case BT_TOKEN_QUESTION: return 15;
    case BT_TOKEN_BANG: return 16;
    case BT_TOKEN_LEFTBRACKET: return 17;
    default:
        return 0;
    }
}

typedef struct InfixBindingPower {
    uint8_t left, right;
} InfixBindingPower;

static InfixBindingPower infix_binding_power(bt_Token* token)
{
    switch (token->type)
    {
    case BT_TOKEN_ASSIGN: return (InfixBindingPower) { 2, 1 };
    
    case BT_TOKEN_PLUSEQ: case BT_TOKEN_MINUSEQ: case BT_TOKEN_MULEQ: case BT_TOKEN_DIVEQ:
        return (InfixBindingPower) { 4, 3 };
    
    case BT_TOKEN_AND: case BT_TOKEN_OR: return (InfixBindingPower) { 5, 6 };
    case BT_TOKEN_EQUALS: case BT_TOKEN_NOTEQ: return (InfixBindingPower) { 7, 8 };
    case BT_TOKEN_PLUS: case BT_TOKEN_MINUS: return (InfixBindingPower) { 9, 10 };
    case BT_TOKEN_LT: case BT_TOKEN_LTE:
    case BT_TOKEN_GT: case BT_TOKEN_GTE:
        return (InfixBindingPower) { 11, 12 };
    case BT_TOKEN_MUL: case BT_TOKEN_DIV: return (InfixBindingPower) { 13, 14 };
    case BT_TOKEN_NULLCOALESCE: return (InfixBindingPower) { 15, 16 };
    case BT_TOKEN_PERIOD: return (InfixBindingPower) { 101, 100 };
    default: return (InfixBindingPower) { 0, 0 };
    }
}

static bt_Type* parse_type(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_Token* token = bt_tokenizer_emit(tok);
    bt_Context* ctx = tok->context;

    switch (token->type) {
    case BT_TOKEN_IDENTIFIER: {
        bt_ParseBinding* binding = find_local_fast(parse, token->source);
        
        bt_Type* result = 0;
        if (binding) {
            if (binding->source->resulting_type != ctx->types.type) {
                assert(0 && "Type identifier didn't resolve to type!");
            }

            result = binding->source->as.alias.type;
        }

        if (!result) {
            bt_ModuleImport* import = find_import_fast(parse, token->source);
            if (import) {
                if (import->type != ctx->types.type) {
                    assert(0 && "Type identifier didn't resolve to type!");
                }

                result = BT_AS_OBJECT(import->value);
            }
        }

        if (!result) {
            bt_String* name = bt_make_string_hashed_len(ctx, token->source.source, token->source.length);
            result = bt_find_type(ctx, BT_VALUE_STRING(name));
        }

        if (!result) assert(0 && "Failed to identify type!");

        token = bt_tokenizer_peek(tok);
        if (token->type == BT_TOKEN_QUESTION) {
            bt_tokenizer_emit(tok);
            result = bt_make_nullable(ctx, result);
        }

        return result;
    } break;
    case BT_TOKEN_FN: {
        bt_Type* args[16];
        uint8_t arg_top = 0;

        token = bt_tokenizer_peek(tok);
        if (token->type == BT_TOKEN_LEFTPAREN) {
            bt_tokenizer_emit(tok);
            token = bt_tokenizer_peek(tok);
            while (token->type != BT_TOKEN_RIGHTPAREN) {
                args[arg_top++] = parse_type(parse);
                token = bt_tokenizer_emit(tok);
                if (token->type != BT_TOKEN_COMMA && token->type != BT_TOKEN_RIGHTPAREN) {
                    assert(0 && "Invalid token in function type signature!");
                }
            }
        }

        bt_Type* return_type = 0;
        token = bt_tokenizer_peek(tok);

        if (token->type == BT_TOKEN_COLON) {
            bt_tokenizer_emit(tok);
            return_type = parse_type(parse);
        }

        return bt_make_signature(ctx, return_type, args, arg_top);
    } break;
    default: assert(0);
    }
}

static bt_Type* infer_return(bt_Buffer* body, bt_Type* expected)
{
    for (uint32_t i = 0; i < body->length; ++i) {
        bt_AstNode* expr = *(bt_AstNode**)bt_buffer_at(body, i);
        if (!expr) continue;

        if (expr->type == BT_AST_NODE_RETURN) {
            if (expected && expr->resulting_type == NULL) {
                assert(0 && "Expected block to return value!");
            }

            if (!expected && expr) {
                expected = expr->resulting_type;
            }

            if (expected && !expected->satisfier(expected, expr->resulting_type)) {
                assert(0 && "Block returns wrong type!");
            }
        }
        else if (expr->type == BT_AST_NODE_IF) {
            expected = infer_return(&expr->as.branch.body, expected);
            bt_AstNode* elif = expr->as.branch.next;
            while (elif) {
                expected = infer_return(&elif->as.branch.body, expected);
                elif = elif->as.branch.next;
            }
        }
    }

    return expected;
}

static bt_AstNode* parse_function_literal(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;

    bt_AstNode* result = make_node(parse->context, BT_AST_NODE_FUNCTION);
    result->as.fn.args = BT_BUFFER_NEW(parse->context, bt_FnArg);
    result->as.fn.body = BT_BUFFER_WITH_CAPACITY(parse->context, bt_AstNode*, 8);
    result->as.fn.ret_type = NULL;
    result->as.fn.outer = parse->current_fn;
    result->as.fn.upvals = bt_buffer_empty();

    parse->current_fn = result;

    bt_Token* next = bt_tokenizer_peek(tok);

    bt_bool has_param_list = BT_FALSE;
    if (next->type == BT_TOKEN_LEFTPAREN) {
        has_param_list = BT_TRUE;

        bt_tokenizer_emit(tok);

        do {
            next = bt_tokenizer_emit(tok);

            bt_FnArg this_arg;
            if (next->type == BT_TOKEN_IDENTIFIER) {
                this_arg.name = next->source;
                next = bt_tokenizer_peek(tok);
            }
            else if (next->type == BT_TOKEN_RIGHTPAREN) {
                break;
            }
            else {
                assert(0 && "Malformed parameter list!");
            }

            if (next->type == BT_TOKEN_COLON) {
                next = bt_tokenizer_emit(tok);
                this_arg.type = parse_type(parse);
            }
            else {
                this_arg.type = parse->context->types.any;
            }


            bt_buffer_push(parse->context, &result->as.fn.args, &this_arg);

            next = bt_tokenizer_emit(tok);
        } while (next && next->type == BT_TOKEN_COMMA);
    }

    if (has_param_list && next->type != BT_TOKEN_RIGHTPAREN) {
        assert(0 && "Cannot find end of parameter list!");
    }

    next = bt_tokenizer_peek(tok);
    
    if (next->type == BT_TOKEN_COLON) {
        next = bt_tokenizer_emit(tok);
        result->as.fn.ret_type = parse_type(parse);
    }

    next = bt_tokenizer_emit(tok);

    if (next->type == BT_TOKEN_LEFTBRACE) {
        push_scope(parse, BT_TRUE);

        for (uint8_t i = 0; i < result->as.fn.args.length; i++) {
            push_arg(parse, (bt_FnArg*)bt_buffer_at(&result->as.fn.args, i));
        }

        parse_block(&result->as.fn.body, parse);
        
        pop_scope(parse);
    }
    else {
        assert(0 && "Found function without body!");
    }

    result->as.fn.ret_type = infer_return(&result->as.fn.body, result->as.fn.ret_type);
    
    next = bt_tokenizer_emit(tok);
    if (next->type != BT_TOKEN_RIGHTBRACE) {
        assert(0 && "Expected end of function!");
    }

    bt_Type* args[16];

    for (uint8_t i = 0; i < result->as.fn.args.length; ++i) {
        args[i] = ((bt_FnArg*)bt_buffer_at(&result->as.fn.args, i))->type;
    }

    result->resulting_type = bt_make_signature(parse->context, result->as.fn.ret_type, args, result->as.fn.args.length);

    parse->current_fn = parse->current_fn->as.fn.outer;

    return result;
}

static void parse_block(bt_Buffer* result, bt_Parser* parse)
{
    push_scope(parse, BT_FALSE);

    bt_Token* next = bt_tokenizer_peek(parse->tokenizer);

    while (next->type != BT_TOKEN_RIGHTBRACE)
    {
        bt_AstNode* expression = parse_statement(parse);
        bt_buffer_push(parse->context, result, &expression);
        next = bt_tokenizer_peek(parse->tokenizer);
    }

    pop_scope(parse);
}

static bt_AstNode* pratt_parse(bt_Parser* parse, uint32_t min_binding_power)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_Token* lhs = bt_tokenizer_emit(tok);

    bt_AstNode* lhs_node;

    if (lhs->type == BT_TOKEN_FN) {
        lhs_node = parse_function_literal(parse);
    }
    else if (lhs->type == BT_TOKEN_LEFTPAREN) {
        lhs_node = pratt_parse(parse, 0);
        assert(bt_tokenizer_expect(tok, BT_TOKEN_RIGHTPAREN));
    }
    else if (prefix_binding_power(lhs)) {
        lhs_node = make_node(tok->context, BT_AST_NODE_UNARY_OP);
        lhs_node->source = lhs;
        lhs_node->as.unary_op.operand = pratt_parse(parse, prefix_binding_power(lhs));
    }
    else {
        lhs_node = token_to_node(parse, lhs);
    }
    
    for (;;) {
        bt_Token* op = bt_tokenizer_peek(tok);

        // end of file is end of expression, two non-operators following each other is also an expression bound
        if (op == NULL || !is_operator(op)) break;

        uint8_t post_bp = postfix_binding_power(op);
        if (post_bp) {
            if (post_bp < min_binding_power) break;
            bt_tokenizer_emit(tok); // consume peeked operator

            if (op->type == BT_TOKEN_LEFTBRACKET)
            {
                bt_AstNode* rhs = pratt_parse(tok, 0);
                assert(bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACKET));
                
                bt_AstNode* lhs = lhs_node;
                lhs_node = make_node(tok->context, BT_AST_NODE_BINARY_OP);
                lhs_node->source = op;
                lhs_node->as.binary_op.left = lhs;
                lhs_node->as.binary_op.right = rhs;
            }
            else if (op->type == BT_TOKEN_LEFTPAREN)
            {
                bt_Type* to_call = type_check(parse, lhs_node)->resulting_type;
                
                if (!to_call || (to_call->category != BT_TYPE_CATEGORY_SIGNATURE && to_call != parse->context->types.any)) {
                    assert(0 && "Trying to call non-callable type!");
                }

                bt_AstNode* args[16];
                uint8_t max_arg = 0;

                bt_Token* next = bt_tokenizer_peek(tok);
                while (next && next->type != BT_TOKEN_RIGHTPAREN) {
                    args[max_arg++] = pratt_parse(parse, 0);
                    next = bt_tokenizer_emit(tok);

                    if (!next || (next->type != BT_TOKEN_COMMA && next->type != BT_TOKEN_RIGHTPAREN)) {
                        assert(0 && "Invalid token in parameter list!");
                    }
                }

                if (max_arg == 0) bt_tokenizer_emit(tok);

                if (!next || next->type != BT_TOKEN_RIGHTPAREN) {
                    assert(0 && "Couldn't find end of function call!");
                }

                if (max_arg != to_call->as.fn.args.length && to_call->as.fn.is_vararg == BT_FALSE) {
                    assert(0 && "Incorrect number of arguments!");
                }

                bt_AstNode* call = make_node(parse->context, BT_AST_NODE_CALL);
                call->as.call.fn = lhs_node;
                call->as.call.args = BT_BUFFER_WITH_CAPACITY(parse->context, bt_AstNode*, max_arg);
                
                for (uint8_t i = 0; i < max_arg; i++) {
                    bt_Type* arg_type = type_check(parse, args[i])->resulting_type;
                    
                    if (i < to_call->as.fn.args.length) {
                        bt_Type* fn_type = *(bt_Type**)bt_buffer_at(&to_call->as.fn.args, i);
                        if (!fn_type->satisfier(fn_type, arg_type)) {
                            assert(0 && "Invalid argument type!");
                        }
                        else {
                            bt_buffer_push(parse->context, &call->as.call.args, &args[i]);
                        }
                    }
                    else {
                        if (!to_call->as.fn.varargs_type->satisfier(to_call->as.fn.varargs_type, arg_type)) {
                            assert(0 && "Arg doesn't match typed vararg");
                        }
                        else {
                            bt_buffer_push(parse->context, &call->as.call.args, &args[i]);
                        }
                    }
                }

                call->resulting_type = to_call->as.fn.return_type;
                lhs_node = call;
            }
            else
            {
                bt_AstNode* lhs = lhs_node;
                lhs_node = make_node(tok->context, BT_AST_NODE_UNARY_OP);
                lhs_node->source = op;
                lhs_node->as.unary_op.operand = lhs;
            }
            continue;
        }

        InfixBindingPower infix_bp = infix_binding_power(op);
        if (infix_bp.left != 0)
        {
            if (infix_bp.left < min_binding_power) break;
            bt_tokenizer_emit(tok); // consume peeked operator
            
            bt_AstNode* rhs = pratt_parse(parse, infix_bp.right);

            bt_AstNode* lhs = lhs_node;
            lhs_node = make_node(tok->context, BT_AST_NODE_BINARY_OP);
            lhs_node->source = op;
            lhs_node->as.binary_op.accelerated = BT_FALSE;
            lhs_node->as.binary_op.left = lhs;
            lhs_node->as.binary_op.right = rhs;
            type_check(parse, lhs_node);

            continue;
        }

        break;
    }
    
    return lhs_node;
}

static void push_upval(bt_Parser* parse, bt_AstNode* fn, bt_ParseBinding* upval)
{
    if (fn->as.fn.upvals.element_size == 0) {
        fn->as.fn.upvals = BT_BUFFER_NEW(parse->context, bt_ParseBinding);
    }

    for (uint32_t i = 0; i < fn->as.fn.upvals.length; ++i) {
        bt_ParseBinding* binding = (bt_ParseBinding*)bt_buffer_at(&fn->as.fn.upvals, i);
        if (bt_strslice_compare(binding->name, upval->name)) {
            return;
        }
    }

    bt_buffer_push(parse->context, &fn->as.fn.upvals, upval);
}

static bt_ParseBinding* find_upval(bt_Parser* parse, bt_AstNode* ident)
{
    bt_AstNode* fn = parse->current_fn;

    if (!fn) return NULL;

    for (uint32_t i = 0; i < fn->as.fn.upvals.length; ++i) {
        bt_ParseBinding* binding = (bt_ParseBinding*)bt_buffer_at(&fn->as.fn.upvals, i);
        if (bt_strslice_compare(binding->name, ident->source->source)) {
            return binding;
        }
    }

    return NULL;
}

static bt_Type* find_binding(bt_Parser* parse, bt_AstNode* ident)
{
    bt_ParseBinding* binding = find_local(parse, ident);
    if (binding) return binding->type;

    binding = find_upval(parse, ident);
    if (binding) return binding->type;

    bt_AstNode* fns[8];
    uint8_t fns_top = 0;

    fns[fns_top++] = parse->current_fn;

    bt_ParseScope* scope = parse->scope;

    while (scope) {
        for (uint32_t i = 0; i < scope->bindings.length; ++i) {
            bt_ParseBinding* binding = (bt_ParseBinding*)bt_buffer_at(&scope->bindings, i);
            if (bt_strslice_compare(binding->name, ident->source->source)) {
                for (uint8_t j = 0; j < fns_top - 1; ++j) {
                    push_upval(parse, fns[j], binding);
                }

                return binding->type;
            }
        }

        if (scope->is_fn_boundary) {
            fns[fns_top] = fns[fns_top - 1]->as.fn.outer;
            fns_top++;
        }

        scope = scope->last;
    }

    bt_ModuleImport* import = find_import(parse, ident);
    if (import) return import->type;

    return NULL;
}

static bt_AstNode* type_check(bt_Parser* parse, bt_AstNode* node)
{
    if (node->resulting_type) return node;

    switch (node->type) {
    case BT_AST_NODE_IDENTIFIER: {
        bt_Type* type = find_binding(parse, node);
        if (type) {
            node->resulting_type = type;
        }
        else {
            assert(0 && "Failed to find identifier!");
        }
    } break;
    case BT_AST_NODE_LITERAL:
        if (node->resulting_type == NULL) {
            assert(0);
            return NULL;
        }
        break;
    case BT_AST_NODE_UNARY_OP: {
        switch (node->source->type) {
        case BT_TOKEN_QUESTION:
            if (!type_check(parse, node->as.unary_op.operand)->resulting_type->is_optional) {
                assert(0 && "Unary operator ? can only be applied to nullable types.");
                return NULL;
            }
            node->resulting_type = parse->context->types.boolean;
            break;
        case BT_TOKEN_BANG:
            if (!type_check(parse, node->as.unary_op.operand)->resulting_type->is_optional) {
                assert(0 && "Unary operator ! can only be applied to nullable types.");
                return NULL;
            }
            node->resulting_type = node->as.unary_op.operand->resulting_type->as.nullable.base;
            break;
        default:
            node->resulting_type = type_check(parse, node->as.unary_op.operand)->resulting_type;
            break;
        }
    } break;
    case BT_AST_NODE_BINARY_OP:
        switch (node->source->type) {
        case BT_TOKEN_NULLCOALESCE: {
            node->resulting_type = type_check(parse, node->as.binary_op.right)->resulting_type;
            bt_Type* lhs = type_check(parse, node->as.binary_op.left)->resulting_type;

            if (!lhs->is_optional) {
                assert(0 && "Lhs is non-optional, cannot coalesce!");
                return NULL;
            }

            lhs = bt_remove_nullable(parse->context, lhs);

            if(!lhs->satisfier(node->resulting_type, lhs)) {
                assert(0 && "Unable to coalesce rhs into lhs");
                return NULL;
            }
        } break;
        case BT_TOKEN_PERIOD: {
            if (node->as.binary_op.right->type != BT_AST_NODE_IDENTIFIER) assert(0 && "Illegal identifier!");

            node->as.binary_op.right->type = BT_AST_NODE_LITERAL;
            node->as.binary_op.right->source->type = BT_TOKEN_IDENTIFER_LITERAL;

            bt_Type* lhs = type_check(parse, node->as.binary_op.left)->resulting_type;
            if (lhs == parse->context->types.table) {
                node->resulting_type = parse->context->types.any;
                return node;
            }

            if (lhs->category != BT_TYPE_CATEGORY_TABLESHAPE) { assert(0 && "Lhs must be table!"); }

            bt_Token* rhs = node->as.binary_op.right->source;
            bt_Value rhs_key = BT_VALUE_STRING(bt_make_string_len(parse->context, rhs->source.source, rhs->source.length));

            bt_Table* layout = lhs->as.table_shape.layout;
            bt_Value table_entry = bt_table_get(layout, rhs_key);
            if (table_entry != BT_VALUE_NULL) {
                bt_Type* type = BT_AS_OBJECT(table_entry);
                node->resulting_type = type;
                return node;
            }

            bt_Table* proto = lhs->as.table_shape.proto;
            bt_Value proto_entry = bt_table_get(layout, rhs_key);
            if (proto_entry != BT_VALUE_NULL) {
                bt_Type* type = BT_AS_OBJECT(proto_entry);
                node->resulting_type = type;
                return node;
            }

            if (lhs->as.table_shape.sealed) {
                assert(0 && "Couldn't find item in table shape.");
            }

            node->resulting_type = parse->context->types.any;
        } break;

        // Comparison binops always produce boolean
        case BT_TOKEN_LT: case BT_TOKEN_LTE: case BT_TOKEN_GT: case BT_TOKEN_GTE: {
            if (type_check(parse, node->as.binary_op.left)->resulting_type != parse->context->types.number) assert(0);
            if (type_check(parse, node->as.binary_op.right)->resulting_type != parse->context->types.number) assert(0);
            node->as.binary_op.accelerated = BT_TRUE;
            node->resulting_type = parse->context->types.boolean;
        } break;
        case BT_TOKEN_EQUALS: case BT_TOKEN_NOTEQ: {
            if (type_check(parse, node->as.binary_op.left)->resulting_type !=
                type_check(parse, node->as.binary_op.right)->resulting_type) assert(0);
            node->resulting_type = parse->context->types.boolean;
        } break;

        default:
            node->resulting_type = type_check(parse, node->as.binary_op.left)->resulting_type;
            if (!node->resulting_type->satisfier(node->resulting_type, type_check(parse, node->as.binary_op.right)->resulting_type)) {
                assert(0);
                return NULL;
            }

            if (node->resulting_type == parse->context->types.number) {
                node->as.binary_op.accelerated = BT_TRUE;
            } break;
        }
        break;
    }

    return node;
}

static bt_AstNode* parse_let(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_AstNode* node = make_node(tok->context, BT_AST_NODE_LET);
    node->as.let.is_const = BT_FALSE;

    bt_Token* name_or_const = bt_tokenizer_emit(tok);

    if (name_or_const->type == BT_TOKEN_CONST) {
        node->as.let.is_const = BT_TRUE;
        name_or_const = bt_tokenizer_emit(tok);
    }

    if (name_or_const->type != BT_TOKEN_IDENTIFIER) {
        assert(0);
    }
    node->as.let.name = name_or_const->source;

    bt_Token* type_or_expr = bt_tokenizer_peek(tok);

    if (type_or_expr->type == BT_TOKEN_COLON) {
        bt_tokenizer_emit(tok);

        bt_Type* type = parse_type(parse);
        if (!type) assert(0);

        node->resulting_type = type;
        type_or_expr = bt_tokenizer_peek(tok);
    }

    if (type_or_expr->type == BT_TOKEN_ASSIGN) {
        bt_tokenizer_emit(tok); // eat assignment operator
        bt_AstNode* rhs = pratt_parse(parse, 0);
        node->as.let.initializer = rhs;

        if (node->resulting_type) {
            if (!node->resulting_type->satisfier(node->resulting_type, type_check(parse, rhs)->resulting_type)) {
                assert(0); // assignment doesnt match explicit binding type
            }
        }
        else {
            node->resulting_type = type_check(parse, rhs)->resulting_type;
            if (!node->resulting_type) assert(0 && "Right hand side did not evaluate to type!");
        }
    }
    else {
        if (!node->resulting_type)
            assert(0); // no explicit type or expression to infer from!
    }

    push_local(parse, node);
    return node;
}

static bt_AstNode* parse_var(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_AstNode* node = make_node(tok->context, BT_AST_NODE_LET);
    node->as.let.is_const = BT_FALSE;

    bt_Token* name_or_const = bt_tokenizer_emit(tok);

    if (name_or_const->type == BT_TOKEN_CONST) {
        node->as.let.is_const = BT_TRUE;
        name_or_const = bt_tokenizer_emit(tok);
    }

    if (name_or_const->type != BT_TOKEN_IDENTIFIER) {
        assert(0);
    }
    node->as.let.name = name_or_const->source;

    bt_Token* expr = bt_tokenizer_peek(tok);

    if (expr->type == BT_TOKEN_ASSIGN) {
        bt_tokenizer_emit(tok); // eat assignment operator
        bt_AstNode* rhs = pratt_parse(parse, 0);
        node->as.let.initializer = rhs;
    }

    node->resulting_type = tok->context->types.any;

    push_local(parse, node);
    return node;
}

static bt_AstNode* parse_return(bt_Parser* parse)
{
    bt_AstNode* node = make_node(parse->context, BT_AST_NODE_RETURN);
    node->as.ret.expr = pratt_parse(parse, 0);
    node->resulting_type = type_check(parse, node->as.ret.expr)->resulting_type;
    return node;
}

static bt_AstNode* parse_import(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;

    bt_Token* name_or_first_item = bt_tokenizer_emit(tok);
    bt_Token* output_name = name_or_first_item;

    if (name_or_first_item->type == BT_TOKEN_MUL) {
        // glob import
        bt_Token* next = bt_tokenizer_emit(tok);
        if (next->type != BT_TOKEN_FROM) assert(0 && "Expected 'from' statement!");
        next = bt_tokenizer_emit(tok);
        if (next->type != BT_TOKEN_IDENTIFIER) assert(0 && "Expected module name!");

        bt_Value module_name = BT_VALUE_STRING(bt_make_string_len(parse->context,
            next->source.source, next->source.length));

        bt_Module* mod_to_import = bt_find_module(parse->context, module_name);

        if (!mod_to_import) {
            assert(0 && "Failed to import module!");
        }

        bt_Type* export_types = mod_to_import->type;
        bt_Table* types = export_types->as.table_shape.layout;
        bt_Table* values = mod_to_import->exports;

        for (uint32_t i = 0; i < values->pairs.length; ++i) {
            bt_TablePair* item = bt_buffer_at(&values->pairs, i);
            bt_Value type_val = bt_table_get(types, item->key);

            if (type_val == BT_VALUE_NULL) assert(0 && "Couldn't find import in module type!");

            bt_ModuleImport* import = BT_ALLOCATE(parse->context, IMPORT, bt_ModuleImport);
            import->name = BT_AS_STRING(item->key);
            import->type = BT_AS_OBJECT(type_val);
            import->value = item->value;

            bt_buffer_push(parse->context, &parse->root->as.module.imports, &import);
        }

        return NULL;
    }

    if (name_or_first_item->type != BT_TOKEN_IDENTIFIER) {
        assert(0 && "Invalid import statement!");
    }

    bt_Token* peek = bt_tokenizer_peek(tok);
    if (peek->type == BT_TOKEN_COMMA || peek->type == BT_TOKEN_FROM) {
       bt_Buffer items = bt_buffer_new(parse->context, sizeof(bt_StrSlice));
       bt_buffer_push(parse->context, &items, &name_or_first_item->source);
    
       while (peek->type == BT_TOKEN_COMMA) {
           bt_tokenizer_emit(tok);
           peek = bt_tokenizer_peek(tok);

           if (peek->type == BT_TOKEN_IDENTIFIER) {
               bt_tokenizer_emit(tok);
               bt_buffer_push(parse->context, &items, &peek->source);
               peek = bt_tokenizer_peek(tok);
           }
       }

       if (peek->type != BT_TOKEN_FROM) {
           assert(0 && "Expected 'from' statement!");
       }

       bt_tokenizer_emit(tok);

       bt_Token* mod_name = bt_tokenizer_emit(tok);
       if (mod_name->type != BT_TOKEN_IDENTIFIER) assert(0 && "Expected module name!");

       bt_Value module_name = BT_VALUE_STRING(bt_make_string_len(parse->context,
           mod_name->source.source, mod_name->source.length));

       bt_Module* mod_to_import = bt_find_module(parse->context, module_name);

       if (!mod_to_import) {
           assert(0 && "Failed to import module!");
       }

       bt_Type* export_types = mod_to_import->type;
       bt_Table* types = export_types->as.table_shape.layout;
       bt_Table* values = mod_to_import->exports;

       for (uint32_t i = 0; i < items.length; ++i) {
           bt_StrSlice* item = bt_buffer_at(&items, i);

           bt_ModuleImport* import = BT_ALLOCATE(parse->context, IMPORT, bt_ModuleImport);
           import->name = bt_make_string_len(parse->context, item->source, item->length);

           bt_Value type_val = bt_table_get(types, BT_VALUE_STRING(import->name));
           bt_Value value = bt_table_get(values, BT_VALUE_STRING(import->name));

           if (type_val == BT_VALUE_NULL || value == BT_VALUE_NULL) {
               assert(0 && "Failed to hoist import from module!");
           }

           import->type = BT_AS_OBJECT(type_val);
           import->value = value;

           bt_buffer_push(parse->context, &parse->root->as.module.imports, &import);
       }

       bt_buffer_destroy(parse->context, &items);
       return NULL;
    }
    else if (peek->type == BT_TOKEN_AS) {
        bt_tokenizer_emit(tok);
        output_name = bt_tokenizer_emit(tok);

        if (output_name->type != BT_TOKEN_IDENTIFIER) {
            assert(0 && "Invalid import statement!");
        }
    }

    bt_Value module_name = BT_VALUE_STRING(bt_make_string_len(parse->context, 
        name_or_first_item->source.source, name_or_first_item->source.length));
    
    bt_Module* mod_to_import = bt_find_module(parse->context, module_name);

    if (!mod_to_import) {
        assert(0 && "Failed to import module!");
    }

    bt_ModuleImport* import = BT_ALLOCATE(parse->context, IMPORT, bt_ModuleImport);
    import->name = bt_make_string_len(parse->context, output_name->source.source, output_name->source.length);
    import->type = mod_to_import->type;
    import->value = BT_VALUE_OBJECT(mod_to_import->exports);

    bt_buffer_push(parse->context, &parse->root->as.module.imports, &import);

    return NULL;
}

static bt_AstNode* parse_export(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_AstNode* to_export = pratt_parse(parse, 0);
    
    bt_Token* name = to_export->source;

    bt_Token* peek = bt_tokenizer_peek(tok);
    if (peek && peek->type == BT_TOKEN_AS) {
        bt_tokenizer_emit(tok);
        name = bt_tokenizer_emit(tok);
        if (name->type != BT_TOKEN_IDENTIFIER) assert(0 && "Expected valid export name!");
    }
    else if (to_export->type != BT_AST_NODE_IDENTIFIER) {
        assert(0 && "Expected 'as' statement following expression export!");
    }

    bt_AstNode* export = make_node(parse->context, BT_AST_NODE_EXPORT);
    export->as.exp.name = name;
    export->as.exp.value = to_export;
    export->resulting_type = type_check(parse, to_export)->resulting_type;
    
    if (export->resulting_type == 0) assert(0 && "Export statement didn't resolve to known type!");

    return export;
}

static bt_AstNode* parse_function_statement(bt_Parser* parser)
{
    bt_Tokenizer* tok = parser->tokenizer;
    bt_Token* ident = bt_tokenizer_emit(tok);

    if (ident->type != BT_TOKEN_IDENTIFIER) assert(0 && "Cannot assign to non-identifier!");

    bt_AstNode* fn = parse_function_literal(parser);
    if (fn->type != BT_AST_NODE_FUNCTION) assert(0 && "Expected function literal!");

    bt_AstNode* result = make_node(parser->context, BT_AST_NODE_LET);
    result->resulting_type = type_check(parser, fn)->resulting_type;
    result->as.let.name = ident->source;
    result->as.let.initializer = fn;
    result->as.let.is_const = BT_TRUE;

    push_local(parser, result);

    return result;
}

static bt_AstNode* parse_if(bt_Parser* parser)
{
    bt_Tokenizer* tok = parser->tokenizer;

    bt_AstNode* condition = pratt_parse(parser, 0);
    if (type_check(parser, condition)->resulting_type != parser->context->types.boolean) {
        assert(0 && "If expression must evaluate to boolean!");
    }

    bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);

    bt_Buffer body = BT_BUFFER_NEW(parser->context, bt_AstNode*);
    parse_block(&body, parser);

    bt_AstNode* result = make_node(parser->context, BT_AST_NODE_IF);
    result->as.branch.condition = condition;
    result->as.branch.body = body;
    result->as.branch.next = NULL;

    bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);

    bt_Token* next = bt_tokenizer_peek(tok);
    if (next->type == BT_TOKEN_ELSE) {
        bt_tokenizer_emit(tok);
        next = bt_tokenizer_peek(tok);

        if (next->type == BT_TOKEN_IF) {
            bt_tokenizer_emit(tok);
            result->as.branch.next = parse_if(parser);
        }
        else {
            bt_AstNode* else_node = make_node(parser->context, BT_AST_NODE_IF);
            else_node->as.branch.condition = NULL;
            else_node->as.branch.next = NULL;

            bt_Buffer body = BT_BUFFER_NEW(parser->context, bt_AstNode*);
            
            bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);
            parse_block(&body, parser);
            bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);

            else_node->as.branch.body = body;

            result->as.branch.next = else_node;
        }
    }

    return result;
}

static bt_AstNode* parse_for(bt_Parser* parse)
{
    bt_AstNode* identifier = pratt_parse(parse, 0);
    if (identifier->type != BT_AST_NODE_IDENTIFIER)
        assert(0 && "Invalid loop identifier!");

    bt_Tokenizer* tok = parse->tokenizer;
    bt_Token* token = bt_tokenizer_peek(tok);

    if (token->type != BT_TOKEN_IN) assert(0 && "Expected keyword 'in'!");
    bt_tokenizer_emit(tok);

    bt_AstNode* iterator = pratt_parse(parse, 0);

    bt_Type* generator_type = type_check(parse, iterator)->resulting_type;

    if (generator_type == parse->context->types.number) {
        bt_AstNode* stop = iterator;

        bt_AstNode* start = 0;
        bt_AstNode* step = 0;

        token = bt_tokenizer_peek(tok);
        if (token->type == BT_TOKEN_TO) {
            bt_tokenizer_emit(tok);
            start = stop;
            stop = pratt_parse(parse, 0);
        }
        else {
            start = token_to_node(parse, tok->literal_zero);
        }

        token = bt_tokenizer_peek(tok);
        if (token->type == BT_TOKEN_BY) {
            bt_tokenizer_emit(tok);
            step = pratt_parse(parse, 0);
        }
        else {
            step = token_to_node(parse, tok->literal_one);
        }

        bt_AstNode* result = make_node(parse->context, BT_AST_NODE_LOOP_NUMERIC);
        result->as.loop_numeric.start = start;
        result->as.loop_numeric.stop = stop;
        result->as.loop_numeric.step = step;

        identifier->resulting_type = parse->context->types.number;
        result->as.loop_numeric.identifier = identifier;

        bt_Buffer body = BT_BUFFER_NEW(parse->context, bt_AstNode*);

        push_scope(parse, BT_FALSE);

        bt_AstNode* ident_as_let = make_node(parse->context, BT_AST_NODE_LET);
        ident_as_let->as.let.initializer = NULL;
        ident_as_let->as.let.is_const = BT_TRUE;
        ident_as_let->as.let.name = identifier->source->source;
        ident_as_let->resulting_type = identifier->resulting_type;

        push_local(parse, ident_as_let);

        bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);
        parse_block(&body, parse);
        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);

        pop_scope(parse);

        result->as.loop_numeric.body = body;

        return result;
    }
    else if (generator_type->category != BT_TYPE_CATEGORY_SIGNATURE) assert(0 && "Expected iterator to be function!");

    bt_Type* generated_type = generator_type->as.fn.return_type;
    if (!generated_type->is_optional) assert(0 && "Iterator return type must be optional!");

    bt_Type* it_type = generated_type->as.nullable.base;
    identifier->resulting_type = it_type;

    bt_Buffer body = BT_BUFFER_NEW(parse->context, bt_AstNode*);

    push_scope(parse, BT_FALSE);

    bt_AstNode* ident_as_let = make_node(parse->context, BT_AST_NODE_LET);
    ident_as_let->as.let.initializer = NULL;
    ident_as_let->as.let.is_const = BT_TRUE;
    ident_as_let->as.let.name = identifier->source->source;
    ident_as_let->resulting_type = identifier->resulting_type;

    push_local(parse, ident_as_let);

    bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);
    parse_block(&body, parse);
    bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);
    
    pop_scope(parse);

    bt_AstNode* result = make_node(parse->context, BT_AST_NODE_LOOP_ITERATOR);
    result->as.loop_iterator.body = body;
    result->as.loop_iterator.identifier = identifier;
    result->as.loop_iterator.iterator = iterator;

    return result;
}

static bt_AstNode* parse_alias(bt_Parser* parse)
{
    bt_AstNode* result = make_node(parse->context, BT_AST_NODE_ALIAS);

    bt_Token* name = bt_tokenizer_emit(parse->tokenizer);

    if (name->type != BT_TOKEN_IDENTIFIER) {
        assert(0 && "Invalid type alias name!");
    }

    result->source = name;
    result->resulting_type = parse->context->types.type;

    bt_tokenizer_expect(parse->tokenizer, BT_TOKEN_ASSIGN);

    bt_Type* type = parse_type(parse);

    result->as.alias.type = type;

    push_local(parse, result);

    return result;
}

static bt_AstNode* parse_statement(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_Token* token = bt_tokenizer_peek(tok);
    switch (token->type) {
    case BT_TOKEN_IMPORT: {
        bt_tokenizer_emit(tok);
        return parse_import(parse);
    } break;
    case BT_TOKEN_EXPORT: {
        bt_tokenizer_emit(tok);
        return parse_export(parse);
    } break;
    case BT_TOKEN_LET: {
        bt_tokenizer_emit(tok);
        return parse_let(parse);
    } break;
    case BT_TOKEN_VAR: {
        bt_tokenizer_emit(tok);
        return parse_var(parse);
    } break;
    case BT_TOKEN_RETURN: {
        bt_tokenizer_emit(tok);
        return parse_return(parse);
    } break;
    case BT_TOKEN_FN: {
        bt_tokenizer_emit(tok);
        return parse_function_statement(parse);
    } break;
    case BT_TOKEN_IF: {
        bt_tokenizer_emit(tok);
        return parse_if(parse);
    } break;
    case BT_TOKEN_FOR: {
        bt_tokenizer_emit(tok);
        return parse_for(parse);
    } break;
    case BT_TOKEN_TYPE: {
        bt_tokenizer_emit(tok);
        return parse_alias(parse);
    }
    default: // no statment structure found, assume expression
        return pratt_parse(parse, 0);
    }
}

bt_bool bt_parse(bt_Parser* parser)
{
    parser->root = (bt_AstNode*)parser->context->alloc(sizeof(bt_AstNode));
    parser->root->type = BT_AST_NODE_MODULE;
    parser->root->as.module.body = bt_buffer_new(parser->context, sizeof(bt_AstNode*));
    parser->root->as.module.imports = bt_buffer_new(parser->context, sizeof(bt_ModuleImport*));
    parser->current_fn = NULL;

    push_scope(parser, BT_FALSE);

    while (bt_tokenizer_peek(parser->tokenizer))
    {
        bt_AstNode* expression = parse_statement(parser);
        if (expression) {
            bt_buffer_push(parser->context, &parser->root->as.module.body, &expression);
        }
    }

    pop_scope(parser);

    bt_debug_print_parse_tree(parser);

    return BT_TRUE;
}
