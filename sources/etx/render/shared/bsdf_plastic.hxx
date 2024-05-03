namespace etx {

namespace DeltaPlasticBSDF {

ETX_GPU_CODE BSDFSample sample(const BSDFData& data, const Material& mtl, const Scene& scene, Sampler& smp) {
  auto frame = data.get_normal_frame();

  auto eta_e = mtl.ext_ior(data.spectrum_sample);
  auto eta_i = mtl.int_ior(data.spectrum_sample);

  auto thinfilm = evaluate_thinfilm(data.spectrum_sample, mtl.thinfilm, data.tex, scene);
  auto fr = fresnel::calculate(data.spectrum_sample, dot(data.w_i, frame.nrm), eta_e, eta_i, thinfilm);
  auto f = fr.monochromatic();

  bool reflection = smp.next() <= f;

  BSDFSample result;
  if (reflection) {
    result.w_o = normalize(reflect(data.w_i, frame.nrm));
    result.properties = BSDFSample::Reflection;
  } else {
    result.w_o = sample_cosine_distribution(smp.next_2d(), frame.nrm, 1.0f);
    result.properties = BSDFSample::Diffuse | BSDFSample::Reflection;
  }

  float n_dot_o = dot(frame.nrm, result.w_o);
  auto diffuse = apply_image(data.spectrum_sample, mtl.diffuse, data.tex, scene, nullptr);
  auto specular = apply_image(data.spectrum_sample, mtl.specular, data.tex, scene, nullptr);

  if (reflection) {
    result.weight = specular * fr / f;
    ETX_VALIDATE(result.weight);
    result.pdf = kMaxHalf;
  } else {
    result.weight = diffuse * (1.0f - fr) / (1.0f - f);
    result.pdf = kInvPi * n_dot_o * (1.0f - f);
  }

  return result;
}

ETX_GPU_CODE BSDFEval evaluate(const BSDFData& data, const float3& w_o, const Material& mtl, const Scene& scene, Sampler& smp) {
  auto frame = data.get_normal_frame();

  float n_dot_o = dot(frame.nrm, w_o);
  if (n_dot_o <= kEpsilon) {
    return {data.spectrum_sample, 0.0f};
  }

  float3 m = normalize(w_o - data.w_i);
  auto eta_e = mtl.ext_ior(data.spectrum_sample);
  auto eta_i = mtl.int_ior(data.spectrum_sample);
  auto thinfilm = evaluate_thinfilm(data.spectrum_sample, mtl.thinfilm, data.tex, scene);
  auto inv_fr = 1.0f - fresnel::calculate(data.spectrum_sample, dot(data.w_i, m), eta_e, eta_i, thinfilm);

  auto diffuse = apply_image(data.spectrum_sample, mtl.diffuse, data.tex, scene, nullptr);

  BSDFEval result;
  result.func = diffuse * (kInvPi * inv_fr);
  ETX_VALIDATE(result.func);
  result.bsdf = diffuse * (kInvPi * n_dot_o * inv_fr);
  ETX_VALIDATE(result.bsdf);
  result.weight = diffuse;
  ETX_VALIDATE(result.weight);
  result.pdf = kInvPi * n_dot_o * inv_fr.monochromatic();
  ETX_VALIDATE(result.pdf);
  return result;
}

ETX_GPU_CODE float pdf(const BSDFData& data, const float3& w_o, const Material& mtl, const Scene& scene, Sampler& smp) {
  auto frame = data.get_normal_frame();

  float n_dot_o = dot(frame.nrm, w_o);
  if (n_dot_o <= kEpsilon) {
    return 0.0f;
  }

  float3 m = normalize(w_o - data.w_i);
  auto eta_e = mtl.ext_ior(data.spectrum_sample);
  auto eta_i = mtl.int_ior(data.spectrum_sample);
  auto thinfilm = evaluate_thinfilm(data.spectrum_sample, mtl.thinfilm, data.tex, scene);
  auto fr = fresnel::calculate(data.spectrum_sample, dot(data.w_i, m), eta_e, eta_i, thinfilm);
  return kInvPi * n_dot_o * (1.0f - fr.monochromatic());
}
}  // namespace DeltaPlasticBSDF

namespace PlasticBSDF {

ETX_GPU_CODE BSDFSample sample(const BSDFData& data, const Material& mtl, const Scene& scene, Sampler& smp) {
  if (dot(mtl.roughness, float2{0.5f, 0.5f}) <= kDeltaAlphaTreshold) {
    return DeltaPlasticBSDF::sample(data, mtl, scene, smp);
  }

  auto frame = data.get_normal_frame();

  auto ggx = NormalDistribution(frame, mtl.roughness);
  auto m = ggx.sample(smp, data.w_i);

  auto eta_e = mtl.ext_ior(data.spectrum_sample);
  auto eta_i = mtl.int_ior(data.spectrum_sample);
  auto thinfilm = evaluate_thinfilm(data.spectrum_sample, mtl.thinfilm, data.tex, scene);
  auto f = fresnel::calculate(data.spectrum_sample, dot(data.w_i, m), eta_e, eta_i, thinfilm);

  uint32_t properties = 0u;
  float3 w_o = {};
  if (smp.next() <= f.monochromatic()) {
    w_o = normalize(reflect(data.w_i, m));
    properties = BSDFSample::Reflection;
  } else {
    w_o = sample_cosine_distribution(smp.next_2d(), frame.nrm, 1.0f);
    properties = BSDFSample::Reflection | BSDFSample::Diffuse;
  }

  return {w_o, evaluate(data, w_o, mtl, scene, smp), properties};
}

ETX_GPU_CODE BSDFEval evaluate(const BSDFData& data, const float3& w_o, const Material& mtl, const Scene& scene, Sampler& smp) {
  if (dot(mtl.roughness, float2{0.5f, 0.5f}) <= kDeltaAlphaTreshold) {
    return DeltaPlasticBSDF::evaluate(data, w_o, mtl, scene, smp);
  }

  auto frame = data.get_normal_frame();

  float n_dot_o = dot(frame.nrm, w_o);
  float n_dot_i = -dot(frame.nrm, data.w_i);

  float3 m = normalize(w_o - data.w_i);
  float m_dot_o = dot(m, w_o);

  if ((n_dot_o <= kEpsilon) || (n_dot_i <= kEpsilon) || (m_dot_o <= kEpsilon)) {
    return {data.spectrum_sample, 0.0f};
  }

  auto eta_e = mtl.ext_ior(data.spectrum_sample);
  auto eta_i = mtl.int_ior(data.spectrum_sample);
  auto thinfilm = evaluate_thinfilm(data.spectrum_sample, mtl.thinfilm, data.tex, scene);
  auto fr = fresnel::calculate(data.spectrum_sample, dot(data.w_i, m), eta_e, eta_i, thinfilm);
  auto f = fr.monochromatic();

  auto ggx = NormalDistribution(frame, mtl.roughness);
  auto eval = ggx.evaluate(m, data.w_i, w_o);
  float j = 1.0f / (4.0f * m_dot_o);

  float sample_pdf = kInvPi * n_dot_o * (1.0f - f) + eval.pdf * j * f;
  ETX_VALIDATE(sample_pdf);

  if (sample_pdf <= kEpsilon) {
    return {data.spectrum_sample, 0.0f};
  }

  auto diffuse = apply_image(data.spectrum_sample, mtl.diffuse, data.tex, scene, nullptr);
  auto specular = apply_image(data.spectrum_sample, mtl.specular, data.tex, scene, nullptr);

  BSDFEval result;
  result.func = diffuse * (kInvPi * (1.0f - fr)) + specular * (fr * eval.ndf * eval.visibility / (4.0f * n_dot_i * n_dot_o));
  ETX_VALIDATE(result.func);
  result.bsdf = diffuse * (kInvPi * n_dot_o * (1.0f - fr)) + specular * (fr * eval.ndf * eval.visibility / (4.0f * n_dot_i));
  ETX_VALIDATE(result.bsdf);
  result.pdf = sample_pdf;
  ETX_VALIDATE(result.pdf);
  result.weight = result.bsdf / result.pdf;
  ETX_VALIDATE(result.weight);
  return result;
}

ETX_GPU_CODE float pdf(const BSDFData& data, const float3& w_o, const Material& mtl, const Scene& scene, Sampler& smp) {
  if (dot(mtl.roughness, float2{0.5f, 0.5f}) <= kDeltaAlphaTreshold) {
    return DeltaPlasticBSDF::pdf(data, w_o, mtl, scene, smp);
  }

  auto frame = data.get_normal_frame();

  float3 m = normalize(w_o - data.w_i);
  float m_dot_o = dot(m, w_o);
  float n_dot_o = dot(frame.nrm, w_o);

  if ((n_dot_o <= kEpsilon) || (m_dot_o <= kEpsilon)) {
    return 0.0f;
  }

  auto eta_e = mtl.ext_ior(data.spectrum_sample);
  auto eta_i = mtl.int_ior(data.spectrum_sample);
  auto thinfilm = evaluate_thinfilm(data.spectrum_sample, mtl.thinfilm, data.tex, scene);
  auto fr = fresnel::calculate(data.spectrum_sample, dot(data.w_i, m), eta_e, eta_i, thinfilm);
  auto f = fr.monochromatic();

  auto ggx = NormalDistribution(frame, mtl.roughness);

  float j = 1.0f / (4.0f * m_dot_o);
  float result = kInvPi * n_dot_o * (1.0f - f) + ggx.pdf(m, data.w_i, w_o) * j * f;
  ETX_VALIDATE(result);
  return result;
}

ETX_GPU_CODE bool is_delta(const Material& material, const float2& tex, const Scene& scene, Sampler& smp) {
  return false;
}

ETX_GPU_CODE SpectralResponse albedo(const BSDFData& data, const Material& mtl, const Scene& scene, Sampler& smp) {
  return apply_image(data.spectrum_sample, mtl.diffuse, data.tex, scene, nullptr);
}

}  // namespace PlasticBSDF

}  // namespace etx
