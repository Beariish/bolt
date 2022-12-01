#include "bt_parser.h"

#include "bt_context.h"
#include "bt_object.h"

#include <assert.h>

bt_Parser bt_open_parser(bt_Tokenizer* tkn)
{
    bt_Parser result;
    result.context = tkn->context;
    result.tokenizer = tkn;
    result.root = NULL;
    result.scope = NULL;

    return result;
}

static void push_scope(bt_Parser* parser)
{
    bt_ParseScope* new_scope = parser->context->alloc(sizeof(bt_ParseScope));
    new_scope->last = parser->scope;
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
    assert(node->type == BT_AST_NODE_LET);

    bt_ParseBinding new_binding;
    new_binding.is_const = node->as.let.is_const;
    new_binding.name = node->as.let.name;
    new_binding.type = node->resulting_type;

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

        current = current->last;
    }

    return NULL;
}

static bt_AstNode* make_node(bt_Context* ctx, bt_AstNodeType type)
{
    bt_AstNode* new_node = ctx->alloc(sizeof(bt_AstNode));
    new_node->type = type;
    new_node->resulting_type = NULL;

    return new_node;
}

static bt_AstNode* token_to_node(bt_Context* ctx, bt_Token* token)
{
    bt_AstNode* result = NULL;
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

    case BT_TOKEN_IDENTIFIER:
        result = make_node(ctx, BT_AST_NODE_IDENTIFIER);
        result->source = token;
        return result;
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
    case BT_TOKEN_PERIOD: case BT_TOKEN_QUESTION:
    case BT_TOKEN_LEFTBRACKET:
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
    case BT_TOKEN_QUESTION: return 15;
    case BT_TOKEN_LEFTBRACKET: return 16;
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
    case BT_TOKEN_MUL: case BT_TOKEN_DIV: return (InfixBindingPower) { 11, 12 };
    case BT_TOKEN_NULLCOALESCE: return (InfixBindingPower) { 13, 14 };
    case BT_TOKEN_PERIOD: return (InfixBindingPower) { 101, 100 };
    default: return (InfixBindingPower) { 0, 0 };
    }
}

static bt_Type* parse_type(bt_Tokenizer* tok)
{
    bt_Token* token = bt_tokenizer_emit(tok);
    bt_Context* ctx = tok->context;

    switch (token->type) {
    case BT_TOKEN_IDENTIFIER: {
        bt_String* name = bt_make_string_hashed_len(ctx, token->source.source, token->source.length);
        bt_Type* result = bt_find_type(ctx, BT_VALUE_STRING(name));
        if (!result) assert(0);

        token = bt_tokenizer_peek(tok);
        if (token->type == BT_TOKEN_QUESTION) {
            bt_tokenizer_emit(tok);
            result = bt_make_nullable(ctx, result);
        }

        return result;
    } break;
    default: assert(0);
    }
}

static bt_AstNode* pratt_parse(bt_Parser* parse, uint32_t min_binding_power)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_Token* lhs = bt_tokenizer_emit(tok);

    bt_AstNode* lhs_node;

    if (lhs->type == BT_TOKEN_LEFTPAREN) {
        lhs_node = pratt_parse(parse, 0);
        assert(bt_tokenizer_expect(tok, BT_TOKEN_RIGHTPAREN));
    }
    else if (prefix_binding_power(lhs)) {
        lhs_node = make_node(tok->context, BT_AST_NODE_UNARY_OP);
        lhs_node->source = lhs;
        lhs_node->as.unary_op.operand = pratt_parse(parse, prefix_binding_power(lhs));
    }
    else {
       lhs_node = token_to_node(tok->context, lhs);
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
            lhs_node->as.binary_op.left = lhs;
            lhs_node->as.binary_op.right = rhs;

            continue;
        }

        break;
    }
    
    return lhs_node;
}

static bt_AstNode* type_check(bt_Parser* parse, bt_AstNode* node)
{
    switch (node->type) {
    case BT_AST_NODE_IDENTIFIER: {
        bt_ParseBinding* binding = find_local(parse, node);
        if (binding) {
            node->resulting_type = binding->type;
        }
    } break;
    case BT_AST_NODE_LITERAL:
        if (node->resulting_type == NULL)
            assert(0);
        break;
    case BT_AST_NODE_UNARY_OP: {
        switch (node->source->type) {
        case BT_TOKEN_QUESTION:
            if (!type_check(parse, node->as.unary_op.operand)->resulting_type->is_optional) {
                assert(0 && "Unary operator ? can only be applied to nullable types.");
            }
            node->resulting_type = parse->context->types.boolean;
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
            }

            lhs = bt_remove_nullable(parse->context, lhs);

            if(!lhs->satisfier(node->resulting_type, lhs)) {
                assert(0 && "Unable to coalesce rhs into lhs");
            }
        } break;
        default:
            node->resulting_type = type_check(parse, node->as.binary_op.left)->resulting_type;
            if (!node->resulting_type->satisfier(node->resulting_type, type_check(parse, node->as.binary_op.right)->resulting_type)) {
                assert(0);
            }
            break;
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

        bt_Type* type = parse_type(tok);
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

static bt_AstNode* parse_statement(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_Token* token = bt_tokenizer_peek(tok);
    switch (token->type) {
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
    default: // no statment structure found, assume expression
        return pratt_parse(parse, 0);
    }
}

bt_bool bt_parse(bt_Parser* parser)
{
    parser->root = (bt_AstNode*)parser->context->alloc(sizeof(bt_AstNode));
    parser->root->type = BT_AST_NODE_MODULE;
    parser->root->as.module.body = bt_buffer_new(parser->context, sizeof(bt_AstNode*));

    push_scope(parser);

    while (bt_tokenizer_peek(parser->tokenizer))
    {
        bt_AstNode* expression = parse_statement(parser);
        bt_buffer_push(parser->context, &parser->root->as.module.body, 
            &expression);
    }

    pop_scope(parser);

    return BT_TRUE;
}
