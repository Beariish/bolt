#pragma once

#include "bt_prelude.h"

typedef enum {
	/*
		R: function-local register array, starting from 0
		L: function-specific literal array, containing precomputed literal values
		frame: vm call frame, has info about current invocation
	*/

	BT_OP_LOAD,              // R(a) = L[ubc]
	BT_OP_LOAD_SMALL,		 // R(a) = tonumber(ibc)
	BT_OP_LOAD_NULL,		 // R(a) = null
	BT_OP_LOAD_BOOL,		 // R(a) = b ? BT_TRUE : BT_FALSE
	BT_OP_LOAD_IMPORT,       // R(a) = imports[ubc]
	BT_OP_TABLE,             // R(a) = new tablesize(ibc)
	BT_OP_TTABLE,            // R(a) = new tablesize(b) with proto(R(c))
	BT_OP_ARRAY,             // R(a) = new array[b]

	BT_OP_MOVE,				 // R(a) = R(b)
	BT_OP_EXPORT,            // exports[R(a)]: R(c) = R(b)

	BT_OP_CLOSE,             // R(a) = Closure(R(b)) with upvals[R(b+1..b+c)]
	BT_OP_LOADUP,            // R(a) = upvals[b]
	BT_OP_STOREUP,           // upvals[a] = R(b)

	BT_OP_NEG,				 // R(a) = -R(b)
	BT_OP_ADD,				 // R(a) = R(b) + R(c)
	BT_OP_SUB,				 // R(a) = R(b) - R(c)
	BT_OP_MUL,				 // R(a) = R(b) * R(c)
	BT_OP_DIV,				 // R(a) = R(b) / R(c)

	BT_OP_EQ,                // R(a) = R(b) == R(c)
	BT_OP_NEQ,	             // R(a) = R(b) != R(c)
	BT_OP_LT,                // R(a) = R(b) < R(c)
	BT_OP_LTE,               // R(a) = R(b) <= R(c)
	BT_OP_AND,               // R(a) = R(b) and R(c)
	BT_OP_OR,	             // R(a) = R(b) or R(c)
	BT_OP_NOT,				 // R(a) = not R(b)

	BT_OP_LOAD_IDX,			 // R(a) = b.[c]
	BT_OP_STORE_IDX,		 // b.[c] = R(a)

	BT_OP_EXPECT,            // R(a) = R(b) ? FAIL
	BT_OP_EXISTS,			 // R(a) = R(b) != null
	BT_OP_COALESCE,			 // R(a) = R(b) == null ? R(c) : R(b)

	BT_OP_TCHECK,            // R(a) = R(b) is Type(c)
	BT_OP_TCAST,             // R(a) = R(b) as Type(c)
	BT_OP_TALIAS,            // R(a) = (Type(c))R(b)
	BT_OP_COMPOSE,           // R(a) = fieldsof(b) + fieldsof(c)

	BT_OP_CALL,              // R(a) = R(b)(R(b + 1) .. R(b + c))

	BT_OP_JMP,               // pc += ibc
	BT_OP_JMPF,              // if(R(a) == BT_FALSE) pc += ibc

	BT_OP_RETURN,			 // R(frame->ret_pos) = R(a)
	BT_OP_END,               // return without value

	// Fast opcode extensions.
	// These are emitted by the compiler whenever types are strongly known
	
	// Looping macroops
	BT_OP_NUMFOR,
	BT_OP_ITERFOR,

	// Fast arithmetic, do the same thing as their non-fast counterparts, but 
	// do no typechecking. bolt values are numbers by default
	BT_OP_NEGF,
	BT_OP_ADDF,
	BT_OP_SUBF,
	BT_OP_MULF,
	BT_OP_DIVF,
	BT_OP_EQF,
	BT_OP_NEQF,
	BT_OP_LTF,
	BT_OP_LTEF,

	// Fast table indexing. Used for known tableshapes, as the pair offset
	// is known by the parser
	BT_OP_LOAD_IDX_F,
	BT_OP_STORE_IDX_F,

	// Fast array indexing. Used when the indexed type is known to be an array,
	// and the index known to be a number
	BT_OP_LOAD_SUB_F,
	BT_OP_STORE_SUB_F,
} bt_OpCode;

typedef struct bt_Op {
	uint8_t op, a;
	union {
		struct { uint8_t b, c; };
		struct { uint16_t ubc; };
		struct { int16_t ibc; };
	};
} bt_Op;