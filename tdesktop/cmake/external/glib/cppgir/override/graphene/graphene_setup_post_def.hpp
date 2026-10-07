#pragma once

// + typedef __m128 graphene_simd4f_t;
//   so it comes with (vector) attributes
// + some functions return bool rather than gboolean
//   so the reconstructed signature does not quite match
#define GI_INCLUDE_GRAPHENE_BEGIN \
  _Pragma("GCC diagnostic push") \
      _Pragma("GCC diagnostic ignored \"-Wignored-attributes\"")

#define GI_INCLUDE_GRAPHENE_END _Pragma("GCC diagnostic pop")

#define GI_INCLUDE_IMPL_GRAPHENE_BEGIN \
  GI_INCLUDE_GRAPHENE_BEGIN \
  _Pragma("GCC diagnostic push") \
      _Pragma("GCC diagnostic ignored \"-Wcast-function-type\"")

#define GI_INCLUDE_IMPL_GRAPHENE_END \
  GI_INCLUDE_GRAPHENE_END \
  GI_INCLUDE_GRAPHENE_END
