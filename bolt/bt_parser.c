#include "bt_parser.h"

#include "bt_context.h"
#include "bt_object.h"
#include "bt_debug.h"
#include "bt_userdata.h"

#include <assert.h>
#include <memory.h>
#include <stdarg.h>
#include <stdio.h>

static void parse_block(bt_Buffer(bt_AstNode*)* result, bt_Parser* parse);
static void destroy_subobj(bt_Context* ctx, bt_AstNode* node);
static bt_AstNode* parse_statement(bt_Parser* parse);
static bt_AstNode* type_check(bt_Parser* parse, bt_AstNode* node);
static bt_AstNode* pratt_parse(bt_Parser* parse, uint32_t min_binding_power);
static bt_Type* find_binding(bt_Parser* parse, bt_AstNode* ident);

bt_Parser bt_open_parser(bt_Tokenizer* tkn)
{
    bt_Parser result;
    result.context = tkn->context;
    result.tokenizer = tkn;
    result.root = NULL;
    result.scope = NULL;
    result.current_pool = NULL;
    result.has_errored = BT_FALSE;

    return result;
}

static void parse_error(bt_Parser* parse, const char* message, uint16_t line, uint16_t col)
{
    parse->context->on_error(BT_ERROR_PARSE, parse->tokenizer->source_name, message, line, col);
    parse->has_errored = BT_TRUE;
}

static void parse_error_fmt(bt_Parser* parse, const char* format, uint16_t line, uint16_t col, ...)
{
    va_list va;
    va_start(va, col);

    char message[4096];
    message[vsprintf_s(message, sizeof(message), format, va)] = 0;
    va_end(va);

    parse_error(parse, message, line, col);
}

static void parse_error_token(bt_Parser* parse, const char* format, bt_Token* source)
{
    parse_error_fmt(parse, format, source->line, source->col, source->source.length, source->source.source);
}

void bt_close_parser(bt_Parser* parse)
{
    bt_AstNodePool* pool = parse->current_pool;

    while (pool) {
        for (uint32_t i = 0; i < pool->count; ++i) {
            destroy_subobj(parse->context, pool->nodes + i);
        }

        bt_AstNodePool* tmp = pool;
        pool = tmp->prev;
        parse->context->free(tmp);
    }
}

static void push_scope(bt_Parser* parser, bt_bool is_fn_boundary)
{
    bt_ParseScope* new_scope = parser->context->alloc(sizeof(bt_ParseScope));
    new_scope->last = parser->scope;
    new_scope->is_fn_boundary = is_fn_boundary;

    parser->scope = new_scope;

    bt_buffer_empty(&new_scope->bindings);
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
        char* name = parse->context->alloc(node->source->source.length + 1);
        memcpy(name, node->source->source.source, node->source->source.length);
        name[node->source->source.length] = 0;
        new_binding.type = bt_make_alias(parse->context, name, node->as.alias.type);;
    } break;
    case BT_AST_NODE_IF: {
        if (!node->as.branch.is_let) {
            parse_error_token(parse, "Expected local at '%.*s' to be within if-let statement", node->as.branch.identifier);
        }

        new_binding.is_const = BT_FALSE;
        new_binding.name = node->as.branch.identifier->source;
        new_binding.type = node->as.branch.bound_type;
    } break;
    default: parse_error_token(parse, "Internal parser error: Unexpected local at '%.*s'", node->source);
    }

    bt_ParseScope* topmost = parse->scope;
    for (uint32_t i = 0; i < topmost->bindings.length; ++i) {
        bt_ParseBinding* binding = topmost->bindings.elements + i;
        if (bt_strslice_compare(binding->name, new_binding.name)) {
            parse_error_token(parse, "Attempted to redefine binding '%.*s'", new_binding.source->source);
            return;
        }
    }

    bt_buffer_push(parse->context, &topmost->bindings, new_binding);
}

static void push_arg(bt_Parser* parse, bt_FnArg* arg, bt_Token* source) {
    bt_ParseBinding new_binding;
    new_binding.is_const = BT_FALSE;
    new_binding.name = arg->name;
    new_binding.type = arg->type;
    new_binding.source = 0;

    bt_ParseScope* topmost = parse->scope;
    for (uint32_t i = 0; i < topmost->bindings.length; ++i) {
        bt_ParseBinding* binding = topmost->bindings.elements + i;
        if (bt_strslice_compare(binding->name, new_binding.name)) {
            parse_error_fmt(parse, "Binding rededinition in function argument '%.*s'", source->line, source->col, arg->name.length, arg->name.source);
        }
    }

    bt_buffer_push(parse->context, &topmost->bindings, new_binding);
}

static bt_ParseBinding* find_local(bt_Parser* parse, bt_AstNode* identifier)
{
    if (identifier->type != BT_AST_NODE_IDENTIFIER) return NULL;

    bt_ParseScope* current = parse->scope;

    while (current) {
        for (uint32_t i = 0; i < current->bindings.length; ++i) {
            bt_ParseBinding* binding = current->bindings.elements + i;
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
            bt_ParseBinding* binding = current->bindings.elements + i;
            if (bt_strslice_compare(binding->name, identifier)) {
                return binding;
            }
        }

        current = current->is_fn_boundary ? NULL : current->last;
    }

    return NULL;
}

static bt_ParseBinding* find_local_exhaustive(bt_Parser* parse, bt_StrSlice identifier)
{
    bt_ParseScope* current = parse->scope;

    while (current) {
        for (uint32_t i = 0; i < current->bindings.length; ++i) {
            bt_ParseBinding* binding = current->bindings.elements + i;
            if (bt_strslice_compare(binding->name, identifier)) {
                return binding;
            }
        }

        current = current->last;
    }

    return NULL;
}

static bt_ModuleImport* find_import(bt_Parser* parser, bt_AstNode* identifier)
{
    bt_ImportBuffer* imports = &parser->root->as.module.imports;
    for (uint32_t i = 0; i < imports->length; ++i) {
        bt_ModuleImport* import = imports->elements[i];
        if (bt_strslice_compare(bt_as_strslice(import->name), identifier->source->source)) {
            identifier->type = BT_AST_NODE_IMPORT_REFERENCE;
            return import;
        }
    }

    // Import not found, _should_ we import from prelude?
    bt_Table* prelude = parser->context->prelude;
    for (uint32_t i = 0; i < prelude->length; ++i) {
        bt_ModuleImport* entry = BT_AS_OBJECT((BT_TABLE_PAIRS(prelude) + i)->value);

        if (bt_strslice_compare(bt_as_strslice(entry->name), identifier->source->source)) {
            bt_buffer_push(parser->context, imports, entry);
            identifier->type = BT_AST_NODE_IMPORT_REFERENCE;
            return bt_buffer_last(imports);
        }

    }

    return NULL;
}

static bt_ModuleImport* find_import_fast(bt_Parser* parser, bt_StrSlice identifier)
{
    bt_ImportBuffer* imports = &parser->root->as.module.imports;
    for (uint32_t i = 0; i < imports->length; ++i) {
        bt_ModuleImport* import = imports->elements[i];
        if (bt_strslice_compare(bt_as_strslice(import->name), identifier)) {
            return import;
        }
    }

    // Import not found, _should_ we import from prelude?
    bt_Table* prelude = parser->context->prelude;
    for (uint32_t i = 0; i < prelude->length; ++i) {
        bt_ModuleImport* entry = BT_AS_OBJECT((BT_TABLE_PAIRS(prelude) + i)->value);

        if (bt_strslice_compare(bt_as_strslice(entry->name), identifier)) {
            bt_buffer_push(parser->context, imports, entry);
            return bt_buffer_last(imports);
        }

    }

    return NULL;
}

static void next_pool(bt_Parser* parse)
{
    bt_AstNodePool* prev = parse->current_pool;
    parse->current_pool = parse->context->alloc(sizeof(bt_AstNodePool));
    parse->current_pool->prev = prev;
    parse->current_pool->count = 0;
}

static bt_AstNode* make_node(bt_Parser* parse, bt_AstNodeType type)
{
    if (!parse->current_pool || parse->current_pool->count == BT_AST_NODE_POOL_SIZE - 1) next_pool(parse);

    bt_AstNode* new_node = &parse->current_pool->nodes[parse->current_pool->count++];
    memset(new_node, 0, sizeof(bt_AstNode));
    new_node->type = type;
    new_node->resulting_type = NULL;
    new_node->source = 0;

    return new_node;
}

static void destroy_subobj(bt_Context* ctx, bt_AstNode* node)
{
    switch (node->type) {
    case BT_AST_NODE_MODULE: {
        bt_buffer_destroy(ctx, &node->as.module.body);
        bt_buffer_destroy(ctx, &node->as.module.imports);
    } break;

    case BT_AST_NODE_ARRAY: {
        bt_buffer_destroy(ctx, &node->as.arr.items);
    } break;

    case BT_AST_NODE_TABLE: {
        bt_buffer_destroy(ctx, &node->as.table.fields);
    } break;

    case BT_AST_NODE_FUNCTION: 
    case BT_AST_NODE_METHOD: {
        bt_buffer_destroy(ctx, &node->as.fn.args);
        bt_buffer_destroy(ctx, &node->as.fn.upvals);
        bt_buffer_destroy(ctx, &node->as.fn.body);
    } break;

    case BT_AST_NODE_IF: {
        bt_buffer_destroy(ctx, &node->as.branch.body);
    } break;

    case BT_AST_NODE_LOOP_WHILE: {
        bt_buffer_destroy(ctx, &node->as.loop_while.body);
    }

    case BT_AST_NODE_LOOP_ITERATOR: {
        bt_buffer_destroy(ctx, &node->as.loop_iterator.body);
    } break;

    case BT_AST_NODE_LOOP_NUMERIC: {
        bt_buffer_destroy(ctx, &node->as.loop_numeric.body);
    } break;

    case BT_AST_NODE_CALL: {
        bt_buffer_destroy(ctx, &node->as.call.args);
    } break;
    }
}

static bt_Value node_to_key(bt_Parser* parse, bt_AstNode* node)
{
    bt_Value result = BT_VALUE_NULL;

    switch (node->type) {
    case BT_AST_NODE_LITERAL: case BT_AST_NODE_IDENTIFIER: {
        switch (node->source->type) {
        case BT_TOKEN_IDENTIFER_LITERAL: case BT_TOKEN_IDENTIFIER: {
            result = BT_VALUE_OBJECT(bt_make_string_hashed_len(parse->context, node->source->source.source, node->source->source.length));
        } break;
        case BT_TOKEN_STRING_LITERAL: {
            // Cut off the quotes!
            result = BT_VALUE_OBJECT(bt_make_string_hashed_len(parse->context, node->source->source.source + 1, node->source->source.length - 2));
        } break;
        case BT_TOKEN_NUMBER_LITERAL: {
            bt_Literal* lit = parse->tokenizer->literals.elements + node->source->idx;
            result = BT_VALUE_NUMBER(lit->as_num);
        } break;
        case BT_TOKEN_TRUE_LITERAL: result = BT_VALUE_TRUE; break;
        case BT_TOKEN_FALSE_LITERAL: result = BT_VALUE_FALSE; break;
        case BT_TOKEN_NULL_LITERAL: result = BT_VALUE_NULL; break;
        default: parse_error_token(parse, "Internal parser error: Unhandled token literal type '%.*s'", node->source);
        }
    } break;

    case BT_AST_NODE_ENUM_LITERAL: {
        result = node->as.enum_literal.value;
    } break;

    default: parse_error_token(parse, "Failed to make table key from '%.*s'", node->source);
    }

    return result;
}

static bt_AstNode* parse_table(bt_Parser* parse, bt_Token* source, bt_Type* type) {
    bt_Token* token = source;
    bt_Context* ctx = parse->context;

    bt_AstNode* result = make_node(parse, BT_AST_NODE_TABLE);
    result->source = token;
    bt_buffer_empty(&result->as.table.fields);
    result->as.table.typed = type ? BT_TRUE : BT_FALSE;
    result->resulting_type = type ? type : bt_make_tableshape(ctx, "<anonymous>", BT_TRUE);

    uint32_t n_satisfied = 0;

    token = bt_tokenizer_peek(parse->tokenizer);
    while (token && token->type != BT_TOKEN_RIGHTBRACE) {
        bt_AstNode* key_expr = pratt_parse(parse, 0);

        bt_AstNode* field = make_node(parse, BT_AST_NODE_TABLE_ENTRY);

        bt_Value key = node_to_key(parse, key_expr);
        field->as.table_field.key = key;
        field->source = token;

        token = bt_tokenizer_emit(parse->tokenizer);
        if (token->type != BT_TOKEN_COLON) {
            parse_error_token(parse, "Expected colon after table field name, got '%.*s'", token);
        }

        bt_AstNode* value_expr = pratt_parse(parse, 0);
        field->as.table_field.value_expr = value_expr;
        field->as.table_field.value_type = type_check(parse, value_expr)->resulting_type;

        if (type) {
            bt_Type* expected = BT_AS_OBJECT(bt_table_get(type->as.table_shape.layout, key));
            if (!expected && type->as.table_shape.sealed) {
                parse_error_token(parse, "Unexpected field '%.*s' in sealed table literal", key_expr->source);
            }

            if (expected && !expected->satisfier(field->as.table_field.value_type, expected)) {
                parse_error_fmt(parse, "Invalid type for field '%.*s': wanted '%s', got '%s'", key_expr->source->line, key_expr->source->col,
                    key_expr->source->source.length, key_expr->source->source.source, expected->name, field->as.table_field.value_type->name);
            }

            n_satisfied++;
        }
        else {
            bt_Type* key_type = key_expr->type == BT_AST_NODE_IDENTIFIER ? ctx->types.string : type_check(parse, key_expr)->resulting_type;
            bt_tableshape_add_layout(ctx, result->resulting_type, key_type, key, BT_VALUE_OBJECT(field->as.table_field.value_type));
        }

        token = bt_tokenizer_peek(parse->tokenizer);
        if (token->type == BT_TOKEN_COMMA) {
            bt_tokenizer_emit(parse->tokenizer);
            token = bt_tokenizer_peek(parse->tokenizer);
        }

        bt_buffer_push(ctx, &result->as.table.fields, field);
    }

    bt_tokenizer_expect(parse->tokenizer, BT_TOKEN_RIGHTBRACE);

    if (type && n_satisfied != type->as.table_shape.layout->length) {
        // TODO: actually list missing fields
        parse_error_fmt(parse, "Missing %d fields in typed table literal", result->source->line, result->source->col, 
            type->as.table_shape.layout->length - n_satisfied);
    }

    return result;
}

static bt_AstNode* parse_array(bt_Parser* parse, bt_Token* source)
{
    bt_AstNode* result = make_node(parse, BT_AST_NODE_ARRAY);
    bt_buffer_empty(&result->as.arr.items);
    result->as.arr.inner_type = 0;
    result->source = source;

    bt_Tokenizer* tok = parse->tokenizer;

    bt_Token* next = bt_tokenizer_peek(tok);
    while (next && next->type != BT_TOKEN_RIGHTBRACKET) {
        bt_AstNode* expr = pratt_parse(parse, 0);
        bt_buffer_push(parse->context, &result->as.arr.items, expr);

        bt_Type* item_type = type_check(parse, expr)->resulting_type;
        if (result->as.arr.inner_type) {
            if (!result->as.arr.inner_type->satisfier(result->as.arr.inner_type, item_type)) {
                if (result->as.arr.inner_type->category != BT_TYPE_CATEGORY_UNION) {
                    bt_Type* old = result->as.arr.inner_type;
                    result->as.arr.inner_type = bt_make_union(parse->context);
                    bt_push_union_variant(parse->context, result->as.arr.inner_type, old);
                }

                bt_push_union_variant(parse->context, result->as.arr.inner_type, item_type);
            }
        }
        else {
            result->as.arr.inner_type = item_type;
        }

        next = bt_tokenizer_peek(tok);
        if (!next || !(next->type == BT_TOKEN_COMMA || next->type == BT_TOKEN_RIGHTBRACKET)) {
            assert(0 && "Malformed array literal!");
        }

        next = bt_tokenizer_emit(tok);
    }

    // empty array literal
    // I would really like this to infer the type from the binding on the lhs, but I 
    // don't think we really have the capability to do that yet
    if (result->as.arr.inner_type == 0) {

    }

    result->resulting_type = bt_make_array_type(parse->context, result->as.arr.inner_type);

    return result;
}

static bt_AstNode* token_to_node(bt_Parser* parse, bt_Token* token)
{
    bt_AstNode* result = NULL;
    bt_Context* ctx = parse->context;
    switch (token->type)
    {
    case BT_TOKEN_TRUE_LITERAL:
    case BT_TOKEN_FALSE_LITERAL:
        result = make_node(parse, BT_AST_NODE_LITERAL);
        result->source = token;
        result->resulting_type = ctx->types.boolean;
        return result;

    case BT_TOKEN_STRING_LITERAL:
        result = make_node(parse, BT_AST_NODE_LITERAL);
        result->source = token;
        result->resulting_type = ctx->types.string;
        return result;

    case BT_TOKEN_NUMBER_LITERAL:
        result = make_node(parse, BT_AST_NODE_LITERAL);
        result->source = token;
        result->resulting_type = ctx->types.number;
        return result;

    case BT_TOKEN_NULL_LITERAL:
        result = make_node(parse, BT_AST_NODE_LITERAL);
        result->source = token;
        result->resulting_type = ctx->types.null;
        return result;

    case BT_TOKEN_IDENTIFIER: {
        result = make_node(parse, BT_AST_NODE_IDENTIFIER);
        result->source = token;
        return result;
    } break;

    case BT_TOKEN_LEFTBRACE: {
        return parse_table(parse, token, NULL);
    } break;

    case BT_TOKEN_LEFTBRACKET: {
        return parse_array(parse, token);
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
    case BT_TOKEN_IS: case BT_TOKEN_AS:
    case BT_TOKEN_FATARROW: case BT_TOKEN_COMPOSE:
    case BT_TOKEN_SATISFIES:
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
    case BT_TOKEN_BANG: return 10;
    case BT_TOKEN_LEFTPAREN: return 19;
    case BT_TOKEN_QUESTION: return 15;
    case BT_TOKEN_LEFTBRACKET: return 17;
    case BT_TOKEN_FATARROW: return 18;
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
    case BT_TOKEN_LT: case BT_TOKEN_LTE:
    case BT_TOKEN_GT: case BT_TOKEN_GTE:
        return (InfixBindingPower) { 9, 10 };

    case BT_TOKEN_NULLCOALESCE: return (InfixBindingPower) { 11, 12 };
    case BT_TOKEN_IS: case BT_TOKEN_AS: case BT_TOKEN_SATISFIES: return (InfixBindingPower) { 13, 14 };
    case BT_TOKEN_PLUS: case BT_TOKEN_MINUS: return (InfixBindingPower) { 15, 16 };
    case BT_TOKEN_MUL: case BT_TOKEN_DIV: return (InfixBindingPower) { 17, 18 };
    case BT_TOKEN_PERIOD: return (InfixBindingPower) { 19, 20 };
    case BT_TOKEN_COMPOSE: return (InfixBindingPower) { 21, 22 };
    default: return (InfixBindingPower) { 0, 0 };
    }
}

static bt_Type* resolve_type_identifier(bt_Parser* parse, bt_Token* identifier)
{
    if (identifier->type != BT_TOKEN_IDENTIFIER) {
        assert(0 && "Expected identifier to be valid!");
    }

    bt_ParseBinding* binding = find_local_exhaustive(parse, identifier->source);

    bt_Type* result = 0;
    if (binding) {
        if (binding->source->resulting_type != parse->context->types.type) {
            assert(0 && "Type identifier didn't resolve to type!");
        }

        result = binding->source->as.alias.type;
    }

    if (!result) {
        bt_ModuleImport* import = find_import_fast(parse, identifier->source);
        if (import) {
            if (import->type->category != BT_TYPE_CATEGORY_TYPE) {
                assert(0 && "Type identifier didn't resolve to type!");
            }

            result = BT_AS_OBJECT(import->value);
        }
    }

    if (!result) {
        bt_String* name = bt_make_string_hashed_len(parse->context, identifier->source.source, identifier->source.length);
        result = bt_find_type(parse->context, BT_VALUE_OBJECT(name));
    }

    return result;
}

static bt_Type* find_type_or_shadow(bt_Parser* parse, bt_Token* identifier)
{
    if (identifier->type != BT_TOKEN_IDENTIFIER) {
        assert(0 && "Expected identifier to be valid!");
    }

    bt_ParseBinding* binding = find_local_exhaustive(parse, identifier->source);

    bt_Type* result = 0;
    if (binding) {
        if (binding->source->resulting_type != parse->context->types.type) {
            assert(0 && "Type identifier didn't resolve to type!");
        }

        result = binding->source->as.alias.type;
    }

    if (!result) {
        bt_ModuleImport* import = find_import_fast(parse, identifier->source);
        if (import) {
            if (import->type->category == BT_TYPE_CATEGORY_TYPE) {
                result = BT_AS_OBJECT(import->value);
            }
        }
    }

    if (!result) {
        bt_String* name = bt_make_string_hashed_len(parse->context, identifier->source.source, identifier->source.length);
        result = bt_find_type(parse->context, BT_VALUE_OBJECT(name));
    }

    return result;
}

static bt_Type* parse_type(bt_Parser* parse, bt_bool recurse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_Token* token = bt_tokenizer_emit(tok);
    bt_Context* ctx = tok->context;
    bt_bool is_sealed = BT_TRUE;
    bt_bool is_final = BT_FALSE;

    switch (token->type) {
    case BT_TOKEN_NULL_LITERAL: {
        return parse->context->types.null;
    }
    case BT_TOKEN_IDENTIFIER: {
        bt_Type* result = resolve_type_identifier(parse, token);

        token = bt_tokenizer_peek(tok);
        if (token->type == BT_TOKEN_QUESTION) {
            bt_tokenizer_emit(tok);
            result = bt_make_nullable(ctx, result);
        }
        else if (token->type == BT_TOKEN_PLUS) {
            bt_tokenizer_emit(tok);
            bt_Type* rhs = parse_type(parse, BT_FALSE);

            if (result->category != BT_TYPE_CATEGORY_TABLESHAPE || rhs->category != BT_TYPE_CATEGORY_TABLESHAPE) {
                assert(0 && "Type composition must be done between table types!");
            }

            bt_Type* lhs = result;
            result = bt_make_tableshape(ctx, "?", rhs->as.table_shape.sealed && lhs->as.table_shape.sealed);

            bt_Table* lhs_fields = lhs->as.table_shape.layout;
            bt_Table* lhs_field_types = lhs->as.table_shape.key_layout;
            bt_Table* rhs_fields = rhs->as.table_shape.layout;
            bt_Table* rhs_field_types = rhs->as.table_shape.key_layout;

            for (uint32_t i = 0; i < lhs_fields->length; ++i) {
                bt_TablePair* field = BT_TABLE_PAIRS(lhs_fields) + i;
                bt_TablePair* type = BT_TABLE_PAIRS(lhs_field_types) + i;
                bt_tableshape_add_layout(parse->context, result, BT_AS_OBJECT(type->value), field->key, BT_AS_OBJECT(field->value));
            }

            for (uint32_t i = 0; i < rhs_fields->length; ++i) {
                bt_TablePair* field = BT_TABLE_PAIRS(rhs_fields) + i;
                bt_TablePair* type = BT_TABLE_PAIRS(rhs_field_types) + i;

                if (bt_table_get(result->as.table_shape.layout, field->key) != BT_VALUE_NULL) {
                    assert(0 && "Both lhs and rhs have a feild with name %s!");
                    break;
                }

                bt_tableshape_add_layout(parse->context, result, BT_AS_OBJECT(type->value), field->key, BT_AS_OBJECT(field->value));
            }

            bt_tableshape_set_parent(parse->context, result, lhs);
        }
        else if (token->type == BT_TOKEN_UNION && recurse) {
            bt_Type* selector = bt_make_union(ctx);
            bt_push_union_variant(ctx, selector, result);
            
            while (token->type == BT_TOKEN_UNION) {
                bt_tokenizer_emit(tok);
                bt_push_union_variant(ctx, selector, parse_type(parse, BT_FALSE));
                
                token = bt_tokenizer_peek(tok);
            }

            result = selector;
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
                args[arg_top++] = parse_type(parse, BT_TRUE);
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
            return_type = parse_type(parse, BT_TRUE);
        }

        return bt_make_signature(ctx, return_type, args, arg_top);
    } break;
    case BT_TOKEN_FINAL: 
        is_final = BT_TRUE;
        if (!bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE)) {
            assert(0 && "Expected opening brace to follow keyword 'final'");
        }
        goto parse_table;
    case BT_TOKEN_UNSEALED:
        is_sealed = BT_FALSE;
        if (!bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE)) {
            assert(0 && "Expected opening brace to follow keyword 'unsealed'");
        }
    case BT_TOKEN_LEFTBRACE: parse_table: {
        token = bt_tokenizer_peek(tok);
        if (token->type == BT_TOKEN_RIGHTBRACE) {
            bt_tokenizer_emit(tok);
            return ctx->types.table;
        }

        if (token->type == BT_TOKEN_VARARG) {
            bt_tokenizer_emit(tok);

            bt_Type* key_type = parse_type(parse, BT_FALSE);
            bt_tokenizer_expect(tok, BT_TOKEN_COLON);
            bt_Type* value_type = parse_type(parse, BT_FALSE);

            bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);

            return bt_make_map(parse->context, key_type, bt_make_nullable(parse->context, value_type));
        }

        bt_Type* result = bt_make_tableshape(ctx, "<tableshape>", is_sealed);
        result->as.table_shape.final = is_final;
        while (token && token->type != BT_TOKEN_RIGHTBRACE) {
            token = bt_tokenizer_emit(tok);
            if (token->type != BT_TOKEN_IDENTIFIER) {
                assert(0 && "Expected identifier name for tableshape field!");
            }
    
            bt_String* name = bt_make_string_hashed_len(ctx, token->source.source, token->source.length);
            bt_Type* type = 0;

            token = bt_tokenizer_peek(tok);
            if (token->type == BT_TOKEN_COLON) {
                bt_tokenizer_emit(tok);
                type = parse_type(parse, BT_TRUE);
                token = bt_tokenizer_peek(tok);
            }

            if (token->type == BT_TOKEN_ASSIGN) {
                bt_tokenizer_emit(tok);
                bt_AstNode* expr = pratt_parse(parse, 0);
                if(type && !type_check(parse, expr)->resulting_type->satisfier(expr->resulting_type, type)) {
                    assert(0 && "Table value initializer doesn't match annotated type!");
                }

                type = expr->resulting_type;
                bt_type_add_field(ctx, result, BT_VALUE_OBJECT(name), BT_VALUE_OBJECT(expr), type);
            }

            bt_tableshape_add_layout(ctx, result, ctx->types.string, BT_VALUE_OBJECT(name), BT_AS_OBJECT(type));

            token = bt_tokenizer_peek(tok);
            if (token->type == BT_TOKEN_COMMA) {
                bt_tokenizer_emit(tok);
                token = bt_tokenizer_peek(tok);
            }
        }

        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);
        return result;
    } break;
    case BT_TOKEN_LEFTBRACKET: {
        token = bt_tokenizer_peek(tok);
        if (token->type == BT_TOKEN_RIGHTBRACKET) {
            bt_tokenizer_emit(tok);
            return ctx->types.array;
        }

        bt_Type* inner = parse_type(parse, BT_TRUE);
        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACKET);
        return bt_make_array_type(parse->context, inner);
    } break;
    case BT_TOKEN_ENUM: {
        bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);

        bt_Type* result = bt_make_enum(parse->context, (bt_StrSlice) { "<enum>", 6 });
        
        uint32_t option_idx = 0;
        while (bt_tokenizer_peek(tok)->type == BT_TOKEN_IDENTIFIER) {
            bt_Token* name = bt_tokenizer_emit(tok);
            
            bt_enum_push_option(parse->context, result, name->source, BT_VALUE_ENUM(option_idx));
            option_idx++;

            if (bt_tokenizer_peek(tok)->type == BT_TOKEN_COMMA) {
                bt_tokenizer_emit(tok);
            }
        }

        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);

        return result;
    } break;
    case BT_TOKEN_TYPEOF: {
        bt_AstNode* inner = pratt_parse(parse, 0);
        bt_Type* result = type_check(parse, inner)->resulting_type;

        if (!result) assert(0 && "Expression did not evaluate to type!");

        return bt_type_dealias(result);
    } break;
    default: assert(0);
    }

    return NULL;
}

static bt_Type* infer_return(bt_Context* ctx, bt_Buffer(bt_AstNode*)* body, bt_Type* expected)
{
    for (uint32_t i = 0; i < body->length; ++i) {
        bt_AstNode* expr = body->elements[i];
        if (!expr) continue;

        if (expr->type == BT_AST_NODE_RETURN) {
            if (expected && expr->resulting_type == NULL) {
                assert(0 && "Expected block to return value!");
            }

            if (!expected && expr) {
                expected = expr->resulting_type;
            }

            if (expected && !expected->satisfier(expected, expr->resulting_type)) {
                if (expected->category != BT_TYPE_CATEGORY_UNION) {
                    bt_Type* new_union = bt_make_union(ctx);
                    bt_push_union_variant(ctx, new_union, expected);
                    expected = new_union;
                }

                bt_push_union_variant(ctx, expected, expr->resulting_type);
            }
        }
        else if (expr->type == BT_AST_NODE_IF) {
            expected = infer_return(ctx, &expr->as.branch.body, expected);
            bt_AstNode* elif = expr->as.branch.next;
            while (elif) {
                expected = infer_return(ctx, &elif->as.branch.body, expected);
                elif = elif->as.branch.next;
            }
        }
    }

    return expected;
}

static bt_AstNode* parse_function_literal(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;

    bt_AstNode* result = make_node(parse, BT_AST_NODE_FUNCTION);
    result->source = bt_tokenizer_peek(parse->tokenizer);
    bt_buffer_empty(&result->as.fn.args);
    bt_buffer_with_capacity(&result->as.fn.body, parse->context, 8);
    result->as.fn.ret_type = NULL;
    result->as.fn.outer = parse->current_fn;
    bt_buffer_empty(&result->as.fn.upvals);

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
                this_arg.type = parse_type(parse, BT_TRUE);
            }
            else {
                this_arg.type = parse->context->types.any;
            }


            bt_buffer_push(parse->context, &result->as.fn.args, this_arg);

            next = bt_tokenizer_emit(tok);
        } while (next && next->type == BT_TOKEN_COMMA);
    }

    if (has_param_list && (!next || next->type != BT_TOKEN_RIGHTPAREN)) {
        assert(0 && "Cannot find end of parameter list!");
    }

    next = bt_tokenizer_peek(tok);
    
    if (next->type == BT_TOKEN_COLON) {
        next = bt_tokenizer_emit(tok);
        result->as.fn.ret_type = parse_type(parse, BT_TRUE);
    }

    next = bt_tokenizer_emit(tok);

    if (next->type == BT_TOKEN_LEFTBRACE) {
        push_scope(parse, BT_TRUE);

        for (uint8_t i = 0; i < result->as.fn.args.length; i++) {
            push_arg(parse, result->as.fn.args.elements + i, result->source);
        }

        parse_block(&result->as.fn.body, parse);
        
        pop_scope(parse);
    }
    else {
        assert(0 && "Found function without body!");
    }

    result->as.fn.ret_type = infer_return(parse->context, &result->as.fn.body, result->as.fn.ret_type);
    
    next = bt_tokenizer_emit(tok);
    if (next->type != BT_TOKEN_RIGHTBRACE) {
        assert(0 && "Expected end of function!");
    }

    bt_Type* args[16];

    for (uint8_t i = 0; i < result->as.fn.args.length; ++i) {
        args[i] = result->as.fn.args.elements[i].type;
    }

    result->resulting_type = bt_make_signature(parse->context, result->as.fn.ret_type, args, result->as.fn.args.length);

    parse->current_fn = parse->current_fn->as.fn.outer;

    return result;
}

static void parse_block(bt_Buffer(bt_AstNode*)* result, bt_Parser* parse)
{
    push_scope(parse, BT_FALSE);

    bt_Token* next = bt_tokenizer_peek(parse->tokenizer);

    while (next->type != BT_TOKEN_RIGHTBRACE)
    {
        bt_AstNode* expression = parse_statement(parse);
        bt_buffer_push(parse->context, result, expression);
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
        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTPAREN);
    }
    else if (lhs->type == BT_TOKEN_TYPEOF) {
        bt_tokenizer_expect(tok, BT_TOKEN_LEFTPAREN);
        bt_AstNode* inner = pratt_parse(parse, 0);
        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTPAREN);

        bt_Type* result = type_check(parse, inner)->resulting_type;

        if (!result) assert(0 && "Expression did not evaluate to type!");

        lhs_node = make_node(parse, BT_AST_NODE_TYPE);
        lhs_node->source = inner->source;
        lhs_node->resulting_type = bt_make_alias(parse->context, result->name, result);
    }
    else if (lhs->type == BT_TOKEN_TYPE) {
        bt_tokenizer_expect(tok, BT_TOKEN_LEFTPAREN);
        bt_Type* inner = parse_type(parse, BT_TRUE);
        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTPAREN);

        lhs_node = make_node(parse, BT_AST_NODE_TYPE);
        lhs_node->source = lhs;
        lhs_node->resulting_type = bt_make_alias(parse->context, inner->name, inner);
    }
    else if (prefix_binding_power(lhs)) {
        lhs_node = make_node(parse, BT_AST_NODE_UNARY_OP);
        lhs_node->source = lhs;
        lhs_node->as.unary_op.accelerated = BT_FALSE;
        lhs_node->as.unary_op.operand = pratt_parse(parse, prefix_binding_power(lhs));
    }
    else {
        lhs_node = token_to_node(parse, lhs);
    }
    
    for (;;) {
        bt_Token* op = bt_tokenizer_peek(tok);

        // end of file is end of expression, two non-operators following each other is also an expression bound
        if (op->type == BT_TOKEN_EOS || !is_operator(op)) break;

        uint8_t post_bp = postfix_binding_power(op);
        if (post_bp) {
            if (post_bp < min_binding_power) break;
            bt_tokenizer_emit(tok); // consume peeked operator

            if (op->type == BT_TOKEN_LEFTBRACKET)
            {
                bt_AstNode* rhs = pratt_parse(parse, 0);
                bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACKET);
                
                bt_AstNode* lhs = lhs_node;
                lhs_node = make_node(parse, BT_AST_NODE_BINARY_OP);
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
                uint8_t self_arg = 0;

                if (to_call->as.fn.is_method) {
                    if (lhs_node->type == BT_AST_NODE_BINARY_OP && lhs_node->source->type == BT_TOKEN_PERIOD) {
                        if (!to_call->is_polymorphic) {
                            bt_TypeBuffer* args_ref = &to_call->as.fn.args;
                            bt_Type* first_arg = args_ref->elements[0];

                            bt_Type* lhs_type = type_check(parse, lhs_node->as.binary_op.left)->resulting_type;
                            if (first_arg->satisfier(first_arg, lhs_type)) {
                                args[max_arg++] = lhs_node->as.binary_op.left;
                                self_arg = 1;
                            }
                        }
                        else {
                            args[max_arg++] = lhs_node->as.binary_op.left;
                            self_arg = 1;
                        }
                    }
                }

                bt_Token* next = bt_tokenizer_peek(tok);
                while (next && next->type != BT_TOKEN_RIGHTPAREN) {
                    args[max_arg++] = pratt_parse(parse, 0);
                    next = bt_tokenizer_emit(tok);

                    if (!next || (next->type != BT_TOKEN_COMMA && next->type != BT_TOKEN_RIGHTPAREN)) {
                        assert(0 && "Invalid token in parameter list!");
                    }
                }

                if (max_arg == 0 || (self_arg && max_arg == 1)) bt_tokenizer_emit(tok);

                if (!next || next->type != BT_TOKEN_RIGHTPAREN) {
                    assert(0 && "Couldn't find end of function call!");
                }

                if (to_call->is_polymorphic) {
                    bt_Type* arg_types[16];
                    for (uint8_t i = 0; i < max_arg; ++i) {
                        arg_types[i] = type_check(parse, args[i])->resulting_type;
                    }

                    bt_Type* old_to_call = to_call;
                    to_call = to_call->as.poly_fn.applicator(parse->context, arg_types, max_arg);
                    if (!to_call) {
                        if (self_arg) {
                            // If we have a self arg and fail to polymorphize, let's discard self and try with the remaining args.
                            for (uint8_t i = 0; i < max_arg; i++) {
                                args[i] = args[i + 1];
                                arg_types[i] = arg_types[i + 1];
                            }
                            max_arg--;

                            to_call = old_to_call->as.poly_fn.applicator(parse->context, arg_types, max_arg);

                            if (!to_call) {
                                assert(0 && "Found no polymorhic mode for function!");
                            }
                        }
                        else {
                            assert(0 && "Found no polymorhic mode for function!");
                        }
                    }
                }

                if (max_arg != to_call->as.fn.args.length && to_call->as.fn.is_vararg == BT_FALSE) {
                    assert(0 && "Incorrect number of arguments!");
                }

                bt_AstNode* call = make_node(parse, BT_AST_NODE_CALL);
                call->source = lhs_node->source;
                call->as.call.fn = lhs_node;
                bt_buffer_with_capacity(&call->as.call.args, parse->context, max_arg);
                
                for (uint8_t i = 0; i < max_arg; i++) {
                    bt_Type* arg_type = type_check(parse, args[i])->resulting_type;
                    
                    if (i < to_call->as.fn.args.length) {
                        bt_Type* fn_type = to_call->as.fn.args.elements[i];
                        if (!fn_type->satisfier(fn_type, arg_type)) {
                            assert(0 && "Invalid argument type!");
                        }
                        else {
                            bt_buffer_push(parse->context, &call->as.call.args, args[i]);
                        }
                    }
                    else {
                        if (!to_call->as.fn.varargs_type->satisfier(to_call->as.fn.varargs_type, arg_type)) {
                            assert(0 && "Arg doesn't match typed vararg");
                        }
                        else {
                            bt_buffer_push(parse->context, &call->as.call.args, args[i]);
                        }
                    }
                }

                call->resulting_type = to_call->as.fn.return_type;
                lhs_node = call;
            }
            else if (op->type == BT_TOKEN_FATARROW) {
                if (lhs_node->type != BT_AST_NODE_IDENTIFIER) {
                    assert(0 && "Expected identifier before typed table literal!");
                }

                bt_Type* type = find_binding(parse, lhs_node);
                if (type->category == BT_TYPE_CATEGORY_TYPE) type = type->as.type.boxed;

                if (!type) assert(0 && "Failed to find type for table literal!");

                bt_Token* next = bt_tokenizer_emit(tok);
                if (next->type != BT_TOKEN_LEFTBRACE) {
                    assert(0 && "Expected table literal to follow!");
                }

                lhs_node = parse_table(parse, next, type);
            }
            else
            {
                bt_AstNode* lhs = lhs_node;
                lhs_node = make_node(parse, BT_AST_NODE_UNARY_OP);
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
            lhs_node = make_node(parse, BT_AST_NODE_BINARY_OP);
            lhs_node->source = op;
            lhs_node->as.binary_op.accelerated = BT_FALSE;
            lhs_node->as.binary_op.hoistable = BT_FALSE;
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
    for (uint32_t i = 0; i < fn->as.fn.upvals.length; ++i) {
        bt_ParseBinding* binding = fn->as.fn.upvals.elements + i;
        if (bt_strslice_compare(binding->name, upval->name)) {
            return;
        }
    }

    bt_buffer_push(parse->context, &fn->as.fn.upvals, *upval);
}

static bt_ParseBinding* find_upval(bt_Parser* parse, bt_AstNode* ident)
{
    bt_AstNode* fn = parse->current_fn;

    if (!fn) return NULL;

    for (uint32_t i = 0; i < fn->as.fn.upvals.length; ++i) {
        bt_ParseBinding* binding = fn->as.fn.upvals.elements + i;
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
            bt_ParseBinding* binding = scope->bindings.elements + i;
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
    if (import) { return import->type; }

    return NULL;
}

static bt_AstNode* type_check(bt_Parser* parse, bt_AstNode* node)
{
    if (node->resulting_type) {
        return node;
    }

    switch (node->type) {
    case BT_AST_NODE_IDENTIFIER: {
        bt_Type* type = find_binding(parse, node);
        if (type) {
            node->resulting_type = type;
        }
        else {
            node->resulting_type = NULL;
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
            if (!bt_is_optional(type_check(parse, node->as.unary_op.operand)->resulting_type)) {
                assert(0 && "Unary operator ? can only be applied to nullable types.");
                return NULL;
            }
            node->resulting_type = parse->context->types.boolean;
            break;
        case BT_TOKEN_BANG:
            if (!bt_is_optional(type_check(parse, node->as.unary_op.operand)->resulting_type)) {
                assert(0 && "Unary operator ! can only be applied to nullable types.");
                return NULL;
            }
            node->resulting_type = bt_remove_nullable(parse->context, node->as.unary_op.operand->resulting_type);
            break;
        case BT_TOKEN_MINUS:
            if (type_check(parse, node->as.unary_op.operand)->resulting_type == parse->context->types.number) {
                node->as.unary_op.accelerated = BT_TRUE;
            }
            node->resulting_type = node->as.unary_op.operand->resulting_type;
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

            if (!bt_is_optional(lhs)) {
                assert(0 && "Lhs is non-optional, cannot coalesce!");
                return NULL;
            }

            lhs = bt_remove_nullable(parse->context, lhs);

            if(!lhs->satisfier(node->resulting_type, lhs)) {
                assert(0 && "Unable to coalesce rhs into lhs");
                return NULL;
            }
        } break;
        case BT_TOKEN_LEFTBRACKET: {
            node->source->type = BT_TOKEN_PERIOD;
            bt_Type* lhs = type_check(parse, node->as.binary_op.left)->resulting_type;
            if (lhs->category == BT_TYPE_CATEGORY_ARRAY) {
                bt_Type* rhs = type_check(parse, node->as.binary_op.right)->resulting_type;
                if (!(rhs == parse->context->types.number || rhs == parse->context->types.any)) {
                    assert(0 && "Invalid index type!");
                }

                node->resulting_type = lhs->as.array.inner;
                if (rhs == parse->context->types.number) {
                    node->as.binary_op.accelerated = BT_TRUE;
                }

                return node;
            }
        }
        case BT_TOKEN_PERIOD: {
            // dot syntax acceleration
            if (node->as.binary_op.right->type == BT_AST_NODE_IDENTIFIER) {
                node->as.binary_op.right->type = BT_AST_NODE_LITERAL;
                node->as.binary_op.right->resulting_type = parse->context->types.string;
                node->as.binary_op.right->source->type = BT_TOKEN_IDENTIFER_LITERAL;
            }

            // TODO(bearish): Make a smarter check than just comparing against literal table type
            bt_Type* lhs = bt_type_dealias(type_check(parse, node->as.binary_op.left)->resulting_type);
            if (lhs == parse->context->types.table) {
                node->resulting_type = parse->context->types.any;
                return node;
            }

            bt_Value rhs_key = node_to_key(parse, node->as.binary_op.right);

            bt_Table* proto = lhs->prototype_types;
            if (!proto && lhs->prototype) proto = lhs->prototype->prototype_types;
            if (proto) {
                bt_Value proto_entry = bt_table_get(proto, rhs_key);
                if (proto_entry != BT_VALUE_NULL) {
                    bt_Type* entry = BT_AS_OBJECT(proto_entry);
                    node->resulting_type = entry;

                    if (lhs->as.table_shape.final) {
                        node->as.binary_op.hoistable = BT_TRUE;
                        node->as.binary_op.from = lhs;
                        node->as.binary_op.key = rhs_key;
                    }

                    return node;
                }
            }

            if (lhs->category == BT_TYPE_CATEGORY_TABLESHAPE) {
                if (lhs->as.table_shape.map) {
                    if (!lhs->as.table_shape.key_type->satisfier(lhs->as.table_shape.key_type, type_check(parse, node->as.binary_op.right)->resulting_type)) {
                        assert(0 && "Key type mismatch!");
                    }

                    node->resulting_type = lhs->as.table_shape.value_type;
                    return node;
                }

                bt_Table* layout = lhs->as.table_shape.layout;
                bt_Value table_entry = bt_table_get(layout, rhs_key);
                if (table_entry != BT_VALUE_NULL) {
                    bt_Type* type = BT_AS_OBJECT(table_entry);
                    node->resulting_type = type;

                    if (lhs->as.table_shape.sealed) {
                        int16_t as_idx = bt_table_get_idx(layout, rhs_key);
                        if (as_idx != -1 && as_idx < UINT8_MAX) {
                            node->as.binary_op.accelerated = BT_TRUE;
                            node->as.binary_op.idx = as_idx;
                        }
                    }

                    return node;
                }

                if (lhs->as.table_shape.sealed) {
                    assert(0 && "Couldn't find item in table shape.");
                }

                node->resulting_type = parse->context->types.any;
                return node;
            }
            else if (lhs->category == BT_TYPE_CATEGORY_USERDATA) {
                bt_FieldBuffer* fields = &lhs->as.userdata.fields;

                for (uint32_t i = 0; i < fields->length; i++) {
                    bt_UserdataField* field = fields->elements + i;
                    if (bt_value_is_equal(BT_VALUE_OBJECT(field->name), rhs_key)) {
                        node->resulting_type = field->bolt_type;
                        return node;
                    }
                }

                bt_MethodBuffer* methods = &lhs->as.userdata.functions;

                for (uint32_t i = 0; i < methods->length; i++) {
                    bt_UserdataMethod* method = methods->elements + i;
                    if (bt_value_is_equal(BT_VALUE_OBJECT(method->name), rhs_key)) {
                        node->resulting_type = method->fn->type;
                        return node;
                    }
                }

                assert(0 && "Field not found in userdata type!");
            }
            else if (lhs->category == BT_TYPE_CATEGORY_ENUM) {
                bt_String* as_str = BT_AS_OBJECT(rhs_key);
                bt_Value result = bt_enum_get(parse->context, lhs, as_str);
                if (result == BT_VALUE_NULL) assert(0 && "Invalid enum option!");
                node->type = BT_AST_NODE_ENUM_LITERAL;
                node->as.enum_literal.value = result;
                node->resulting_type = lhs;
                return node;
            }
            else {
                assert(0 && "lhs is unindexable type");
            }
        } break;
        case BT_TOKEN_IS: {
            if (type_check(parse, node->as.binary_op.right)->resulting_type->category != BT_TYPE_CATEGORY_TYPE)
                assert(0 && "Expected right hand of 'is' to be Type!");
            node->resulting_type = parse->context->types.boolean;
        } break;
        case BT_TOKEN_SATISFIES: {
            if (type_check(parse, node->as.binary_op.right)->resulting_type->category != BT_TYPE_CATEGORY_TYPE)
                assert(0 && "Expected right hand of 'satisfies' to be Type!");
            node->resulting_type = parse->context->types.boolean;
        } break;
        case BT_TOKEN_AS: {
            bt_Type* from = type_check(parse, node->as.binary_op.left)->resulting_type;
            
            /*
            if (from != parse->context->types.any && from->category != BT_TYPE_CATEGORY_TABLESHAPE) {
                assert(0 && "Invalid source type for casting!");
            }*/
            
            if (type_check(parse, node->as.binary_op.right)->resulting_type->category != BT_TYPE_CATEGORY_TYPE)
                assert(0 && "Expected right hand of 'as' to be Type!");
            bt_Type* type = find_binding(parse, node->as.binary_op.right);

            bt_Type* to = type->as.type.boxed;

            if (from->category == BT_TYPE_CATEGORY_TABLESHAPE && to->category == BT_TYPE_CATEGORY_TABLESHAPE) {
                if (to->as.table_shape.sealed && from->as.table_shape.layout->length != to->as.table_shape.layout->length) {
                    assert(0 && "Lhs has too many fields to conform to rhs.");
                }

                node->as.binary_op.accelerated = 1;

                bt_Table* lhs = from->as.table_shape.layout;
                bt_Table* rhs = to->as.table_shape.layout;
                
                for (uint32_t i = 0; i < lhs->length; ++i) {
                    bt_TablePair* current = BT_TABLE_PAIRS(lhs) + i;
                    bt_bool found = BT_FALSE;

                    for (uint32_t j = 0; j < rhs->length; j++) {
                        bt_TablePair* inner = BT_TABLE_PAIRS(rhs) + j;
                        if (bt_value_is_equal(inner->key, current->key)) {
                            found = BT_TRUE;
                            bt_Type* left = BT_AS_OBJECT(current->value);
                            bt_Type* right = BT_AS_OBJECT(inner->value);
                            if (!right->satisfier(right, left)) {
                                assert(0 && "Type of field failed to satisfy!");
                            }

                            if (i != j) node->as.binary_op.accelerated = 0;
                        }
                    }

                    if (!found && from->as.table_shape.sealed) {
                        assert(0 && "Failed to find field in tablehape!");
                    }
                }
            }

            node->resulting_type = bt_make_nullable(parse->context, type->as.type.boxed);
        } break;
        case BT_TOKEN_COMPOSE: {
            bt_Type* lhs = type_check(parse, node->as.binary_op.left)->resulting_type;
            bt_Type* rhs = type_check(parse, node->as.binary_op.right)->resulting_type;

            if (lhs->category != BT_TYPE_CATEGORY_TABLESHAPE || rhs->category != BT_TYPE_CATEGORY_TABLESHAPE) {
                assert(0 && "Operator compose '&' takes two known tableshapes.");
            }

            if (!lhs->as.table_shape.sealed || !rhs->as.table_shape.sealed) {
                assert(0 && "Operator compose '&' requires operands to be sealed types.");
            }

            bt_Table* lhs_fields = lhs->as.table_shape.layout;
            bt_Table* lhs_field_types = lhs->as.table_shape.key_layout;
            bt_Table* rhs_fields = rhs->as.table_shape.layout;
            bt_Table* rhs_field_types = rhs->as.table_shape.key_layout;

            bt_Type* resulting_type = bt_make_tableshape(parse->context, "", BT_TRUE);

            for (uint32_t i = 0; i < lhs_fields->length; ++i) {
                bt_TablePair* field = BT_TABLE_PAIRS(lhs_fields) + i;
                bt_TablePair* type = BT_TABLE_PAIRS(lhs_field_types) + i;
                bt_tableshape_add_layout(parse->context, resulting_type, BT_AS_OBJECT(type->value), field->key, BT_AS_OBJECT(field->value));
            }

            for (uint32_t i = 0; i < rhs_fields->length; ++i) {
                bt_TablePair* field = BT_TABLE_PAIRS(rhs_fields) + i;
                bt_TablePair* type = BT_TABLE_PAIRS(rhs_field_types) + i;

                if (bt_table_get(resulting_type->as.table_shape.layout, field->key) != BT_VALUE_NULL) {
                    assert(0 && "Both lhs and rhs have a feild with name %s!");
                    break;
                }

                bt_tableshape_add_layout(parse->context, resulting_type, BT_AS_OBJECT(type->value), field->key, BT_AS_OBJECT(field->value));
            }

            node->resulting_type = resulting_type;
        } break;
        // Comparison binops always produce boolean
        case BT_TOKEN_LT: case BT_TOKEN_LTE: case BT_TOKEN_GT: case BT_TOKEN_GTE: {
            if (type_check(parse, node->as.binary_op.left)->resulting_type != parse->context->types.number) assert(0);
            if (type_check(parse, node->as.binary_op.right)->resulting_type != parse->context->types.number) assert(0);
            node->as.binary_op.accelerated = BT_TRUE;
            node->resulting_type = parse->context->types.boolean;
        } break;
        case BT_TOKEN_EQUALS: case BT_TOKEN_NOTEQ: {
            bt_Type* tl = type_check(parse, node->as.binary_op.left)->resulting_type;
            bt_Type* tr = type_check(parse, node->as.binary_op.right)->resulting_type;
            if (bt_type_dealias(tl)->satisfier(bt_type_dealias(tl), bt_type_dealias(tr)) == BT_FALSE) assert(0);
            node->resulting_type = parse->context->types.boolean;
        } break;
#define XSTR(x) #x
#define TYPE_ARITH(tok1, tok2, metaname)                                                                                           \
        case tok1: case tok2: {                                                                                                    \
            bt_Type* lhs = type_check(parse, node->as.binary_op.left)->resulting_type;                                             \
            bt_Type* rhs = type_check(parse, node->as.binary_op.right)->resulting_type;                                            \
                                                                                                                                   \
            if(node->source->type == tok2) {                                                                                       \
                bt_AstNode* left = node->as.binary_op.left;                                                                        \
                while (left->type == BT_AST_NODE_BINARY_OP) left = left->as.binary_op.left;                                        \
                bt_ParseBinding* binding = find_local(parse, left);                                                                \
                if (binding && binding->is_const) assert(0 && "Cannot mutate const binding!");                                                \
            }                                                                                                                      \
                                                                                                                                   \
            if (lhs == parse->context->types.number || lhs == parse->context->types.any || (lhs == parse->context->types.string && \
                ((tok1) == BT_TOKEN_PLUS && (tok2) == BT_TOKEN_PLUSEQ))) {                                                         \
                if (!lhs->satisfier(lhs, rhs)) {                                                                                   \
                    assert(0 && "Cannot " XSTR(metaname) " rhs to lhs!");                                                          \
                }                                                                                                                  \
                node->resulting_type = lhs;                                                                                        \
            }                                                                                                                      \
            else {                                                                                                                 \
                if (lhs->category == BT_TYPE_CATEGORY_TABLESHAPE) {                                                                \
                    bt_Value mf_key = BT_VALUE_OBJECT(parse->context->meta_names.metaname);                                        \
                    bt_Value sub_mf = bt_table_get(lhs->prototype_types, mf_key);                                                  \
                    if (sub_mf == BT_VALUE_NULL) assert(0 && "Failed to find @" XSTR(metaname) " metamethod in tableshape!");      \
                    bt_Type* sub = BT_AS_OBJECT(sub_mf);                                                                           \
                                                                                                                                   \
                    if (sub->category != BT_TYPE_CATEGORY_SIGNATURE) {                                                             \
                        assert(0 && "Expected metamethod to be function!");                                                        \
                    }                                                                                                              \
                                                                                                                                   \
                    if (sub->as.fn.args.length != 2 || sub->as.fn.is_vararg) {                                                     \
                        assert(0 && "Expected metamethod to take exactly 2 arguments!");                                           \
                    }                                                                                                              \
                                                                                                                                   \
                    bt_Type* arg_lhs = sub->as.fn.args.elements[0];                                                                \
                    bt_Type* arg_rhs = sub->as.fn.args.elements[1];                                                                \
                                                                                                                                   \
                    if (!arg_lhs->satisfier(arg_lhs, lhs) || !arg_rhs->satisfier(arg_rhs, rhs)) {                                  \
                        assert(0 && "Invalid arguments for @" XSTR(metaname) "!");                                                 \
                    }                                                                                                              \
                                                                                                                                   \
                    node->resulting_type = sub->as.fn.return_type;                                                                 \
                    if(lhs->as.table_shape.final) {                                                                                \
                        node->as.binary_op.hoistable = BT_TRUE;                                                                    \
                        node->as.binary_op.from = lhs;                                                                             \
                        node->as.binary_op.key = mf_key;                                                                           \
                    }                                                                                                              \
                }                                                                                                                  \
                else {                                                                                                             \
                    assert(0 && "Lhs is not an " XSTR(metaname) "able type!");                                                     \
                }                                                                                                                  \
            }                                                                                                                      \
                                                                                                                                   \
            if (node->resulting_type == parse->context->types.number) {                                                            \
                node->as.binary_op.accelerated = BT_TRUE;                                                                          \
            } break;                                                                                                               \
        } break;
        TYPE_ARITH(BT_TOKEN_PLUS, BT_TOKEN_PLUSEQ, add);
        TYPE_ARITH(BT_TOKEN_MINUS, BT_TOKEN_MINUSEQ, sub);
        TYPE_ARITH(BT_TOKEN_MUL, BT_TOKEN_MULEQ, mul);
        TYPE_ARITH(BT_TOKEN_DIV, BT_TOKEN_DIVEQ, div);
        case BT_TOKEN_ASSIGN: {
            bt_AstNode* left = node->as.binary_op.left;
            while (left->type == BT_AST_NODE_BINARY_OP) left = left->as.binary_op.left;
            bt_ParseBinding* binding = find_local(parse, left);
            if (binding && binding->is_const) assert(0 && "Cannot reassign to const binding!");
        }
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

static bt_AstNode* generate_initializer(bt_Parser* parse, bt_Type* type)
{
    bt_AstNode* result = 0;

    switch (type->category) {
    case BT_TYPE_CATEGORY_PRIMITIVE: {
        result = make_node(parse, BT_AST_NODE_LITERAL);
        result->resulting_type = type;
        if (type == parse->context->types.number) {
            result->source = parse->tokenizer->literal_zero;
        }
        else if (type == parse->context->types.boolean) {
            result->source = parse->tokenizer->literal_false;
        }
        else if (type == parse->context->types.string) {
            result->source = parse->tokenizer->literal_empty_string;
        }
        else if (bt_is_optional(type) || type == parse->context->types.any) {
            result->source = parse->tokenizer->literal_null;
        }
    } break;
    case BT_TYPE_CATEGORY_UNION: {
        if (bt_is_optional(type)) {
            result = make_node(parse, BT_AST_NODE_LITERAL);
            result->resulting_type = parse->context->types.null;
            result->source = parse->tokenizer->literal_null;
        }
    } break;
    case BT_TYPE_CATEGORY_ARRAY: {
        result = make_node(parse, BT_AST_NODE_ARRAY);
        bt_buffer_empty(&result->as.arr.items);
        result->as.arr.inner_type = type->as.array.inner;
        result->source = 0;
    } break;
    case BT_TYPE_CATEGORY_TABLESHAPE: {
        result = make_node(parse, BT_AST_NODE_TABLE);
        result->as.table.typed = BT_TRUE;
        result->resulting_type = type;
        bt_buffer_empty(&result->as.table.fields);

        if (type->as.table_shape.layout) {
            bt_Table* items = type->as.table_shape.layout;
            for (uint32_t idx = 0; idx < items->length; idx++) {
                bt_TablePair* pair = BT_TABLE_PAIRS(items) + idx;
                
                bt_AstNode* entry = make_node(parse, BT_AST_NODE_TABLE_ENTRY);
                entry->as.table_field.value_type = BT_AS_OBJECT(pair->value);
                entry->as.table_field.key = pair->key;
                entry->as.table_field.value_expr = generate_initializer(parse, entry->as.table_field.value_type);

                if (!entry->as.table_field.value_expr) {
                    assert(0 && "Failed to generate intiailzier for table field x!");
                }

                bt_buffer_push(parse->context, &result->as.table.fields, entry);
            }
        }
    } break;
    case BT_TYPE_CATEGORY_ENUM: {
        result = make_node(parse, BT_AST_NODE_ENUM_LITERAL);
        result->resulting_type = type;
        bt_Table* options = type->as.enum_.options;
        if (options->length == 0) {
            assert(0 && "Cannot generate initializer for enum with 0 variants!");
        }

        result->as.enum_literal.value = BT_TABLE_PAIRS(options)->value;
    } break;
    }

    return result;
}

static bt_AstNode* parse_let(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_AstNode* node = make_node(parse, BT_AST_NODE_LET);
    node->source = bt_tokenizer_peek(tok);
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

        bt_Type* type = parse_type(parse, BT_TRUE);
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
        // if there's no assignment expression, we need to generate a default initializer!

        // If there's no type specified either, assume any
        if (!node->resulting_type) node->resulting_type = parse->context->types.any;
        
        bt_AstNode* initializer = generate_initializer(parse, node->resulting_type);
        assert(initializer && "Failed to generate default initializer for {}!"); // no explicit type or expression to infer from!
        
        node->as.let.initializer = initializer;
    }

    push_local(parse, node);
    return node;
}

static bt_AstNode* parse_var(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_AstNode* node = make_node(parse, BT_AST_NODE_LET);
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
    bt_AstNode* node = make_node(parse, BT_AST_NODE_RETURN);
    node->source = bt_tokenizer_peek(parse->tokenizer);
    node->as.ret.expr = pratt_parse(parse, 0);
    node->resulting_type = type_check(parse, node->as.ret.expr)->resulting_type;
    
    return node;
}

static bt_String* parse_module_name(bt_Parser* parse, bt_Token* first)
{
    bt_Tokenizer* tok = parse->tokenizer;
    
    bt_Token* tokens[16];
    uint8_t token_top = 0;

    if (first) tokens[token_top++] = first;
    else tokens[token_top++] = bt_tokenizer_emit(tok);

    while (bt_tokenizer_peek(tok)->type == BT_TOKEN_PERIOD) {
        bt_tokenizer_expect(tok, BT_TOKEN_PERIOD);

        tokens[token_top++] = bt_tokenizer_emit(tok);
    }

    char path_buf[1024];
    uint16_t path_len = 0;

    for (uint8_t i = 0; i < token_top; ++i) {
        memcpy(path_buf + path_len, tokens[i]->source.source, tokens[i]->source.length);
        path_len += tokens[i]->source.length;

        if(i < token_top - 1) path_buf[path_len++] = '/';
    }

    path_buf[path_len] = '\0';

    return bt_make_string_len(parse->context, path_buf, path_len);
}

static bt_AstNode* parse_import(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;

    bt_Token* name_or_first_item = bt_tokenizer_peek(tok);
    bt_Token* output_name = name_or_first_item;

    if (name_or_first_item->type == BT_TOKEN_MUL) {
        bt_tokenizer_emit(tok);
        
        // glob import
        bt_Token* next = bt_tokenizer_emit(tok);
        if (next->type != BT_TOKEN_FROM) {
            parse_error_token(parse, "Unexpected token '%.*s' in import statement, expected 'from'", next);
            return NULL;
        }
        
        bt_String* module_name_str = parse_module_name(parse, NULL);
        bt_Value module_name = BT_VALUE_OBJECT(module_name_str);
        bt_Module* mod_to_import = bt_find_module(parse->context, module_name);

        if (!mod_to_import) {
            parse_error_fmt(parse, "Failed to import module '%.*s'", next->line, next->col, module_name_str->len, BT_STRING_STR(module_name_str));
            return NULL;
        }

        bt_Type* export_types = mod_to_import->type;
        bt_Table* types = export_types->as.table_shape.layout;
        bt_Table* values = mod_to_import->exports;

        for (uint32_t i = 0; i < values->length; ++i) {
            bt_TablePair* item = BT_TABLE_PAIRS(values) + i;
            bt_Value type_val = bt_table_get(types, item->key);

            bt_ModuleImport* import = BT_ALLOCATE(parse->context, IMPORT, bt_ModuleImport);
            import->name = BT_AS_OBJECT(item->key);
            import->type = BT_AS_OBJECT(type_val);
            import->value = item->value;

            bt_buffer_push(parse->context, &parse->root->as.module.imports, import);
        }

        return NULL;
    }

    if (name_or_first_item->type != BT_TOKEN_IDENTIFIER) {
        parse_error_token(parse, "Unexpected token '%.*s' in import statement, expected identifier", name_or_first_item);
        return NULL;
    }

    bt_tokenizer_emit(tok);
    bt_Token* peek = bt_tokenizer_peek(tok);
    if (peek->type == BT_TOKEN_COMMA || peek->type == BT_TOKEN_FROM) {
        bt_Buffer(bt_StrSlice) items;
        bt_buffer_with_capacity(&items, parse->context, 1);
        bt_buffer_push(parse->context, &items, name_or_first_item->source);

        while (peek->type == BT_TOKEN_COMMA) {
            bt_tokenizer_emit(tok);
            peek = bt_tokenizer_peek(tok);

            if (peek->type == BT_TOKEN_IDENTIFIER) {
                bt_tokenizer_emit(tok);
                bt_buffer_push(parse->context, &items, peek->source);
                peek = bt_tokenizer_peek(tok);
            }
        }

        if (peek->type != BT_TOKEN_FROM) {
            parse_error_token(parse, "Unexpected token '%.*s' in import statement, expected 'from'", peek);
            return NULL;
        }

        bt_Token* name_begin = bt_tokenizer_emit(tok);

        bt_Value module_name = BT_VALUE_OBJECT(parse_module_name(parse, NULL));

        bt_Module* mod_to_import = bt_find_module(parse->context, module_name);

        if (!mod_to_import) {
            bt_String* name_str = BT_AS_OBJECT(module_name);
            parse_error_fmt(parse, "Failed to import module '%.*s'", name_begin->line, name_begin->col, name_str->len, BT_STRING_STR(name_str));
            return NULL;
        }

        bt_Type* export_types = mod_to_import->type;
        bt_Table* types = export_types->as.table_shape.layout;
        bt_Table* values = mod_to_import->exports;

        for (uint32_t i = 0; i < items.length; ++i) {
            bt_StrSlice* item = items.elements + i;

            bt_ModuleImport* import = BT_ALLOCATE(parse->context, IMPORT, bt_ModuleImport);
            import->name = bt_make_string_hashed_len(parse->context, item->source, item->length);

            bt_Value type_val = bt_table_get(types, BT_VALUE_OBJECT(import->name));
            bt_Value value = bt_table_get(values, BT_VALUE_OBJECT(import->name));

            bt_Type* type = BT_AS_OBJECT(type_val);

            if (type_val == BT_VALUE_NULL || value == BT_VALUE_NULL) {
                bt_String* mod_name_str = BT_AS_OBJECT(module_name);
                parse_error_fmt(parse, "Failed to import item '%.*s' from module '%.*s'", name_begin->col, name_begin->line, 
                    item->length, item->source, mod_name_str->len, BT_STRING_STR(mod_name_str));
                return NULL;
            }

            import->type = BT_AS_OBJECT(type_val);
            import->value = value;

            bt_buffer_push(parse->context, &parse->root->as.module.imports, import);
        }

        bt_buffer_destroy(parse->context, &items);
        return NULL;
    }

    bt_Value module_name = BT_VALUE_OBJECT(parse_module_name(parse, name_or_first_item));

    peek = bt_tokenizer_peek(tok);
    if (peek->type == BT_TOKEN_AS) {
        bt_tokenizer_emit(tok);
        output_name = bt_tokenizer_emit(tok);

        if (output_name->type != BT_TOKEN_IDENTIFIER) {
            parse_error_token(parse, "Unexpected token '%.*s' in import statement", output_name);
            return NULL;
        }
    }
    
    bt_Module* mod_to_import = bt_find_module(parse->context, module_name);

    if (!mod_to_import) {
        bt_String* name_str = BT_AS_OBJECT(module_name);
        parse_error_fmt(parse, "Failed to import module '%.*s'", name_or_first_item->line, name_or_first_item->col, name_str->len, BT_STRING_STR(name_str));
        return NULL;
    }

    bt_ModuleImport* import = BT_ALLOCATE(parse->context, IMPORT, bt_ModuleImport);
    import->name = bt_make_string_hashed_len(parse->context, output_name->source.source, output_name->source.length);
    import->type = mod_to_import->type;
    import->value = BT_VALUE_OBJECT(mod_to_import->exports);

    bt_buffer_push(parse->context, &parse->root->as.module.imports, import);

    return NULL;
}

static bt_AstNode* parse_export(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;
    bt_AstNode* to_export = parse_statement(parse);
    
    bt_StrSlice name;

    if (to_export->type == BT_AST_NODE_LET) {
        name = to_export->as.let.name;
    }
    else if (to_export->type == BT_AST_NODE_ALIAS) {
        name = to_export->source->source;
    }
    else if (to_export->type == BT_AST_NODE_IDENTIFIER) {
        name = to_export->source->source;
    }
    else {
        parse_error_token(parse, "Unexportable expression '%.*s' following 'export'!", to_export->source);
        return NULL;
    }

    bt_AstNode* export = make_node(parse, BT_AST_NODE_EXPORT);
    export->source = to_export->source;
    export->as.exp.name = name;
    export->as.exp.value = to_export;
    export->resulting_type = type_check(parse, to_export)->resulting_type;
    
    if (export->resulting_type == 0) {
        parse_error_token(parse, "Failed to resolve type of export", export->source);
        return NULL;
    }

    return export;
}

static bt_AstNode* parse_function_statement(bt_Parser* parser)
{
    bt_Tokenizer* tok = parser->tokenizer;
    bt_Token* ident = bt_tokenizer_emit(tok);

    if (ident->type != BT_TOKEN_IDENTIFIER) {
        parse_error_token(parser, "Function name '%.*s' must be valid identifer", ident);
        return NULL;
    }

    bt_Type* type = find_type_or_shadow(parser, ident);

    if (type) {
        // We are defining a member function
        if (!bt_tokenizer_expect(tok, BT_TOKEN_PERIOD)) {
            return NULL;
        }

        ident = bt_tokenizer_emit(tok);
        if (ident->type != BT_TOKEN_IDENTIFIER) assert(0 && "Cannot assign to non-identifier!");

        bt_AstNode* fn = parse_function_literal(parser);
        if (fn->type != BT_AST_NODE_FUNCTION) assert(0 && "Expected function literal!");
    
        bt_StrSlice this_str = { "this", 4 };
        if (fn->as.fn.args.length) {
            bt_FnArg* arg = fn->as.fn.args.elements;
            if (bt_strslice_compare(arg->name, this_str) && arg->type->satisfier(arg->type, type)) {
                fn->type = BT_AST_NODE_METHOD;
                fn->resulting_type->as.fn.is_method = BT_TRUE;
            }
        }

        bt_String* name = bt_make_string_hashed_len(parser->context, ident->source.source, ident->source.length);
        bt_type_add_field(parser->context, type, BT_VALUE_OBJECT(name), BT_VALUE_OBJECT(fn), fn->resulting_type);
        return NULL;
    }

    bt_AstNode* fn = parse_function_literal(parser);
    if (fn->type != BT_AST_NODE_FUNCTION) assert(0 && "Expected function literal!");

    bt_AstNode* result = make_node(parser, BT_AST_NODE_LET);
    result->source = ident;
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

    bt_Token* next = bt_tokenizer_peek(tok);

    bt_AstNode* result = make_node(parser, BT_AST_NODE_IF);
    result->as.branch.next = NULL;

    if (next->type == BT_TOKEN_LET) {
        bt_tokenizer_emit(tok);

        bt_Token* ident = bt_tokenizer_emit(tok);

        if (ident->type != BT_TOKEN_IDENTIFIER) {
            assert(0 && "Expected identifier!");
        }

        bt_Token* assign = bt_tokenizer_emit(tok);
        if (assign->type != BT_TOKEN_ASSIGN) {
            assert(0 && "Expected assignment!");
        }

        bt_AstNode* expr = pratt_parse(parser, 0);
        bt_Type* result_type = type_check(parser, expr)->resulting_type;
        if (!bt_is_optional(result_type)) {
            assert(0 && "Type must be optional!");
        }

        bt_Type* binding_type = bt_remove_nullable(parser->context, result_type);

        result->source = ident;
        result->as.branch.is_let = BT_TRUE;
        result->as.branch.identifier = ident;
        result->as.branch.condition = expr;
        result->as.branch.bound_type = binding_type;

        bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);

        push_scope(parser, BT_FALSE);
        push_local(parser, result);
        bt_AstBuffer body;
        bt_buffer_with_capacity(&body, parser->context, 8);
        parse_block(&body, parser);
        pop_scope(parser);

        result->as.branch.body = body;

        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);
    }
    else {
        bt_AstNode* condition = pratt_parse(parser, 0);
        if (type_check(parser, condition)->resulting_type != parser->context->types.boolean) {
            assert(0 && "If expression must evaluate to boolean!");
        }

        bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);

        bt_AstBuffer body;
        bt_buffer_with_capacity(&body, parser->context, 8);
        parse_block(&body, parser);

        result->source = condition->source;
        result->as.branch.condition = condition;
        result->as.branch.body = body;
        result->as.branch.is_let = BT_FALSE;

        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);
    }

    next = bt_tokenizer_peek(tok);
    if (next && next->type == BT_TOKEN_ELSE) {
        bt_tokenizer_emit(tok);
        next = bt_tokenizer_peek(tok);

        if (next->type == BT_TOKEN_IF) {
            bt_tokenizer_emit(tok);
            result->as.branch.next = parse_if(parser);
        }
        else {
            bt_AstNode* else_node = make_node(parser, BT_AST_NODE_IF);
            else_node->as.branch.condition = NULL;
            else_node->as.branch.next = NULL;
            else_node->as.branch.is_let = BT_FALSE;

            bt_AstBuffer body;
            bt_buffer_with_capacity(&body, parser->context, 8);
            
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
    bt_Tokenizer* tok = parse->tokenizer;
    bt_Token* token = bt_tokenizer_peek(tok);

    bt_AstNode* identifier;
    if (token->type == BT_TOKEN_LEFTBRACE) identifier = token_to_node(parse, tok->literal_true);
    else identifier = pratt_parse(parse, 0);

    if (identifier->type != BT_AST_NODE_IDENTIFIER || type_check(parse, identifier)->resulting_type == parse->context->types.boolean)
    {
        // "while"-style loop
        if (type_check(parse, identifier)->resulting_type != parse->context->types.boolean) {
            parse_error_token(parse, "'while'-style loop condition must be boolean expression: '%.*s'", identifier->source);
            return NULL;
        }

        bt_AstNode* result = make_node(parse, BT_AST_NODE_LOOP_WHILE);
        result->as.loop_while.condition = identifier;

        bt_AstBuffer body;
        bt_buffer_with_capacity(&body, parse->context, 8);

        push_scope(parse, BT_FALSE);

        bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);
        parse_block(&body, parse);
        bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);

        pop_scope(parse);

        result->as.loop_while.body = body;
        return result;
    }

    if (!bt_tokenizer_expect(tok, BT_TOKEN_IN)) return NULL;

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

        bt_AstNode* result = make_node(parse, BT_AST_NODE_LOOP_NUMERIC);
        result->as.loop_numeric.start = start;
        result->as.loop_numeric.stop = stop;
        result->as.loop_numeric.step = step;

        identifier->resulting_type = parse->context->types.number;
        result->as.loop_numeric.identifier = identifier;

        bt_AstBuffer body;
        bt_buffer_with_capacity(&body, parse->context, 8);

        push_scope(parse, BT_FALSE);

        bt_AstNode* ident_as_let = make_node(parse, BT_AST_NODE_LET);
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
    else if (generator_type->category != BT_TYPE_CATEGORY_SIGNATURE) {
        parse_error_fmt(parse, "Expected iterator to be function, got %s", iterator->source->line, iterator->source->col,
            generator_type->name);
        return NULL;
    }

    bt_Type* generated_type = generator_type->as.fn.return_type;
    if (!bt_is_optional(generated_type)) {
        parse_error_fmt(parse, "Iterator return type must be optional, got %s", iterator->source->line, iterator->source->col, 
            generated_type->name);
        return NULL;
    }

    bt_Type* it_type = bt_remove_nullable(parse->context, generated_type);
    identifier->resulting_type = it_type;

    bt_AstBuffer body;
    bt_buffer_with_capacity(&body, parse->context, 8);

    push_scope(parse, BT_FALSE);

    bt_AstNode* ident_as_let = make_node(parse, BT_AST_NODE_LET);
    ident_as_let->as.let.initializer = NULL;
    ident_as_let->as.let.is_const = BT_TRUE;
    ident_as_let->as.let.name = identifier->source->source;
    ident_as_let->resulting_type = identifier->resulting_type;

    push_local(parse, ident_as_let);

    bt_tokenizer_expect(tok, BT_TOKEN_LEFTBRACE);
    parse_block(&body, parse);
    bt_tokenizer_expect(tok, BT_TOKEN_RIGHTBRACE);
    
    pop_scope(parse);

    bt_AstNode* result = make_node(parse, BT_AST_NODE_LOOP_ITERATOR);
    result->as.loop_iterator.body = body;
    result->as.loop_iterator.identifier = identifier;
    result->as.loop_iterator.iterator = iterator;

    return result;
}

static bt_AstNode* parse_alias(bt_Parser* parse)
{
    bt_AstNode* result = make_node(parse, BT_AST_NODE_ALIAS);

    bt_Token* name = bt_tokenizer_emit(parse->tokenizer);

    if (name->type != BT_TOKEN_IDENTIFIER) {
        parse_error_token(parse, "Expected identifier, got '%.*s'", name);
        return NULL;
    }

    result->source = name;
    result->resulting_type = parse->context->types.type;

    bt_tokenizer_expect(parse->tokenizer, BT_TOKEN_ASSIGN);

    bt_Type* type = parse_type(parse, BT_TRUE);

    result->as.alias.type = type;

    push_local(parse, result);

    return result;
}

static bt_AstNode* parse_method(bt_Parser* parse)
{
    bt_Tokenizer* tok = parse->tokenizer;

    bt_Token* type_name = bt_tokenizer_emit(tok);
    bt_Type* type = resolve_type_identifier(parse, type_name);

    if (!bt_tokenizer_expect(tok, BT_TOKEN_PERIOD)) {
        return NULL;
    }

    bt_Token* method_name = bt_tokenizer_emit(tok);
    if (method_name->type != BT_TOKEN_IDENTIFIER) {
        parse_error_token(parse, "Expected identifier for method name, got '%.*s'", method_name);
        return NULL;
    }

    bt_AstNode* result = make_node(parse, BT_AST_NODE_METHOD);
    
    bt_buffer_empty(&result->as.method.args);
    bt_buffer_with_capacity(&result->as.method.body, parse->context, 8);
    result->as.method.ret_type = NULL;
    result->as.method.outer = parse->current_fn;
    bt_buffer_empty(&result->as.method.upvals);

    parse->current_fn = result;

    bt_FnArg this_arg;
    this_arg.name = (bt_StrSlice) { "this", 4 };
    this_arg.type = type;
    bt_buffer_push(parse->context, &result->as.method.args, this_arg);

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
                parse_error_token(parse, "Unexpected token '%.*s' in parameter list", next);
            }

            if (next->type == BT_TOKEN_COLON) {
                next = bt_tokenizer_emit(tok);
                this_arg.type = parse_type(parse, BT_TRUE);
            }
            else {
                this_arg.type = parse->context->types.any;
            }


            bt_buffer_push(parse->context, &result->as.method.args, this_arg);

            next = bt_tokenizer_emit(tok);
        } while (next && next->type == BT_TOKEN_COMMA);
    }

    if (has_param_list && next && next->type != BT_TOKEN_RIGHTPAREN) {
        parse_error_token(parse, "Expected end of parameter list, got '%.*s'", next);
        return NULL;
    }

    next = bt_tokenizer_peek(tok);

    if (next->type == BT_TOKEN_COLON) {
        next = bt_tokenizer_emit(tok);
        result->as.method.ret_type = parse_type(parse, BT_TRUE);
    }

    next = bt_tokenizer_emit(tok);

    if (next->type == BT_TOKEN_LEFTBRACE) {
        push_scope(parse, BT_TRUE);

        for (uint8_t i = 0; i < result->as.method.args.length; i++) {
            push_arg(parse, result->as.method.args.elements + i, result->source);
        }

        parse_block(&result->as.method.body, parse);

        pop_scope(parse);
    }
    else {
        parse_error_token(parse, "Expected function body, got '%.*s'", next);
        return NULL;
    }

    result->as.method.ret_type = infer_return(parse->context, &result->as.method.body, result->as.method.ret_type);

    next = bt_tokenizer_emit(tok);
    if (next->type != BT_TOKEN_RIGHTBRACE) {
        parse_error_token(parse, "Expected end of function, got '%.*s'", next);
        return NULL;
    }

    bt_Type* args[16];

    for (uint8_t i = 0; i < result->as.method.args.length; ++i) {
        args[i] = result->as.method.args.elements[i].type;
    }

    result->resulting_type = bt_make_method(parse->context, result->as.method.ret_type, args, result->as.method.args.length);

    parse->current_fn = parse->current_fn->as.method.outer;

    bt_String* name_str = bt_make_string_hashed_len(parse->context, method_name->source.source, method_name->source.length);
    bt_type_add_field(parse->context, type, BT_VALUE_OBJECT(name_str), BT_VALUE_OBJECT(result), result->resulting_type);

    return NULL;
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
    case BT_TOKEN_METHOD: {
        bt_tokenizer_emit(tok);
        return parse_method(parse);
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
    case BT_TOKEN_BREAK: {
        bt_AstNode* result = make_node(parse, BT_AST_NODE_BREAK);
        result->source = bt_tokenizer_emit(tok);
        return result;
    }
    case BT_TOKEN_CONTINUE: {
        bt_AstNode* result = make_node(parse, BT_AST_NODE_CONTINUE);
        result->source = bt_tokenizer_emit(tok);
        return result;
    }
    case BT_TOKEN_EOS: {
        return NULL;
    }
    default: // no statment structure found, assume expression
        return pratt_parse(parse, 0);
    }
}

bt_bool bt_parse(bt_Parser* parser)
{
    parser->root = (bt_AstNode*)parser->context->alloc(sizeof(bt_AstNode));
    parser->root->type = BT_AST_NODE_MODULE;
    bt_buffer_empty(&parser->root->as.module.body);
    bt_buffer_empty(&parser->root->as.module.imports);
    parser->current_fn = NULL;

    push_scope(parser, BT_FALSE);

    while (bt_tokenizer_peek(parser->tokenizer)->type != BT_TOKEN_EOS && !parser->has_errored)
    {
        bt_AstNode* expression = parse_statement(parser);
        if (expression) {
            bt_buffer_push(parser->context, &parser->root->as.module.body, expression);
        }
    }

    pop_scope(parser);

#ifdef BOLT_PRINT_DEBUG
    bt_debug_print_parse_tree(parser);
#endif

    return !parser->has_errored;
}
