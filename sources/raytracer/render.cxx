﻿#include <etx/render/host/image_pool.hxx>

#include "render.hxx"

#include <sokol_app.h>
#include <sokol_gfx.h>

#include <vector>

namespace etx {

extern const char* shader_source;

struct ShaderConstants {
  float4 dimensions = {};
  uint32_t image_view = 0;
  uint32_t options = ViewOptions::ToneMapping;
  float exposure = 1.0f;
  float pad = 0.0f;
};

struct RenderContextImpl {
  RenderContextImpl(TaskScheduler& s)
    : image_pool(s) {
  }

  sg_shader output_shader = {};
  sg_pipeline output_pipeline = {};
  sg_image sample_image = {};
  sg_image light_image = {};
  sg_image reference_image = {};
  ShaderConstants constants;
  uint32_t def_image_handle = kInvalidIndex;
  uint32_t ref_image_handle = kInvalidIndex;
  ViewOptions view_options = {};
  uint2 output_dimensions = {};
  ImagePool image_pool;

  std::vector<float4> black_image;
};

ETX_PIMPL_IMPLEMENT(RenderContext, Impl);

RenderContext::RenderContext(TaskScheduler& s) {
  ETX_PIMPL_INIT(RenderContext, s);
}

RenderContext::~RenderContext() {
  ETX_PIMPL_CLEANUP(RenderContext);
}

void RenderContext::init() {
  _private->image_pool.init(1024u);
  _private->def_image_handle = _private->image_pool.add_from_file("##default", Image::RepeatU | Image::RepeatV);

  sg_desc context = {};
  context.context.d3d11.device = sapp_d3d11_get_device();
  context.context.d3d11.device_context = sapp_d3d11_get_device_context();
  context.context.d3d11.depth_stencil_view_cb = sapp_d3d11_get_depth_stencil_view;
  context.context.d3d11.render_target_view_cb = sapp_d3d11_get_render_target_view;
  context.context.depth_format = SG_PIXELFORMAT_NONE;
  sg_setup(context);

  sg_shader_desc shader_desc = {};
  shader_desc.vs.source = shader_source;
  shader_desc.vs.entry = "vertex_main";
  shader_desc.vs.uniform_blocks[0].size = sizeof(ShaderConstants);

  shader_desc.fs.source = shader_source;
  shader_desc.fs.entry = "fragment_main";
  shader_desc.fs.images[0].image_type = SG_IMAGETYPE_2D;
  shader_desc.fs.images[0].name = "sample_image";
  shader_desc.fs.images[0].sampler_type = SG_SAMPLERTYPE_FLOAT;
  shader_desc.fs.images[1].image_type = SG_IMAGETYPE_2D;
  shader_desc.fs.images[1].name = "light_image";
  shader_desc.fs.images[1].sampler_type = SG_SAMPLERTYPE_FLOAT;
  shader_desc.fs.images[2].image_type = SG_IMAGETYPE_2D;
  shader_desc.fs.images[2].name = "reference_image";
  shader_desc.fs.images[2].sampler_type = SG_SAMPLERTYPE_FLOAT;
  shader_desc.fs.uniform_blocks[0].size = sizeof(ShaderConstants);
  _private->output_shader = sg_make_shader(shader_desc);

  sg_pipeline_desc pipeline_desc = {};
  pipeline_desc.shader = _private->output_shader;
  _private->output_pipeline = sg_make_pipeline(pipeline_desc);

  apply_reference_image(_private->def_image_handle);

  set_output_dimensions({16, 16});
  float4 c_image[256] = {};
  float4 l_image[256] = {};
  for (uint32_t y = 0; y < 16u; ++y) {
    for (uint32_t x = 0; x < 16u; ++x) {
      uint32_t i = x + y * 16u;
      c_image[i] = {1.0f, 0.5f, 0.25f, 1.0f};
      l_image[i] = {0.0f, 0.5f, 0.75f, 1.0f};
    }
  }
  update_camera_image(c_image);
  update_light_image(l_image);
  sg_commit();
}

void RenderContext::cleanup() {
  sg_destroy_pipeline(_private->output_pipeline);
  sg_destroy_shader(_private->output_shader);
  sg_destroy_image(_private->sample_image);
  sg_destroy_image(_private->light_image);
  sg_destroy_image(_private->reference_image);
  sg_shutdown();

  _private->image_pool.remove(_private->ref_image_handle);
  _private->image_pool.remove(_private->def_image_handle);
  _private->image_pool.cleanup();
}

void RenderContext::start_frame() {
  sg_pass_action pass_action = {};
  pass_action.colors[0].action = SG_ACTION_CLEAR;
  pass_action.colors[0].value = {0.05f, 0.07f, 0.1f, 1.0f};
  sg_apply_viewport(0, 0, sapp_width(), sapp_height(), sg_features().origin_top_left);
  sg_begin_default_pass(&pass_action, sapp_width(), sapp_height());

  _private->constants = {
    {sapp_widthf(), sapp_heightf(), float(_private->output_dimensions.x), float(_private->output_dimensions.y)},
    uint32_t(_private->view_options.view),
    _private->view_options.options,
    _private->view_options.exposure,
  };

  sg_range uniform_data = {
    .ptr = &_private->constants,
    .size = sizeof(ShaderConstants),
  };

  sg_bindings bindings = {};
  bindings.fs_images[0] = _private->sample_image;
  bindings.fs_images[1] = _private->light_image;
  bindings.fs_images[2] = _private->reference_image;

  sg_apply_pipeline(_private->output_pipeline);
  sg_apply_bindings(bindings);
  sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, uniform_data);
  sg_apply_uniforms(SG_SHADERSTAGE_FS, 0, uniform_data);
  sg_draw(0, 3, 1);
}

void RenderContext::end_frame() {
  sg_end_pass();
  sg_commit();
}

void RenderContext::apply_reference_image(uint32_t handle) {
  auto img = _private->image_pool.get(handle);

  sg_destroy_image(_private->reference_image);

  sg_image_desc ref_image_desc = {};
  ref_image_desc.type = SG_IMAGETYPE_2D;
  ref_image_desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
  ref_image_desc.width = img.isize.x;
  ref_image_desc.height = img.isize.y;
  ref_image_desc.mag_filter = SG_FILTER_NEAREST;
  ref_image_desc.min_filter = SG_FILTER_NEAREST;
  ref_image_desc.num_mipmaps = 1;
  ref_image_desc.usage = SG_USAGE_STREAM;
  _private->reference_image = sg_make_image(ref_image_desc);

  ref_image_desc.data.subimage[0][0].ptr = img.pixels.a;
  ref_image_desc.data.subimage[0][0].size = sizeof(float4) * img.pixels.count;
  sg_update_image(_private->reference_image, ref_image_desc.data);
}

void RenderContext::set_reference_image(const char* file_name) {
  _private->image_pool.remove(_private->ref_image_handle);
  _private->ref_image_handle = _private->image_pool.add_from_file(file_name, 0);
  apply_reference_image(_private->ref_image_handle);
}

void RenderContext::set_view_options(const ViewOptions& o) {
  _private->view_options = o;
}

void RenderContext::set_output_dimensions(const uint2& dim) {
  if ((_private->sample_image.id != 0) && (_private->light_image.id != 0) && (_private->output_dimensions == dim)) {
    return;
  }

  _private->output_dimensions = dim;
  sg_destroy_image(_private->sample_image);
  sg_destroy_image(_private->light_image);

  sg_image_desc desc = {};
  desc.type = SG_IMAGETYPE_2D;
  desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
  desc.width = _private->output_dimensions.x;
  desc.height = _private->output_dimensions.y;
  desc.mag_filter = SG_FILTER_NEAREST;
  desc.min_filter = SG_FILTER_NEAREST;
  desc.num_mipmaps = 1;
  desc.usage = SG_USAGE_STREAM;
  _private->sample_image = sg_make_image(desc);
  _private->light_image = sg_make_image(desc);

  _private->black_image.resize(dim.x * dim.y);
  std::fill(_private->black_image.begin(), _private->black_image.end(), float4{});
}

void RenderContext::update_camera_image(const float4* camera) {
  ETX_ASSERT(_private->sample_image.id != 0);

  sg_image_data data = {};
  data.subimage[0][0].size = sizeof(float4) * _private->output_dimensions.x * _private->output_dimensions.y;
  data.subimage[0][0].ptr = camera ? camera : _private->black_image.data();
  sg_update_image(_private->sample_image, data);
}

void RenderContext::update_light_image(const float4* light) {
  ETX_ASSERT(_private->light_image.id != 0);

  sg_image_data data = {};
  data.subimage[0][0].size = sizeof(float4) * _private->output_dimensions.x * _private->output_dimensions.y;
  data.subimage[0][0].ptr = light ? light : _private->black_image.data();
  sg_update_image(_private->light_image, data);
}

const char* shader_source = R"(

cbuffer Constants : register(b0) {
  float4 dimensions;
  uint image_view;
  uint options;
  float exposure;
  float pad;
}

Texture2D<float4> sample_image : register(t0);
Texture2D<float4> light_image : register(t1);
Texture2D<float4> reference_image : register(t2);

struct VSOutput {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

VSOutput vertex_main(uint vertexIndex : SV_VertexID) {
  float2 pos = float2((vertexIndex << 1u) & 2u, vertexIndex & 2u);
  float2 scale = dimensions.zw / dimensions.xy;
  float2 snapped_pos = floor(pos * 2.0f * dimensions.zw - dimensions.zw) / dimensions.xy;

  VSOutput output = (VSOutput)0;
  output.pos = float4(snapped_pos, 0.0f, 1.0f);
  output.uv = pos;
  return output;
}

static const uint kViewResult = 0;
static const uint kViewCameraImage = 1;
static const uint kViewLightImage = 2;
static const uint kViewReferenceImage = 3;
static const uint kViewRelativeDifference = 4;
static const uint kViewAbsoluteDifference = 5;

static const uint ToneMapping = 1u << 0u;
static const uint sRGB = 1u << 1u;

static const float3 lum = float3(0.2627, 0.6780, 0.0593);

float4 to_rgb(in float4 xyz) {
  float4 rgb;
  rgb[0] = max(0.0, 3.240479f * xyz[0] - 1.537150f * xyz[1] - 0.498535f * xyz[2]);
  rgb[1] = max(0.0, -0.969256f * xyz[0] + 1.875991f * xyz[1] + 0.041556f * xyz[2]);
  rgb[2] = max(0.0, 0.055648f * xyz[0] - 0.204043f * xyz[1] + 1.057311f * xyz[2]);
  rgb[3] = 1.0f;
  return rgb;
}

float4 validate(in float4 xyz) {
  if (any(isnan(xyz))) {
    return float4(123456.0, 0.0, 123456.0, 1.0);
  }
  if (any(isinf(xyz))) {
    return float4(0.0, 123456.0, 123456.0, 1.0);
  }
  if (any(xyz < 0.0)) {
    return float4(0.0, 0.0, 123456.0, 1.0);
  }
  return xyz;
}

float4 tonemap(float4 value) {
  if (options & ToneMapping) {
    value = 1.0f - exp(-exposure * value);
  }

  if (options & sRGB) {
    value = pow(max(0.0f, value), 1.0f / 2.2f);
  }

  return value;
}

float4 fragment_main(in VSOutput input) : SV_Target0 {
  float2 offset = 0.5f * (dimensions.xy - dimensions.zw);

  int2 coord = int2(floor(input.pos.xy - offset));
  int2 clamped = clamp(coord.xy, int2(0, 0), int2(dimensions.zw) - 1);
  clip(any(clamped != coord.xy) ? -1 : 1);

  if (any(clamped != coord.xy)) {
    return float4(1.0f, 0.0f, 1.0f, 1.0f);
  }

  int3 load_coord = int3(clamped, 0);

  float4 c_image = sample_image.Load(load_coord);
  float c_lum = dot(c_image.xyz, lum);

  float4 l_image = light_image.Load(load_coord);
  float l_lum = dot(l_image.xyz, lum);

  float4 r_image = reference_image.Load(load_coord);
  float r_lum = dot(r_image.xyz, lum);

  float4 t_image = c_image + l_image;
  float4 v_image = validate(t_image);
  if (any(v_image != t_image)) {
    return v_image;
  }

  c_image = to_rgb(c_image);
  l_image = to_rgb(l_image);
  v_image = to_rgb(v_image);
  float v_lum = dot(v_image.xyz, lum);

  float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
  switch (image_view) {
    case kViewResult: {
      result = tonemap(v_image);
      break;
    }
    case kViewCameraImage: {
      result = tonemap(c_image);
      break;
    }
    case kViewLightImage: {
      result = tonemap(l_image);
      break;
    }
    case kViewReferenceImage: {
      result = tonemap(r_image);
      break;
    }
    case kViewRelativeDifference: {
      result.x = exposure * max(0.0f, r_lum - v_lum);
      result.y = exposure * max(0.0f, v_lum - r_lum);
      break;
    }
    case kViewAbsoluteDifference: {
      result.x = float(r_lum > v_lum);
      result.y = float(v_lum > r_lum);
      break;
    }
    default:
      break;
  };

  return result;
}

)";

}  // namespace etx
