// Copyright 2015 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
// SSE2 version of cost functions
//
// Author: Skal (pascal.massimino@gmail.com)

#include "./dsp.h"

#if defined(WEBP_USE_SSE2)
#include <emmintrin.h>

#include "../enc/cost.h"
#include "../enc/vp8enci.h"
#include "../utils/utils.h"

//------------------------------------------------------------------------------

static void SetResidualCoeffsSSE2(const int16_t* const coeffs,
                                  VP8Residual* const res) {
  const __m128i c0 = _mm_loadu_si128((const __m128i*)coeffs);
  const __m128i c1 = _mm_loadu_si128((const __m128i*)(coeffs + 8));
  // Use SSE to compare 8 values with a single instruction.
  const __m128i zero = _mm_setzero_si128();
  const __m128i m0 = _mm_cmpeq_epi16(c0, zero);
  const __m128i m1 = _mm_cmpeq_epi16(c1, zero);
  // Get the comparison results as a bitmask, consisting of two times 16 bits:
  // two identical bits for each result. Concatenate both bitmasks to get a
  // single 32 bit value. Negate the mask to get the position of entries that
  // are not equal to zero. We don't need to mask out least significant bits
  // according to res->first, since coeffs[0] is 0 if res->first > 0
  const uint32_t mask =
      ~(((uint32_t)_mm_movemask_epi8(m1) << 16) | _mm_movemask_epi8(m0));
  // The position of the most significant non-zero bit indicates the position of
  // the last non-zero value. Divide the result by two because __movemask_epi8
  // operates on 8 bit values instead of 16 bit values.
  assert(res->first == 0 || coeffs[0] == 0);
  res->last = mask ? (BitsLog2Floor(mask) >> 1) : -1;
  res->coeffs = coeffs;
}

static int GetResidualCostSSE2(int ctx0, const VP8Residual* const res) {
  uint8_t levels[16], ctxs[16];
  uint16_t abs_levels[16];
  int n = res->first;
  // should be prob[VP8EncBands[n]], but it's equivalent for n=0 or 1
  const int p0 = res->prob[n][ctx0][0];
  const uint16_t* t = res->cost[n][ctx0];
  // bit_cost(1, p0) is already incorporated in t[] tables, but only if ctx != 0
  // (as required by the syntax). For ctx0 == 0, we need to add it here or it'll
  // be missing during the loop.
  int cost = (ctx0 == 0) ? VP8BitCost(1, p0) : 0;

  if (res->last < 0) {
    return VP8BitCost(0, p0);
  }

  {   // precompute clamped levels and contexts, packed to 8b.
    const __m128i zero = _mm_setzero_si128();
    const __m128i kCst2 = _mm_set1_epi8(2);
    const __m128i kCst67 = _mm_set1_epi8(MAX_VARIABLE_LEVEL);
    const __m128i c0 = _mm_loadu_si128((const __m128i*)&res->coeffs[0]);
    const __m128i c1 = _mm_loadu_si128((const __m128i*)&res->coeffs[8]);
    const __m128i D0_m = _mm_min_epi16(c0, zero);
    const __m128i D0_p = _mm_max_epi16(c0, zero);
    const __m128i D1_m = _mm_min_epi16(c1, zero);
    const __m128i D1_p = _mm_max_epi16(c1, zero);
    const __m128i E0 = _mm_sub_epi16(D0_p, D0_m);   // abs(v), 16b
    const __m128i E1 = _mm_sub_epi16(D1_p, D1_m);
    const __m128i F = _mm_packs_epi16(E0, E1);
    const __m128i G = _mm_min_epu8(F, kCst2);    // context = 0,1,2
    const __m128i H = _mm_min_epu8(F, kCst67);   // clamp_level in [0..67]

    _mm_storeu_si128((__m128i*)&ctxs[0], G);
    _mm_storeu_si128((__m128i*)&levels[0], H);

    _mm_storeu_si128((__m128i*)&abs_levels[0], E0);
    _mm_storeu_si128((__m128i*)&abs_levels[8], E1);
  }
  for (; n < res->last; ++n) {
    const int ctx = ctxs[n];
    const int level = levels[n];
    const int flevel = abs_levels[n];   // full level
    const int b = VP8EncBands[n + 1];
    cost += VP8LevelFixedCosts[flevel] + t[level];  // simplified VP8LevelCost()
    t = res->cost[b][ctx];
  }
  // Last coefficient is always non-zero
  {
    const int level = levels[n];
    const int flevel = abs_levels[n];
    assert(flevel != 0);
    cost += VP8LevelFixedCosts[flevel] + t[level];
    if (n < 15) {
      const int b = VP8EncBands[n + 1];
      const int ctx = ctxs[n];
      const int last_p0 = res->prob[b][ctx][0];
      cost += VP8BitCost(0, last_p0);
    }
  }
  return cost;
}
#endif   // WEBP_USE_SSE2

//------------------------------------------------------------------------------
// Entry point

extern void VP8EncDspCostInitSSE2(void);

WEBP_TSAN_IGNORE_FUNCTION void VP8EncDspCostInitSSE2(void) {
#if defined(WEBP_USE_SSE2)
  VP8SetResidualCoeffs = SetResidualCoeffsSSE2;
  VP8GetResidualCost = GetResidualCostSSE2;
#endif   // WEBP_USE_SSE2
}
