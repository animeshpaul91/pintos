/* Implementation of Fixed Point Real Arithmetic
Floating pt operations are done by simulation using
17.14 (P.Q) Fixed point number representations,
where P are digits before decimal pt & Q digits are after it.
Implemented as MACROS for faster processing
*/
#ifndef THREADS_FP_MATH_H
#define THREADS_FP_MATH_H
#define f 1 << (14)
//Conversions
#define CONVERT_INT_FP(n) ((n) * (f))
#define CONVERT_FP_INT(x) ((x) >= 0 ? ((x) + (f) / 2) / (f) : ((x) - (f) / 2) / (f))
#define CONVERT_FP_INT_FLOOR(x) ((x) / (f))
// Additions & Subtractions
#define ADD_FP_FP(x, y) ((x) + (y))
#define SUB_FP_FP(x, y) ((x) - (y))
#define ADD_FP_INT(x, n) ((x) + (n) * (f))
#define SUB_FP_INT(x, n) ((x) - (n) * (f))
//Mult & Div
#define MULT_FP_FP(x, y) (((int64_t)(x)) * (y) / (f))
#define MULT_FP_INT(x, n) ((x) * (n))
#define DIV_FP_FP(x, y) (((int64_t)(x)) * (f) / (y))
#define DIV_FP_INT(x, n) ((x) / (n))
//Custom nested FP operations
#define DIV_INT_INT(m, n) DIV_FP_FP(CONVERT_INT_FP(m), CONVERT_INT_FP(n))
#endif