#ifndef __LIB_KERNEL_FIXED_H
#define __LIB_KERNEL_FIXED_H

/* Fixed-Point Arithmetic Library. (17.14 format)

   Represents numbers using 32-bit integers with:
   - 1 sign bit.
   - 17 integer bits. (including sign)
   - 14 fractional bits.
   Total range: ±131072.0 with ~0.000061 precision. */

#include <stdint.h>

typedef int32_t fp32_t;
#define FPT_BITS 32
#define FPT_P 17
#define FPT_Q 14
#define FPT_F (1 << FPT_Q)

/* Conversion .*/
/* Convert fixed point to integer. */
#define INT_TO_FP(x) ((fp32_t)((x) * FPT_F))
/* Convert fixed-point to integer. (truncate toward zero) */
#define FP_TO_INT(x) ((x) / FPT_F)
/* Convert fixed-point to integer. (round to nearest) */
#define FP_TO_INT_ROUND(x)                                                    \
  ((int)(((x) + ((x) >= 0 ? FPT_F / 2 : -FPT_F / 2)) / FPT_F))

/* Arithmetic. */
#define FP_ADD(x, y) ((x) + (y))
#define FP_ADD_INT(x, n) ((x) + (n) * FPT_F)
#define FP_SUB(x, y) ((x) - (y))
#define FP_SUB_INT(x, n) ((x) - (n) * FPT_F)
#define FP_MUL(x, y) ((fp32_t)(((int64_t)(x) * (y)) / FPT_F))
#define FP_MUL_INT(x, n) ((x) * (n))
#define FP_DIV(x, y) ((fp32_t)(((int64_t)(x) * FPT_F) / (y)))
#define FP_DIV_INT(x, n) ((x) / (n))

/* Comparison. */

#define FP_EQ(x, y) ((x) == (y))
#define FP_LS(x, y) ((x) < (y))
#define FP_GT(x, y) ((x) > (y))

#endif /* lib/kernel/fixed.h */
