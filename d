[1mdiff --git a/bt_compiler.c b/bt_compiler.c[m
[1mindex 1072f09..4bcaa78 100644[m
[1m--- a/bt_compiler.c[m
[1m+++ b/bt_compiler.c[m
[36m@@ -21,6 +21,12 @@[m [mtypedef struct CompilerBinding {[m
     uint8_t loc;[m
 } CompilerBinding;[m
 [m
[32m+[m[32mtypedef enum StorageClass {[m[41m[m
[32m+[m[32m    STORAGE_INVALID,[m[41m[m
[32m+[m[32m    STORAGE_REGISTER,[m[41m[m
[32m+[m[32m    STORAGE_UPVAL[m[41m[m
[32m+[m[32m} StorageClass;[m[41m[m
[32m+[m[41m[m
 typedef struct FunctionContext {[m
     uint8_t min_top_register;[m
 [m
[36m@@ -266,6 +272,21 @@[m [mstatic uint8_t find_binding_or_compile_temp(FunctionContext* ctx, bt_AstNode* ex[m
     return loc;[m
 }[m
 [m
[32m+[m[32mstatic StorageClass get_storage(FunctionContext* ctx, bt_AstNode* expr)[m[41m[m
[32m+[m[32m{[m[41m[m
[32m+[m[32m    uint8_t loc = find_binding(ctx, expr->source->source);[m[41m[m
[32m+[m[32m    if (loc != INVALID_BINDING) {[m[41m[m
[32m+[m[32m        return STORAGE_REGISTER;[m[41m[m
[32m+[m[32m    }[m[41m[m
[32m+[m[41m[m
[32m+[m[32m    loc = find_upval(ctx, expr->source->source);[m[41m[m
[32m+[m[32m    if (loc != INVALID_BINDING) {[m[41m[m
[32m+[m[32m        return STORAGE_UPVAL;[m[41m[m
[32m+[m[32m    }[m[41m[m
[32m+[m[41m    [m
[32m+[m[32m    return STORAGE_INVALID;[m[41m[m
[32m+[m[32m}[m[41m[m
[32m+[m[41m[m
 static uint8_t find_binding_or_compile_loc(FunctionContext* ctx, bt_AstNode* expr, uint8_t backup_loc)[m
 {[m
     uint8_t loc = INVALID_BINDING;[m
[36m@@ -283,6 +304,19 @@[m [mstatic uint8_t find_binding_or_compile_loc(FunctionContext* ctx, bt_AstNode* exp[m
     return loc;[m
 }[m
 [m
[32m+[m[32mstatic bt_bool is_assigning(bt_TokenType op) {[m[41m[m
[32m+[m[32m    switch (op) {[m[41m[m
[32m+[m[32m    case BT_TOKEN_ASSIGN:[m[41m[m
[32m+[m[32m    case BT_TOKEN_PLUSEQ:[m[41m[m
[32m+[m[32m    case BT_TOKEN_MINUSEQ:[m[41m[m
[32m+[m[32m    case BT_TOKEN_MULEQ:[m[41m[m
[32m+[m[32m    case BT_TOKEN_DIVEQ:[m[41m[m
[32m+[m[32m        return BT_TRUE;[m[41m[m
[32m+[m[32m    }[m[41m[m
[32m+[m[41m[m
[32m+[m[32m    return BT_FALSE;[m[41m[m
[32m+[m[32m}[m[41m[m
[32m+[m[41m[m
 static bt_bool compile_expression(FunctionContext* ctx, bt_AstNode* expr, uint8_t result_loc)[m
 {[m
     switch (expr->type) {[m
[36m@@ -387,17 +421,32 @@[m [mstatic bt_bool compile_expression(FunctionContext* ctx, bt_AstNode* expr, uint8_[m
         uint8_t lhs_loc = find_binding_or_compile_loc(ctx, lhs, result_loc);[m
         uint8_t rhs_loc = find_binding_or_compile_temp(ctx, rhs);[m
 [m
[32m+[m[32m        StorageClass storage = STORAGE_REGISTER;[m[41m[m
[32m+[m[32m        if (is_assigning(expr->source->type)) {[m[41m[m
[32m+[m[32m            storage = get_storage(ctx, lhs);[m[41m[m
[32m+[m[32m            if (storage == STORAGE_INVALID) {[m[41m[m
[32m+[m[32m                assert(0 && "Unassignable lhs!");[m[41m[m
[32m+[m[32m            }[m[41m[m
[32m+[m[32m            else if (storage == STORAGE_REGISTER) {[m[41m[m
[32m+[m[32m                result_loc = lhs_loc;[m[41m[m
[32m+[m[32m            }[m[41m[m
[32m+[m[32m        }[m[41m[m
[32m+[m[41m[m
         switch (expr->source->type) {[m
         case BT_TOKEN_PLUS:[m
[32m+[m[32m        case BT_TOKEN_PLUSEQ:[m[41m[m
             emit_abc(ctx, BT_OP_ADD, result_loc, lhs_loc, rhs_loc);[m
             break;[m
         case BT_TOKEN_MINUS:[m
[32m+[m[32m        case BT_TOKEN_MINUSEQ:[m[41m[m
             emit_abc(ctx, BT_OP_SUB, result_loc, lhs_loc, rhs_loc);[m
             break;[m
         case BT_TOKEN_MUL:[m
[32m+[m[32m        case BT_TOKEN_MULEQ:[m[41m[m
             emit_abc(ctx, BT_OP_MUL, result_loc, lhs_loc, rhs_loc);[m
             break;[m
         case BT_TOKEN_DIV:[m
[32m+[m[32m        case BT_TOKEN_DIVEQ:[m[41m[m
             emit_abc(ctx, BT_OP_DIV, result_loc, lhs_loc, rhs_loc);[m
             break;[m
         case BT_TOKEN_AND:[m
[36m@@ -430,9 +479,17 @@[m [mstatic bt_bool compile_expression(FunctionContext* ctx, bt_AstNode* expr, uint8_[m
         case BT_TOKEN_GTE:[m
             emit_abc(ctx, BT_OP_LTE, result_loc, rhs_loc, lhs_loc);[m
             break;[m
[32m+[m[32m        case BT_TOKEN_ASSIGN:[m[41m[m
[32m+[m[32m            emit_ab(ctx, BT_OP_MOVE, result_loc, rhs_loc);[m[41m[m
[32m+[m[32m            break;[m[41m[m
         default: assert(0 && "Unimplemented binary operator!");[m
         }[m
 [m
[32m+[m[32m        if (storage == STORAGE_UPVAL) {[m[41m[m
[32m+[m[32m            uint8_t upval_idx = find_upval(ctx, lhs->source->source);[m[41m[m
[32m+[m[32m            emit_ab(ctx, BT_OP_STOREUP, upval_idx, result_loc);[m[41m[m
[32m+[m[32m        }[m[41m[m
[32m+[m[41m[m
         restore_registers(ctx);[m
     } break;[m
     case BT_AST_NODE_FUNCTION: {[m
