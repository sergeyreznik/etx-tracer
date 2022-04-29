﻿#pragma once

#include <etx/core/debug.hxx>

#if (ETX_NVCC_COMPILER)
#define ETX_GPU_CODE inline __device__
#define ETX_GPU_DATA __device__
#define ETX_CPU_CODE __host__
#define ETX_INIT_WITH(S)
#else
#define ETX_GPU_CODE inline
#define ETX_GPU_DATA
#define ETX_CPU_CODE
#define ETX_INIT_WITH(S) = S
#endif

#define ETX_ALIGNED alignas(16)

#define ETX_EMPTY_INIT ETX_INIT_WITH({})

#define ETX_FORCE_VALIDATION 0

#define ETX_RENDER_BASE_INCLUDED 1
#include <etx/render/shared/math.hxx>
#undef ETX_RENDER_BASE_INCLUDED

#if (ETX_DEBUG || ETX_FORCE_VALIDATION)

#define ETX_VALIDATE(VALUE)                                         \
  do {                                                              \
    if (valid_value((VALUE)) == false) {                            \
      printf("Validation failed [%s, %u]:\n ", __FILE__, __LINE__); \
      print_value(#VALUE, "invalid value", VALUE);                  \
      ETX_DEBUG_BREAK();                                            \
    }                                                               \
  } while (0)

#else

#define ETX_VALIDATE(VALUE) \
  do {                      \
  } while (0)

#endif

namespace etx {

template <class T>
struct ETX_ALIGNED ArrayView {
  T* a ETX_EMPTY_INIT;
  uint64_t count ETX_EMPTY_INIT;

  ArrayView() = default;

  ETX_GPU_CODE ArrayView(T* p, uint64_t c)
    : a(p)
    , count(c) {
  }

  ETX_GPU_CODE const T& operator[](uint64_t i) const {
    ETX_ASSERT(count > 0);
    ETX_ASSERT(a != nullptr);
    ETX_ASSERT(i < count);
    return a[i];
  }

  ETX_GPU_CODE T& operator[](uint64_t i) {
    ETX_ASSERT(count > 0);
    ETX_ASSERT(a != nullptr);
    ETX_ASSERT(i < count);
    return a[i];
  }
};

template <class T>
struct Pointer {
  T* ptr ETX_EMPTY_INIT;

  Pointer() = default;

  ETX_GPU_CODE Pointer(T* p)
    : ptr(p) {
  }

  ETX_GPU_CODE T* operator->() {
    ETX_ASSERT(ptr != nullptr);
    return ptr;
  }

  ETX_GPU_CODE T* operator->() const {
    ETX_ASSERT(ptr != nullptr);
    return ptr;
  }
};

template <class T>
ETX_GPU_CODE ArrayView<T> make_array_view(void* p, uint64_t count) {
  return {reinterpret_cast<T*>(p), count};
}

ETX_GPU_CODE void print_value(const char* name, const char* tag, float t) {
  printf("%s : %s %f\n", name, tag, t);
}

ETX_GPU_CODE void print_value(const char* name, const char* tag, const float2& v) {
  printf("%s : %s (%f, %f)\n", name, tag, v.x, v.y);
}

ETX_GPU_CODE void print_value(const char* name, const char* tag, const float3& v) {
  printf("%s : %s (%f, %f, %f)\n", name, tag, v.x, v.y, v.z);
}

ETX_GPU_CODE void print_value(const char* name, const char* tag, const float4& v) {
  printf("%s : %s (%f, %f, %f, %f)\n", name, tag, v.x, v.y, v.z, v.w);
}

ETX_GPU_CODE void print_value(const char* name, const char* tag, complex z) {
  printf("%s : %s %f + i * %f\n", name, tag, z.real(), z.imag());
}

}  // namespace etx
