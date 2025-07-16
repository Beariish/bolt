# Math
The math module is a thin wrapper around `math.h`.

## Constants
```ts
math.pi: number
math.tau: number
math.e: number

math.huge: number
math.infinity: number
math.nan: number

math.ln2: number
math.ln10: number
math.log2e: number
math.log10e: number

math.sqrt2: number
math.sqrthalf: number

math.epsilon: number
```

## Functions

```ts
// Returns the smallest out of all the numbers provided.
math.min(n: number, rest: ..number): number

// Returns the largest out of all the numbers provided.
math.max(n: number, rest: ..number): number

// Returns whether `n` is a power of 2.
math.ispow2(n: number): bool

// Sets the seed for the internal random number generator
math.random_seed(seed: number)

// Returns a random number in the range [0-1)
math.random(): number

// Explaining the following functions in detail seems excessive, so I'm just gonna list them.
math.sqrt(n: number): number
math.abs(n: number): number
math.round(n: number): number
math.ceil(n: number): number
math.floor(n: number): number
math.trunc(n: number): number
math.sign(n: number): number

math.sin(n: number): number
math.cos(n: number): number
math.tan(n: number): number

math.asin(n: number): number
math.acos(n: number): number
math.atan(n: number): number

math.sinh(n: number): number
math.cosh(n: number): number
math.tanh(n: number): number

math.asinh(n: number): number
math.acosh(n: number): number
math.atanh(n: number): number
math.atan2(y: number, x: number): number

math.log(n: number): number
math.log10(n: number): number
math.log2(n: number): number
math.exp(n: number): number

math.deg(n: number): number
math.rad(n: number): number

math.pow(a: number, b: number): number
math.mod(a: number, b: number): number
math.imod(a: number, b: number): number
```