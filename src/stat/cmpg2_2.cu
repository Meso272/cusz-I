/**
 * @file cmp2g.cu
 * @author Jiannan Tian
 * @brief (split to speed up buid process; part 2)
 * @version 0.3
 * @date 2022-11-03
 *
 * (C) 2022 by Indiana University, Argonne National Laboratory
 *
 */

#include "detail/equal_thrust.inl"
#include "stat/compare.h"
#include "stat/compare_gpu.hh"

#define THRUSTGPU_COMPARE_LOSSLESS(Tliteral, T) \
    template bool psz::thrustgpu_identical<T>(T * d1, T * d2, size_t const len);

THRUSTGPU_COMPARE_LOSSLESS(fp32, float)
THRUSTGPU_COMPARE_LOSSLESS(fp64, double)

#undef THRUSTGPU_COMPARE_LOSSLESS
