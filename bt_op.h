#pragma once

#include "bt_prelude.h"

/*
	R: function-local register array, starting from 0
	L: function-specific literal array, containing precomputed literal values
	frame: vm call frame, has info about current invocation
*/

#define BT_OPS_X                                                                    \
	X(LOAD)        /*  R(a) = L[ubc]                                 */             \
	X(LOAD_SMALL)  /*  R(a) = tonumber(ibc)                          */             \
	X(LOAD_NULL)   /*  R(a) = null                                   */             \
	X(LOAD_BOOL)   /*  R(a) = b ? BT_TRUE : BT_FALSE                 */             \
	X(LOAD_IMPORT) /*  R(a) = imports[ubc]                           */             \
	X(TABLE)       /*  R(a) = new tablesize(ibc)                     */             \
	X(TTABLE)      /*  R(a) = new tablesize(b) with proto(R(c))      */             \
	X(ARRAY)       /*  R(a) = new array[b]                           */             \
	X(MOVE)        /*  R(a) = R(b)                                   */             \
	X(EXPORT)      /*  exports[R(a)]: R(c) = R(b)                    */             \
	X(CLOSE)       /*  R(a) = Closure(R(b)) with upvals[R(b+1..b+c)] */             \
	X(LOADUP)      /*  R(a) = upvals[b]                              */             \
	X(STOREUP)     /*  upvals[a] = R(b)                              */             \
	X(NEG)         /*  R(a) = -R(b)                                  */             \
	X(ADD)         /*  R(a) = R(b) + R(c)                            */             \
	X(SUB)         /*  R(a) = R(b) - R(c)                            */             \
	X(MUL)         /*  R(a) = R(b) * R(c)                            */             \
	X(DIV)         /*  R(a) = R(b) / R(c)                            */             \
	X(EQ)          /*  R(a) = R(b) == R(c)                           */             \
	X(NEQ)         /*  R(a) = R(b) != R(c)                           */             \
	X(LT)          /*  R(a) = R(b) < R(c)                            */             \
	X(LTE)         /*  R(a) = R(b) <= R(c)                           */             \
	X(AND)         /*  R(a) = R(b) and R(c)                          */             \
	X(OR)          /*  R(a) = R(b) or R(c)                           */             \
	X(NOT)         /*  R(a) = not R(b)                               */             \
	X(LOAD_IDX)    /*  R(a) = R(b).[R(c)]                            */             \
	X(STORE_IDX)   /*  R(b).[R(c)] = R(a)                            */             \
	X(LOAD_IDX_K)  /*  R(a) = R(b).[L(c)]                            */             \
	X(STORE_IDX_K) /*  R(b).[L(c)] = R(a)                            */             \
	X(EXPECT)      /*  R(a) = R(b) ? FAIL                            */             \
	X(EXISTS)      /*  R(a) = R(b) != null                           */             \
	X(COALESCE)    /*  R(a) = R(b) == null ? R(c) : R(b)             */             \
	X(TCHECK)      /*  R(a) = R(b) is Type(c)                        */             \
	X(TSATIS)      /*  R(a) = R(b) satisfies Type(c)                 */             \
	X(TCAST)       /*  R(a) = R(b) as Type(c)                        */             \
	X(TALIAS)      /*  R(a) = (Type(c))R(b)                          */             \
	X(COMPOSE)     /*  R(a) = fieldsof(b) + fieldsof(c)              */             \
	X(CALL)        /*  R(a) = R(b)(R(b + 1) .. R(b + c))             */             \
	X(JMP)         /*  pc += ibc                                     */             \
	X(JMPF)        /*  if(R(a) == BT_FALSE) pc += ibc                */             \
	X(RETURN)      /*  R(frame->ret_pos) = R(a)                      */             \
	X(END)         /*  return without value                          */             \
	                                                                                \
	/*  Fast opcode extensions. */                                                  \
	/*  These are emitted by the compiler whenever types are strongly known */	    \
																				    \
	/*  Looping macroops */                                                         \
	X(NUMFOR)                                                                       \
	X(ITERFOR)                                                                      \
                                                                                    \
	/*  Fast arithmetic, do the same thing as their non-fast counterparts, but  */  \
	/*  do no typechecking. bolt values are numbers by default */				    \
	X(NEGF)                                                                         \
	X(ADDF)                                                                         \
	X(SUBF)                                                                         \
	X(MULF)                                                                         \
	X(DIVF)                                                                         \
	X(EQF)                                                                          \
	X(NEQF)                                                                         \
	X(LTF)                                                                          \
	X(LTEF)                                                                         \
                                                                                    \
	/*  Fast table indexing. Used for known tableshapes, as the pair offset */	    \
	/*  is known by the parser */												    \
	X(LOAD_IDX_F)                                                                   \
	X(STORE_IDX_F)                                                                  \
                                                                                    \
	/*  Fast array indexing. Used when the indexed type is known to be an array, */	\
	/*  and the index known to be a number */										\
	X(LOAD_SUB_F)                                                                   \
	X(STORE_SUB_F)

typedef enum {
#define X(op) BT_OP_##op,
		BT_OPS_X
#undef X
} bt_OpCode;

#ifdef BOLT_BITMASK_OP
typedef uint32_t bt_Op;

#define BT_MAKE_OP_ABC(op, a, b, c) \
	((((bt_Op)op)) | (((bt_Op)a) << 8) | (((bt_Op)b) << 16) | ((bt_Op)c) << 24)

#define BT_MAKE_OP_AIBC(op, a, ibc) \
	((((bt_Op)op)) | (((bt_Op)a) << 8) | ((uint32_t)((uint16_t)ibc) << 16))

#define BT_MAKE_OP_AUBC(op, a, ubc) \
	((((bt_Op)op)) | (((bt_Op)a) << 8) | (((bt_Op)ubc) << 16))

#define BT_GET_OPCODE(op) (op & 0xff)
#define BT_GET_A(op) ((op >> 8) & 0xff)
#define BT_GET_B(op) ((op >> 16) & 0xff)
#define BT_GET_C(op) (op >> 24)
#define BT_GET_IBC(op) (int16_t)(op >> 16)
#define BT_GET_UBC(op) (op >> 16)

#define BT_SET_IBC(op, ibc) (op) = (((op) & (~(bt_Op)0xFFFF0000)) | (((uint32_t)((uint16_t)ibc)) << 16))
#else
typedef struct bt_Op {
	uint8_t op, a;
	union {
		struct { uint8_t b, c; };
		struct { uint16_t ubc; };
		struct { int16_t ibc; };
	};
} bt_Op;

#define BT_MAKE_OP_ABC(code, a, b, c) \
	((bt_Op){ .op = code, .a = a, .b = b, .c = c })

#define BT_MAKE_OP_AIBC(code, a, ibc) \
	((bt_Op){ .op = code, .a = a, .ibc = ibc })

#define BT_MAKE_OP_AUBC(code, a, ubc) \
	((bt_Op){ .op = code, .a = a, .ubc = ubc })

#define BT_GET_OPCODE(op) op.op
#define BT_GET_A(op) op.a
#define BT_GET_B(op) op.b
#define BT_GET_C(op) op.c
#define BT_GET_IBC(op) op.ibc
#define BT_GET_UBC(op) op.ubc

#define BT_SET_IBC(op, _ibc) ((op).ibc = (_ibc))
#endif