/* Implementation of Fixed Point Real Arithmetic
Floating pt operations are done by simulation using
17.14 (P.Q) Fixed point number representations,
where P are digits before decimal pt & Q digits are after it.
Implemented as MACROS for faster processing
*/
#ifndef FP_MATH_H
#define FP_MATH_H

/*Q, which is 2^14*/
#define f 1 << (14)

//Conversions
/*Convert n to fixed point.*/
#define CONVERT_INT_FP(n) ((n) * (f))
/*Convert x to integer (rounding towards zero).*/
#define CONVERT_FP_INT(x) ((x) >= 0 ? ((x) + (f) / 2) / (f) : ((x) - (f) / 2) / (f))
/*Convert x to integer (rounding towards nearest).*/
#define CONVERT_FP_INT_FLOOR(x) ((x) / (f))
// Additions & Subtractions
/*Add x and y.*/
#define ADD_FP_FP(x, y) ((x) + (y))
/*Subtract y from x.*/
#define SUB_FP_FP(x, y) ((x) - (y))
/*Add x and n, converts n to FP.*/
#define ADD_FP_INT(x, n) ((x) + CONVERT_INT_FP(n))
/*Add nn from x, convert n to FP.*/
#define SUB_FP_INT(x, n) ((x) - CONVERT_INT_FP(n))

//Mult & Div
/*Multiplies x and y, as a 64 bit operation.*/
#define MULT_FP_FP(x, y) (((int64_t)(x)) * (y) / (f))
/*Multiplies x and n*/
#define MULT_FP_INT(x, n) ((x) * (n))
/*Divides x by y, as a 64 bit operation.*/
#define DIV_FP_FP(x, y) (((int64_t)(x)) * (f) / (y))
/*Divides x by n.*/
#define DIV_FP_INT(x, n) ((x) / (n))

//Custom nested FP operations
/*Divides m and n. Since output is FP type, converts both to FP and division is in FP*/
#define DIV_INT_INT(m, n) DIV_FP_FP(CONVERT_INT_FP(m), CONVERT_INT_FP(n))
#endif