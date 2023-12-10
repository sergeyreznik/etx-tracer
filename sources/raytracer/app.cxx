﻿#include <etx/core/environment.hxx>
#include <etx/core/profiler.hxx>
#include <etx/render/host/image_pool.hxx>
#include <etx/render/shared/scene.hxx>

#include "app.hxx"

#include <tinyexr.hxx>
#include <stb_image.hxx>
#include <stb_image_write.hxx>

#if defined(ETX_PLATFORM_WINDOWS)

// TODO : fix hacks
# define WIN32_LEAN_AND_MEAN 1
# include <Windows.h>

#endif

namespace etx {

RTApplication::RTApplication()
  : render(raytracing.scheduler())
  , scene(raytracing.scheduler())
  , camera_controller(scene.camera()) {
}

void RTApplication::init() {
  render.init();
  ui.initialize(spectrum::shared());
  ui.set_integrator_list(_integrator_array, std::size(_integrator_array));
  ui.callbacks.reference_image_selected = std::bind(&RTApplication::on_referenece_image_selected, this, std::placeholders::_1);
  ui.callbacks.save_image_selected = std::bind(&RTApplication::on_save_image_selected, this, std::placeholders::_1, std::placeholders::_2);
  ui.callbacks.scene_file_selected = std::bind(&RTApplication::on_scene_file_selected, this, std::placeholders::_1);
  ui.callbacks.save_scene_file_selected = std::bind(&RTApplication::on_save_scene_file_selected, this, std::placeholders::_1);
  ui.callbacks.integrator_selected = std::bind(&RTApplication::on_integrator_selected, this, std::placeholders::_1);
  ui.callbacks.preview_selected = std::bind(&RTApplication::on_preview_selected, this);
  ui.callbacks.run_selected = std::bind(&RTApplication::on_run_selected, this);
  ui.callbacks.stop_selected = std::bind(&RTApplication::on_stop_selected, this, std::placeholders::_1);
  ui.callbacks.reload_scene_selected = std::bind(&RTApplication::on_reload_scene_selected, this);
  ui.callbacks.reload_geometry_selected = std::bind(&RTApplication::on_reload_geometry_selected, this);
  ui.callbacks.options_changed = std::bind(&RTApplication::on_options_changed, this);
  ui.callbacks.use_image_as_reference = std::bind(&RTApplication::on_use_image_as_reference, this);
  ui.callbacks.material_changed = std::bind(&RTApplication::on_material_changed, this, std::placeholders::_1);
  ui.callbacks.medium_changed = std::bind(&RTApplication::on_medium_changed, this, std::placeholders::_1);
  ui.callbacks.emitter_changed = std::bind(&RTApplication::on_emitter_changed, this, std::placeholders::_1);
  ui.callbacks.camera_changed = std::bind(&RTApplication::on_camera_changed, this);
  ui.callbacks.scene_settings_changed = std::bind(&RTApplication::on_scene_settings_changed, this);

  _options.load_from_file(env().file_in_data("options.json"));
  if (_options.has("integrator") == false) {
    _options.add("integrator", "none");
  }
  if (_options.has("scene") == false) {
    _options.add("scene", "none");
  }
  if (_options.has("ref") == false) {
    _options.add("ref", "none");
  }

#if defined(ETX_PLATFORM_WINDOWS)
  if (GetAsyncKeyState(VK_ESCAPE)) {
    _options.set("integrator", std::string());
  }
  if (GetAsyncKeyState(VK_ESCAPE) && GetAsyncKeyState(VK_SHIFT)) {
    _options.set("scene", std::string());
  }
#endif

  auto integrator = _options.get("integrator", std::string{}).name;
  for (uint64_t i = 0; (integrator.empty() == false) && (i < std::size(_integrator_array)); ++i) {
    ETX_ASSERT(_integrator_array[i] != nullptr);
    if (integrator == _integrator_array[i]->name()) {
      _current_integrator = _integrator_array[i];
    }
  }

  ui.set_current_integrator(_current_integrator);

  _current_scene_file = _options.get("scene", std::string{}).name;
  if (_current_scene_file.empty() == false) {
    on_scene_file_selected(_current_scene_file);
  }

  auto ref = _options.get("ref", std::string{}).name;
  if (ref.empty() == false) {
    on_referenece_image_selected(ref);
  }

  save_options();
  ETX_PROFILER_RESET_COUNTERS();
}

void RTApplication::save_options() {
  _options.save_to_file(env().file_in_data("options.json"));
}

void RTApplication::frame() {
  ETX_FUNCTION_SCOPE();
  const float4* c_image = nullptr;
  const float4* l_image = nullptr;
  const char* status = "Not running";

  bool can_change_camera = true;
  bool c_image_updated = false;
  bool l_image_updated = false;

  if (_current_integrator != nullptr) {
    _current_integrator->update();
    status = _current_integrator->status();

    if (_reset_images == false) {
      c_image_updated = _current_integrator->have_updated_camera_image();
      if (c_image_updated) {
        c_image = _current_integrator->get_camera_image(false);
      }

      l_image_updated = _current_integrator->have_updated_light_image();
      if (l_image_updated) {
        l_image = _current_integrator->get_light_image(false);
      }
    }

    can_change_camera = _current_integrator->state() == Integrator::State::Preview;
  }

  auto dt = time_measure.lap();
  if (can_change_camera && camera_controller.update(dt) && (_current_integrator != nullptr)) {
    _current_integrator->preview(ui.integrator_options());
  }

  uint32_t sample_count = _current_integrator ? _current_integrator->sample_count() : 1u;

  render.set_view_options(ui.view_options());
  render.start_frame(sample_count);

  if (_reset_images || c_image_updated) {
    render.update_camera_image(c_image);
  }
  if (_reset_images || l_image_updated) {
    render.update_light_image(l_image);
  }
  _reset_images = false;

  ui.build(dt, status);
  render.end_frame();
}

void RTApplication::cleanup() {
  render.cleanup();
  ui.cleanup();
}

void RTApplication::process_event(const sapp_event* e) {
  if (ui.handle_event(e) || (raytracing.has_scene() == false)) {
    return;
  }
  camera_controller.handle_event(e);
}

void RTApplication::load_scene_file(const std::string& file_name, uint32_t options, bool start_rendering) {
  _current_scene_file = file_name;

  log::warning("Loading scene %s...", _current_scene_file.c_str());
  if (_current_integrator) {
    _current_integrator->stop(Integrator::Stop::Immediate);
  }

  _options.set("scene", _current_scene_file);
  save_options();

  if (scene.load_from_file(_current_scene_file.c_str(), options) == false) {
    ui.set_scene(nullptr, {}, {});
    log::error("Failed to load scene from file: %s", _current_scene_file.c_str());
    return;
  }

  raytracing.set_scene(scene.scene());
  ui.set_scene(scene.mutable_scene_pointer(), scene.material_mapping(), scene.medium_mapping());

  if (scene) {
    render.set_output_dimensions(scene.scene().camera.image_size);

    if (_current_integrator != nullptr) {
      if (start_rendering) {
        _current_integrator->run(ui.integrator_options());
      } else {
        _current_integrator->set_output_size(scene.scene().camera.image_size);
        _current_integrator->preview(ui.integrator_options());
      }
    }
  }
}

void RTApplication::save_scene_file(const std::string& file_name) {
  log::info("Saving %s..", file_name.c_str());
  scene.save_to_file(file_name.c_str());
}

void RTApplication::on_referenece_image_selected(std::string file_name) {
  log::warning("Loading reference image %s...", file_name.c_str());

  _options.set("ref", file_name);
  save_options();

  render.set_reference_image(file_name.c_str());
}

void RTApplication::on_use_image_as_reference() {
  ETX_ASSERT(_current_integrator);
  _options.set("ref", std::string());
  save_options();

  auto image = get_current_image(true);
  uint2 image_size = {raytracing.scene().camera.image_size.x, raytracing.scene().camera.image_size.y};
  render.set_reference_image(image.data(), image_size);
}

std::vector<float4> RTApplication::get_current_image(bool convert_to_rgb) {
  auto c_image = _current_integrator->get_camera_image(true);
  auto l_image = _current_integrator->get_light_image(true);
  uint2 image_size = {raytracing.scene().camera.image_size.x, raytracing.scene().camera.image_size.y};

  std::vector<float4> output(image_size.x * image_size.y, float4{});
  for (uint32_t i = 0, e = image_size.x * image_size.y; (c_image != nullptr) && (i < e); ++i) {
    output[i] = c_image[i];
  }

  for (uint32_t i = 0, e = image_size.x * image_size.y; (l_image != nullptr) && (i < e); ++i) {
    output[i] += l_image[i];
  }

  for (uint32_t i = 0, e = image_size.x * image_size.y; convert_to_rgb && (i < e); ++i) {
    auto rgb = spectrum::xyz_to_rgb(to_float3(output[i]));
    output[i] = {rgb.x, rgb.y, rgb.z, 1.0f};
  }

  return output;
}

void RTApplication::on_save_image_selected(std::string file_name, SaveImageMode mode) {
  if (_current_integrator == nullptr) {
    return;
  }

  uint2 image_size = {raytracing.scene().camera.image_size.x, raytracing.scene().camera.image_size.y};
  std::vector<float4> output = get_current_image(mode != SaveImageMode::XYZ);

  if (mode == SaveImageMode::TonemappedLDR) {
    if (strlen(get_file_ext(file_name.c_str())) == 0) {
      file_name += ".png";
    }
    float exposure = ui.view_options().exposure;
    std::vector<ubyte4> tonemapped(image_size.x * image_size.y);
    for (uint32_t i = 0, e = image_size.x * image_size.y; (mode != SaveImageMode::XYZ) && (i < e); ++i) {
      float3 tm = {
        1.0f - expf(-exposure * output[i].x),
        1.0f - expf(-exposure * output[i].y),
        1.0f - expf(-exposure * output[i].z),
      };
      float3 gamma = linear_to_gamma(tm);
      tonemapped[i].x = static_cast<uint8_t>(255.0f * saturate(gamma.x));
      tonemapped[i].y = static_cast<uint8_t>(255.0f * saturate(gamma.y));
      tonemapped[i].z = static_cast<uint8_t>(255.0f * saturate(gamma.z));
      tonemapped[i].w = 255u;
    }
    if (stbi_write_png(file_name.c_str(), image_size.x, image_size.y, 4, tonemapped.data(), 0) != 1) {
      log::error("Failed to save PNG image to %s", file_name.c_str());
    }
  } else {
    if (strlen(get_file_ext(file_name.c_str())) == 0) {
      file_name += ".exr";
    }
    const char* error = nullptr;
    if (SaveEXR(reinterpret_cast<const float*>(output.data()), image_size.x, image_size.y, 4, false, file_name.c_str(), &error) != TINYEXR_SUCCESS) {
      log::error("Failed to save EXR image to %s: %s", file_name.c_str(), error);
    }
  }
}

void RTApplication::on_scene_file_selected(std::string file_name) {
  load_scene_file(file_name, SceneRepresentation::LoadEverything, false);
}

void RTApplication::on_save_scene_file_selected(std::string file_name) {
  if (strlen(get_file_ext(file_name.c_str())) == 0) {
    file_name += ".json";
  }
  save_scene_file(file_name);
}

void RTApplication::on_integrator_selected(Integrator* i) {
  if (_current_integrator == i) {
    return;
  }

  _options.set("integrator", i->name());
  save_options();

  if (_current_integrator != nullptr) {
    _current_integrator->stop(Integrator::Stop::Immediate);
  }

  _current_integrator = i;
  ui.set_current_integrator(_current_integrator);

  if (scene) {
    _current_integrator->set_output_size(scene.scene().camera.image_size);
    _current_integrator->preview(ui.integrator_options());
  }

  _reset_images = true;
}

void RTApplication::on_preview_selected() {
  ETX_ASSERT(_current_integrator != nullptr);
  _current_integrator->preview(ui.integrator_options());
}

void RTApplication::on_run_selected() {
  ETX_ASSERT(_current_integrator != nullptr);
  _current_integrator->run(ui.integrator_options());
}

void RTApplication::on_stop_selected(bool wait_for_completion) {
  ETX_ASSERT(_current_integrator != nullptr);
  _current_integrator->stop(wait_for_completion ? Integrator::Stop::WaitForCompletion : Integrator::Stop::Immediate);
}

void RTApplication::on_reload_scene_selected() {
  if (_current_scene_file.empty() == false) {
    bool start_render = (_current_integrator != nullptr) && (_current_integrator->state() == Integrator::State::Running);
    load_scene_file(_current_scene_file, SceneRepresentation::LoadEverything, start_render);
  }
}

void RTApplication::on_reload_geometry_selected() {
  if (_current_scene_file.empty() == false) {
    bool start_render = (_current_integrator != nullptr) && (_current_integrator->state() == Integrator::State::Running);
    load_scene_file(_current_scene_file, SceneRepresentation::LoadGeometry, start_render);
  }
}

void RTApplication::on_options_changed() {
  ETX_ASSERT(_current_integrator);
  _current_integrator->update_options(ui.integrator_options());
}

void RTApplication::on_material_changed(uint32_t index) {
  // TODO : re-upload to GPU
  _current_integrator->preview(ui.integrator_options());
}

void RTApplication::on_medium_changed(uint32_t index) {
  // TODO : re-upload to GPU
  _current_integrator->preview(ui.integrator_options());
}

void RTApplication::on_emitter_changed(uint32_t index) {
  // TODO : re-upload to GPU
  _current_integrator->stop(Integrator::Stop::Immediate);
  build_emitters_distribution(scene.mutable_scene());
  _current_integrator->preview(ui.integrator_options());
}

void RTApplication::on_camera_changed() {
  _current_integrator->preview(ui.integrator_options());
}

void RTApplication::on_scene_settings_changed() {
  _current_integrator->preview(ui.integrator_options());
}

}  // namespace etx
