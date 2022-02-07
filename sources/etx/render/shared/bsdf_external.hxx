#pragma once

//
// Multiple-Scattering Microfacet BSDFs with the Smith Model
// Eric Heitz, Johannes Hanika, Eugene d’Eon and Carsten Dachsbacher
//
// https://eheitzresearch.wordpress.com/240-2/
//
// (adapted)

namespace etx::external {

constexpr uint32_t kScatteringOrderMax = 16u;

struct RayInfo {
  float3 w;
  float theta;
  float cosTheta;
  float sinTheta;
  float tanTheta;
  float alpha;
  float Lambda;
  float h;
  float C1;
  float G1;

  RayInfo(const float3& w, const float alpha_x, const float alpha_y) {
    updateDirection(w, alpha_x, alpha_y);
  }

  ETX_GPU_CODE void updateDirection(const float3& in_w, const float alpha_x, const float alpha_y) {
    w = in_w;
    theta = acosf(w.z);
    cosTheta = w.z;
    sinTheta = sinf(theta);
    tanTheta = sinTheta / cosTheta;
    const float invSinTheta2 = 1.0f / (1.0f - w.z * w.z);
    const float cosPhi2 = w.x * w.x * invSinTheta2;
    const float sinPhi2 = w.y * w.y * invSinTheta2;
    alpha = sqrtf(cosPhi2 * alpha_x * alpha_x + sinPhi2 * alpha_y * alpha_y);
    // Lambda
    if (w.z > 0.9999f)
      Lambda = 0.0f;
    else if (w.z < -0.9999f)
      Lambda = -1.0f;
    else {
      const float a = 1.0f / tanTheta / alpha;
      Lambda = 0.5f * (-1.0f + ((a > 0) ? 1.0f : -1.0f) * sqrtf(1.0f + 1.0f / (a * a)));
    }
  }

  ETX_GPU_CODE void updateHeight(const float& in_h) {
    h = in_h;
    C1 = min(1.0f, max(0.0f, 0.5f * (h + 1.0f)));

    if (w.z > 0.9999f)
      G1 = 1.0f;
    else if (w.z <= 0.0f)
      G1 = 0.0f;
    else
      G1 = powf(C1, Lambda);
  }
};

ETX_GPU_CODE float invC1(const float U) {
  return max(-1.0f, min(1.0f, 2.0f * U - 1.0f));
}

ETX_GPU_CODE float sampleHeight(const RayInfo& ray, const float U) {
  if (ray.w.z > 0.9999f)
    return kMaxFloat;

  if (ray.w.z < -0.9999f) {
    const float value = invC1(U * ray.C1);
    return value;
  }
  if (fabsf(ray.w.z) < 0.0001f)
    return ray.h;

  // probability of intersection
  if (U > 1.0f - ray.G1)  // leave the microsurface
    return kMaxFloat;

  return invC1(ray.C1 / powf((1.0f - U), 1.0f / ray.Lambda));
}

ETX_GPU_CODE float D_ggx(const float3& wm, const float alpha_x, const float alpha_y) {
  if (wm.z <= 0.0f)
    return 0.0f;

  // slope of wm
  const float slope_x = -wm.x / wm.z;
  const float slope_y = -wm.y / wm.z;

  // P22
  const float tmp = 1.0f + slope_x * slope_x / (alpha_x * alpha_x) + slope_y * slope_y / (alpha_y * alpha_y);
  const float P22 = 1.0f / (kPi * alpha_x * alpha_y) / (tmp * tmp);

  // value
  return P22 / (wm.z * wm.z * wm.z * wm.z);
}

ETX_GPU_CODE float2 sampleP22_11(const float theta_i, const float U, const float U_2, const float alpha_x, const float alpha_y) {
  float2 slope = {};

  if (theta_i < 0.0001f) {
    const float r = sqrtf(U / (1.0f - U));
    const float phi = 6.28318530718f * U_2;
    slope.x = r * cosf(phi);
    slope.y = r * sinf(phi);
    return slope;
  }

  // constant
  const float sin_theta_i = sinf(theta_i);
  const float cos_theta_i = cosf(theta_i);
  const float tan_theta_i = sin_theta_i / cos_theta_i;

  // slope associated to theta_i
  const float slope_i = cos_theta_i / sin_theta_i;

  // projected area
  const float projectedarea = 0.5f * (cos_theta_i + 1.0f);
  if (projectedarea < 0.0001f || projectedarea != projectedarea)
    return {};

  // normalization coefficient
  const float c = 1.0f / projectedarea;

  const float A = 2.0f * U / cos_theta_i / c - 1.0f;
  const float B = tan_theta_i;
  const float tmp = 1.0f / (A * A - 1.0f);

  const float D = sqrtf(max(0.0f, B * B * tmp * tmp - (A * A - B * B) * tmp));
  const float slope_x_1 = B * tmp - D;
  const float slope_x_2 = B * tmp + D;
  slope.x = (A < 0.0f || slope_x_2 > 1.0f / tan_theta_i) ? slope_x_1 : slope_x_2;

  float U2;
  float S;
  if (U_2 > 0.5f) {
    S = 1.0f;
    U2 = 2.0f * (U_2 - 0.5f);
  } else {
    S = -1.0f;
    U2 = 2.0f * (0.5f - U_2);
  }
  const float z = (U2 * (U2 * (U2 * 0.27385f - 0.73369f) + 0.46341f)) / (U2 * (U2 * (U2 * 0.093073f + 0.309420f) - 1.000000f) + 0.597999f);
  slope.y = S * z * sqrtf(1.0f + slope.x * slope.x);

  return slope;
}

ETX_GPU_CODE float3 sampleVNDF(Sampler& smp, const float3& wi, const float alpha_x, const float alpha_y) {
  const float U1 = smp.next();
  const float U2 = smp.next();

  // sample D_wi

  // stretch to match configuration with alpha=1.0
  const float3 wi_11 = normalize(float3(alpha_x * wi.x, alpha_y * wi.y, wi.z));

  // sample visible slope with alpha=1.0
  float2 slope_11 = sampleP22_11(acosf(wi_11.z), U1, U2, alpha_x, alpha_y);

  // align with view direction
  const float phi = atan2(wi_11.y, wi_11.x);
  float2 slope(cosf(phi) * slope_11.x - sinf(phi) * slope_11.y, sinf(phi) * slope_11.x + cosf(phi) * slope_11.y);

  // stretch back
  slope.x *= alpha_x;
  slope.y *= alpha_y;

  // if numerical instability
  if ((slope.x != slope.x) || isinf(slope.x)) {
    if (wi.z > 0)
      return float3(0.0f, 0.0f, 1.0f);
    else
      return normalize(float3(wi.x, wi.y, 0.0f));
  }

  return normalize(float3(-slope.x, -slope.y, 1.0f));
}

ETX_GPU_CODE SpectralResponse evalPhaseFunction_conductor(SpectralQuery spect, const RayInfo& ray, const float3& wo, const float alpha_x, const float alpha_y,
  const RefractiveIndex::Sample& ext_ior, const RefractiveIndex::Sample& int_ior) {
  if (ray.w.z > 0.9999f)
    return {spect.wavelength, 0.0f};

  // half float3
  const float3 wh = normalize(-ray.w + wo);
  if (wh.z < 0.0f)
    return {spect.wavelength, 0.0f};

  // projected area
  float projectedArea;
  if (ray.w.z < -0.9999f)
    projectedArea = 1.0f;
  else
    projectedArea = ray.Lambda * ray.w.z;

  // value
  return fresnel::conductor(spect, dot(-ray.w, wh), ext_ior, int_ior) * max(0.0f, dot(-ray.w, wh)) * D_ggx(wh, alpha_x, alpha_y) / 4.0f / projectedArea / dot(-ray.w, wh);
}

ETX_GPU_CODE float3 samplePhaseFunction_conductor(SpectralQuery spect, Sampler& smp, const float3& wi, const float alpha_x, const float alpha_y,
  const RefractiveIndex::Sample& ext_ior, const RefractiveIndex::Sample& int_ior, SpectralResponse& weight) {
  const float U1 = smp.next();
  const float U2 = smp.next();

  // sample D_wi
  // stretch to match configuration with alpha=1.0
  const float3 wi_11 = normalize(float3(alpha_x * wi.x, alpha_y * wi.y, wi.z));

  // sample visible slope with alpha=1.0
  float2 slope_11 = sampleP22_11(acosf(wi_11.z), U1, U2, alpha_x, alpha_y);

  // align with view direction
  const float phi = atan2(wi_11.y, wi_11.x);
  float2 slope = {cosf(phi) * slope_11.x - sinf(phi) * slope_11.y, sinf(phi) * slope_11.x + cosf(phi) * slope_11.y};

  // stretch back
  slope.x *= alpha_x;
  slope.y *= alpha_y;

  // compute normal
  float3 wm;
  // if numerical instability
  if ((slope.x != slope.x) || isinf(slope.x)) {
    if (wi.z > 0)
      wm = float3(0.0f, 0.0f, 1.0f);
    else
      wm = normalize(float3(wi.x, wi.y, 0.0f));
  } else
    wm = normalize(float3(-slope.x, -slope.y, 1.0f));

  // reflect
  const float3 wo = -wi + 2.0f * wm * dot(wi, wm);
  weight = fresnel::conductor(spect, dot(wi, wm), ext_ior, int_ior);

  return wo;
}

ETX_GPU_CODE float3 sample_conductor(SpectralQuery spect, Sampler& smp, const float3& wi, const float alpha_x, const float alpha_y, const RefractiveIndex::Sample& ext_ior,
  const RefractiveIndex::Sample& int_ior, SpectralResponse& energy) {
  energy = {spect.wavelength, 1.0f};

  // init
  RayInfo ray = {-wi, alpha_x, alpha_y};
  ray.updateHeight(1.0f);

  // random walk
  uint32_t current_scatteringOrder = 0;
  while (true) {
    // next height
    ray.updateHeight(sampleHeight(ray, smp.next()));

    // leave the microsurface?
    if (ray.h == kMaxFloat) {
      break;
    } else {
      current_scatteringOrder++;
    }

    // next direction
    SpectralResponse weight;
    ray.updateDirection(samplePhaseFunction_conductor(spect, smp, -ray.w, alpha_x, alpha_y, ext_ior, int_ior, weight), alpha_x, alpha_y);
    energy = energy * weight;
    ray.updateHeight(ray.h);

    // if NaN (should not happen, just in case)
    if ((ray.h != ray.h) || (ray.w.x != ray.w.x)) {
      energy = {spect.wavelength, 0.0f};
      return float3(0, 0, 1);
    }

    if (current_scatteringOrder > kScatteringOrderMax) {
      energy = {spect.wavelength, 0.0f};
      return float3(0, 0, 1);
    }
  }

  return ray.w;
}

// MIS weights for bidirectional path tracing on the microsurface
ETX_GPU_CODE float MISweight_conductor(const float3& wi, const float3& wo, const float alpha_x, const float alpha_y) {
  if (wi.x == -wo.x && wi.y == -wo.y && wi.z == -wo.z)
    return 1.0f;
  const float3 wh = normalize(wi + wo);
  return D_ggx((wh.z > 0) ? wh : -wh, alpha_x, alpha_y);
}

ETX_GPU_CODE SpectralResponse eval_conductor(SpectralQuery spect, Sampler& smp, const float3& wi, const float3& wo, const float alpha_x, const float alpha_y,
  const RefractiveIndex::Sample& ext_ior, const RefractiveIndex::Sample& int_ior) {
  if (wi.z <= 0 || wo.z <= 0)
    return {spect.wavelength, 0.0f};

  // init
  RayInfo ray = {-wi, alpha_x, alpha_y};
  ray.updateHeight(1.0f);
  SpectralResponse energy = {spect.wavelength, 1.0f};

  RayInfo ray_shadowing = {wo, alpha_x, alpha_y};

  // eval single scattering
  const float3 wh = normalize(wi + wo);
  const float D = D_ggx(wh, alpha_x, alpha_y);
  const float G2 = 1.0f / (1.0f + (-ray.Lambda - 1.0f) + ray_shadowing.Lambda);
  SpectralResponse singleScattering = fresnel::conductor(spect, dot(-ray.w, wh), ext_ior, int_ior) * D * G2 / (4.0f * wi.z);

  // MIS weight
  float wi_MISweight;

  // multiple scattering
  SpectralResponse multipleScattering = {spect.wavelength, 0.0f};

  // random walk
  uint32_t current_scatteringOrder = 0;
  while (current_scatteringOrder < kScatteringOrderMax) {
    // next height
    ray.updateHeight(sampleHeight(ray, smp.next()));

    // leave the microsurface?
    if (ray.h == kMaxFloat) {
      break;
    } else {
      current_scatteringOrder++;
    }

    // next event estimation
    if (current_scatteringOrder > 1)  // single scattering is already computed
    {
      SpectralResponse phasefunction = evalPhaseFunction_conductor(spect, ray, wo, alpha_x, alpha_y, ext_ior, int_ior);
      ray_shadowing.updateHeight(ray.h);
      float shadowing = ray_shadowing.G1;
      SpectralResponse I = energy * phasefunction * shadowing;

      // MIS
      const float MIS = wi_MISweight / (wi_MISweight + MISweight_conductor(-ray.w, wo, alpha_x, alpha_y));

      if (I.valid())
        multipleScattering += I * MIS;
    }

    // next direction
    SpectralResponse weight;
    ray.updateDirection(samplePhaseFunction_conductor(spect, smp, -ray.w, alpha_x, alpha_y, ext_ior, int_ior, weight), alpha_x, alpha_y);
    energy = energy * weight;
    ray.updateHeight(ray.h);

    if (current_scatteringOrder == 1)
      wi_MISweight = MISweight_conductor(wi, ray.w, alpha_x, alpha_y);

    // if NaN (should not happen, just in case)
    if ((ray.h != ray.h) || (ray.w.x != ray.w.x))
      return {spect.wavelength, 0.0f};
  }

  // 0.5f = MIS weight of singleScattering
  // multipleScattering already weighted by MIS
  return 0.5f * singleScattering + multipleScattering;
}

}  // namespace etx::external
