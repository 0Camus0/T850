#ifndef NOMINMAX
#define NOMINMAX
#endif

/*********************************************************
* Copyright (C) 2017 Daniel Enriquez (camus_mm@hotmail.com)
* All Rights Reserved
*
* You may use, distribute and modify this code under the
* following terms:
* ** Do not claim that you wrote this software
* ** A mention would be appreciated but not needed
* ** I do not and will not provide support, this software is "as is"
* ** Enjoy, learn and share.
*********************************************************/

#include <Config.h>
#ifdef OS_LINUX
    #include <core/LinuxFramework.h>
#elif defined(OS_WINDOWS)
	#include <core/windows/Win32Framework.h>
#endif

#include <Application.h>

#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <thread>

#include <Descriptors.h>
#include <core/Config.h>
#include <utils/Log.h>
#include <utils/ConfigRuntime.h>
#include <utils/ShaderPermutationDump.h>
#include <core/ShaderTools.h>
#ifdef __EMSCRIPTEN__
#include <core/WebFramework.h>
#endif
#include <debug/Profiler.h>
#include <utils/gltf/GLTFLoader.h>
#include <utils/gltf/GLTFAccessor.h>
#include <game/GameSelfTest.h>
#include <debug/ComputeSelfTest.h>
#include <simulation/FluidSimulation.h>
#include <BackyardSelfTest.h>
// W-22: App.cpp parses --backyard-spawn and hands the count to the scene.
#include <BackyardScene.h>
#include <debug/CrashDiagnostics.h>
#include <debug/GraphicsFixture.h>
#if (defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__)
#include <video/webgpu/WebGPUContext.h>
#endif

std::vector<std::string> g_args;

t850::AppBase		  *pApp = 0;
t850::RootFramework *pFrameWork = 0;

namespace {
  class ComputeSelfTestApp final : public t850::AppBase {
  public:
    void InitVars() override {}
    void CreateAssets() override {}
    void LoadAssets() override {}
    void DestroyAssets() override {}
    void OnUpdate() override {}
    void OnDraw() override {}
    void OnInput() override {}
    void OnPause() override {}
    void OnResume() override {}
    void OnReset() override {}
    void LoadScene(int) override {}
  };

  class WebGPURecoverySelfTestApp final : public t850::AppBase {
  public:
    void InitVars() override {}
    void CreateAssets() override {}
    void LoadAssets() override {}
    void DestroyAssets() override {}
    void OnUpdate() override {
      pFramework->pVideoDriver->ClearBackbufferWithColor(0.05f, 0.1f, 0.15f, 1.0f);
      pFramework->pVideoDriver->CompleteFrame(t850::BaseDriver::FrameCompletionMode::Present);
    }
    void OnDraw() override {}
    void OnInput() override {}
    void OnPause() override {}
    void OnResume() override {}
    void OnReset() override {}
    void LoadScene(int) override {}
  };
}

int main(int arg,char ** args) try {
  t850::InstallUnattendedCrtReportHook();
  t850::Config defaultConfig;
  t850::g_config = defaultConfig;

    for(int i=0;i<arg;i++){
        g_args.push_back( std::string( args[i] ) );
    }

  bool computeSelfTest = false;
  bool fluidSelfTest = false;
  bool backyardSelfTest = false;
  bool backyardNoBall = false;
  float backyardCellSize = 0.0f;
  int backyardSpawnOnStart = 0;
  int backyardSelfTestFrames = 900;
  float fluidFixedDt = 1.0f / 300.0f;
  int fluidSelfTestFrames = 24;
  std::string fluidDumpDirectory;
  int fluidIterations = 8;
  bool webgpuRecoverySelfTest = false;
  bool computeSelfTestApiExplicit = false;
  int computeSelfTestWaitSeconds = 0;
  bool webgpuRecoveryStressSelfTest = false;
  for (int i = 1; i < arg; ++i) {
      if (std::string_view(args[i]) == "--graphics-fixture") {
  #if defined(_WIN32) && defined(_M_X64)
    return t850::RunGraphicsFixture(arg, args);
  #else
    std::cerr << "The graphics fixture currently requires Windows x64.\n";
    return 1;
  #endif
      }
    if (std::string_view(args[i]) == "--game-selftest") {
      const int failures = t850::game::RunGameSelfTests();
      return failures == 0 ? 0 : 1;
    }
    if (std::string_view(args[i]) == "--compute-selftest") {
      computeSelfTest = true;
    }
    if (std::string_view(args[i]) == "--fluid-selftest") {
      fluidSelfTest = true;
    }
    if (std::string_view(args[i]) == "--fluid-selftest-frames" && i + 1 < arg) {
      fluidSelfTest = true;
      fluidSelfTestFrames = (std::max)(1, (std::min)(2000, std::stoi(args[++i])));
    }
    if (std::string_view(args[i]) == "--fluid-dump-volume" && i + 1 < arg) {
      fluidSelfTest = true;
      fluidDumpDirectory = args[++i];
    }
    if (std::string_view(args[i]) == "--fluid-iterations" && i + 1 < arg) {
      fluidSelfTest = true;
      fluidIterations = (std::max)(1, (std::min)(400, std::stoi(args[++i])));
    }
    if (std::string_view(args[i]) == "--backyard-selftest") {
      backyardSelfTest = true;
    }
    if (std::string_view(args[i]) == "--backyard-selftest-frames" && i + 1 < arg) {
      backyardSelfTest = true;
      backyardSelfTestFrames = (std::max)(1, (std::min)(20000, std::stoi(args[++i])));
    }
    // Deterministic step for the water: without it two runs land on different
    // physical states and cannot be compared.
    if (std::string_view(args[i]) == "--fluid-fixed-dt" && i + 1 < arg) {
      const float value = std::stof(args[++i]);
      if (value > 0.0f && value <= 0.1f) fluidFixedDt = value;
      else T8_LOG_ERROR("[App] --fluid-fixed-dt %g is outside 0 < dt <= 0.1", value);
    }
    if (std::string_view(args[i]) == "--backyard-no-ball") {
      backyardNoBall = true;
    }
    if (std::string_view(args[i]) == "--backyard-cell-size" && i + 1 < arg) {
      backyardSelfTest = true;
      const float value = std::stof(args[++i]);
      if (value >= 0.02f && value <= 0.5f) backyardCellSize = value;
      else T8_LOG_ERROR("[App] --backyard-cell-size %g is outside 0.02..0.5 m", value);
    }
    if (std::string_view(args[i]) == "--backyard-spawn" && i + 1 < arg) {
      backyardSpawnOnStart = (std::max)(std::stoi(args[++i]), 0);
      BackyardScene::SetSpawnOnStart(backyardSpawnOnStart);
    }
    if (std::string_view(args[i]) == "--webgpu-recovery-selftest") {
      webgpuRecoverySelfTest = true;
    }
    if (std::string_view(args[i]) == "--webgpu-recovery-stress-selftest") {
      webgpuRecoverySelfTest = true;
      webgpuRecoveryStressSelfTest = true;
    }
    if (std::string_view(args[i]) == "--api" && i + 1 < arg) {
      computeSelfTestApiExplicit = true;
    }
    if (std::string_view(args[i]) == "--compute-selftest-wait" && i + 1 < arg) {
      computeSelfTest = true;
      try {
        computeSelfTestWaitSeconds = (std::max)(0, (std::min)(60, std::stoi(args[++i])));
      } catch (...) {
        std::cerr << "[ComputeSelfTest] Invalid --compute-selftest-wait value\n";
        return 1;
      }
    }
  }

  if (t850::config::HasHelpArgument(arg, args)) {
    t850::config::PrintHelp();
    return 0;
  }

  if (auto configPath = t850::config::FindConfigArgument(arg, args)) {
    t850::config::LoadRuntimeConfig(*configPath, t850::g_config);
  }

  t850::config::ApplyCommandLine(arg, args, t850::g_config);
  if (computeSelfTest && webgpuRecoverySelfTest) {
    throw std::invalid_argument("Choose compute or WebGPU recovery self-test, not both");
  }
  if (webgpuRecoverySelfTest) {
#if (defined(_WIN32) && (defined(_M_X64) || defined(_M_ARM64))) || defined(__EMSCRIPTEN__)
    t850::g_config.api = "webgpu";
    if (webgpuRecoveryStressSelfTest) {
      t850::g_config.webgpuDeviceRecoveryAttempts = 3;
      t850::webgpu::ConfigureDeviceLossTestSequence(60, 4, 1);
    } else {
      t850::webgpu::ConfigureDeviceLossTestFrame(60);
    }
#else
    throw std::invalid_argument("The WebGPU recovery self-test is unavailable on this platform");
#endif
  }
#ifdef OS_WINDOWS
  if (computeSelfTest && !computeSelfTestApiExplicit) {
    // Preserve the original deterministic default while allowing an explicit
    // backend for cross-API compute validation.
    t850::g_config.api = "d3d12";
  }
#elif defined(OS_LINUX)
  if (computeSelfTest && !computeSelfTestApiExplicit) {
    t850::g_config.api = "vulkan";
  }
#endif
  t850::config::ValidateConfig(t850::g_config);
  if (t850::g_config.flags.benchmarkMatrix) {
    t850::g_config.api = t850::g_config.benchmarkPaired ? "d3d12" : "d3d11";
    if (!t850::g_config.benchmarkPaired) {
      t850::g_config.width = 1920;
      t850::g_config.height = 1080;
    }
    t850::g_config.flags.offscreen = false;
    t850::g_config.startScene = 1;
  }

  t850::ApplicationDesc desc;
  t850::config::ConfigureApplicationDesc(t850::g_config, desc);

  // Parse command-line arguments that intentionally exit before renderer startup.
  for (int i = 1; i < arg; i++) {
    std::string a = args[i];
    if (a == "--config" && i + 1 < arg) {
      ++i;
    }
    else if (a == "--validateGltf" && i + 1 < arg) {
      // Offline parser conformance harness (plan §8). Loads a .gltf or
      // .glb file and prints a structural summary plus the first vertex
      // of each primitive. Exits without booting the renderer, so it
      // works on systems with no graphics device available (e.g. CI).
      std::string path = args[++i];
      // Strip surrounding quotes if present (from launcher).
      if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        path = t850::config::StripQuotes(path);
      }
      // Bring up logging early enough to surface loader messages.
      t850::Log::Init(t850::Log::LVL_INFO,
                      t850::Log::T8_LOG_BACKEND_CONSOLE, nullptr);

      t850::gltf::Document doc;
      if (!t850::gltf::LoadGLTF(path, doc)) {
        std::cerr << "[validateGltf] FAILED to load '" << path << "'\n";
        t850::Log::Shutdown();
        return 1;
      }
      std::cout << "[validateGltf] " << path << "\n"
                << "  asset.version       : " << doc.asset.version << "\n"
                << "  scenes / nodes      : " << doc.scenes.size()
                << " / " << doc.nodes.size() << "\n"
                << "  meshes / accessors  : " << doc.meshes.size()
                << " / " << doc.accessors.size() << "\n"
                << "  materials / textures: " << doc.materials.size()
                << " / " << doc.textures.size() << "\n"
                << "  images / samplers   : " << doc.images.size()
                << " / " << doc.samplers.size() << "\n"
                << "  skins / animations  : " << doc.skins.size()
                << " / " << doc.animations.size() << "\n"
                << "  buffers / bufViews  : " << doc.buffers.size()
                << " / " << doc.bufferViews.size() << "\n";

      std::size_t primCount = 0;
      for (std::size_t mi = 0; mi < doc.meshes.size(); ++mi) {
        const auto& m = doc.meshes[mi];
        for (std::size_t pi = 0; pi < m.primitives.size(); ++pi, ++primCount) {
          const auto& p = m.primitives[pi];
          std::cout << "  mesh[" << mi << "].prim[" << pi << "]"
                    << " mode=" << p.mode
                    << " mat=" << (p.material ? *p.material : -1);
          if (p.attributes.POSITION >= 0) {
            std::vector<float> pos; int e = 0;
            if (t850::gltf::ReadAccessorFloats(doc, p.attributes.POSITION, pos, &e)
                && e == 3 && pos.size() >= 3) {
              std::cout << " v0=(" << pos[0] << "," << pos[1] << "," << pos[2] << ")";
            }
          }
          std::cout << "\n";
        }
      }
      std::cout << "[validateGltf] OK: " << primCount << " primitives total\n";
      t850::Log::Shutdown();
      return 0;
    }
  }

  // Create log directory if a file path was requested
  if (!t850::g_config.logFile.empty()) {
    auto parent = std::filesystem::path(t850::g_config.logFile).parent_path();
    if (!parent.empty())
      std::filesystem::create_directories(parent);
  }

  // Determine API display name for session tag
  const char* apiTag = t850::config::ApiTag(desc.api);

  // Initialize logging
  uint32_t logBackends = t850::Log::T8_LOG_BACKEND_CONSOLE;
#ifdef OS_WINDOWS
  logBackends |= t850::Log::T8_LOG_BACKEND_DEBUG_OUTPUT;  // also emit to VS Output window
#endif
  if (!t850::g_config.logFile.empty())
    logBackends |= t850::Log::T8_LOG_BACKEND_FILE;

  t850::Log::Init(
    static_cast<t850::Log::Level>(t850::g_config.logLevel),
    logBackends,
    t850::g_config.logFile.empty() ? nullptr : t850::g_config.logFile.c_str()
  );
  t850::Log::SetSessionTag(apiTag);
  if (auto result = t850::RunShaderPrecompileCommand(t850::g_config)) {
    t850::Log::Shutdown();
    return *result;
  }
  t850::BeginShaderPermutationRecording(t850::g_config);

	pApp = computeSelfTest
  ? static_cast<t850::AppBase*>(new ComputeSelfTestApp())
  : webgpuRecoverySelfTest
  ? static_cast<t850::AppBase*>(new WebGPURecoverySelfTestApp())
  : static_cast<t850::AppBase*>(new App());
  int result = 0;
#ifdef OS_WEB
  pFrameWork = new t850::WebFramework(pApp);
#elif defined(OS_LINUX)
  pFrameWork = new t850::LinuxFramework((t850::AppBase*)pApp);
#elif defined(OS_WINDOWS)
  pFrameWork = new t850::Win32Framework((t850::AppBase*)pApp);
#endif
  pFrameWork->InitGlobalVars();
  pFrameWork->OnCreateApplication(desc);
  if (computeSelfTest) {
    T8_LOG_INFO("[ComputeSelfTest] Starting standalone arithmetic validation on API=%s",
          pFrameWork->pVideoDriver->ApiTag());    if (computeSelfTestWaitSeconds > 0) {
      T8_LOG_INFO("[ComputeSelfTest] Waiting %d seconds before dispatch for capture tools",
                  computeSelfTestWaitSeconds);
      std::this_thread::sleep_for(std::chrono::seconds(computeSelfTestWaitSeconds));
    }
    result = t850::RunComputeSelfTests(pFrameWork->pVideoDriver);
    if (computeSelfTestWaitSeconds > 0) {
      T8_LOG_INFO("[ComputeSelfTest] Waiting %d seconds after dispatch for capture tools",
                  computeSelfTestWaitSeconds);
      std::this_thread::sleep_for(std::chrono::seconds(computeSelfTestWaitSeconds));
    }
  } else if (backyardSelfTest) {
    t850::BackyardSelfTestOptions options;
    options.frames = static_cast<uint32_t>(backyardSelfTestFrames);
    options.fixedDt = fluidFixedDt;
    options.skipBall = backyardNoBall;
    options.cellSize = backyardCellSize;
    result = t850::RunBackyardSelfTest(pFrameWork->pVideoDriver, options);
  } else if (fluidSelfTest) {
    t850::BaseDriver* driver = pFrameWork->pVideoDriver;
    t850::FluidSimulation fluid;
    t850::FluidSettings settings;
    settings.width = 32;
    settings.height = 16;
    settings.depth = 16;
    settings.pressureIterations = static_cast<uint32_t>(fluidIterations);
    settings.deltaTime = 1.0f / 60.0f;
    settings.cellSize = 0.09f;
    if (!fluid.TryInitialize(driver, settings)) {
      T8_LOG_INFO("[FluidSelfTest] SKIP: API=%s has no volumetric compute storage", driver->ApiTag());
      result = 0;
    } else {
      int failures = 0;
      fluid.SeedVolumeFraction(driver);
      const uint32_t expectedSamples = settings.width * settings.height * settings.depth;
      {
        float volume = 0.0f;
        if (fluid.MeasureVolumeFraction(driver, volume)) {
          T8_LOG_INFO("[FluidSelfTest] freeSurface totalAlpha=%.0f cells of %u fluid voxels",
                      volume, fluid.FluidVoxelCount());
          if (volume <= 0.0f) {
            T8_LOG_ERROR("[FluidSelfTest] volume fraction seeded empty");
            ++failures;
          }
        }
      }
      const auto report = [&](const char* label, t850::FluidSimulation::Diagnostics* result) -> bool {
        t850::FluidSimulation::Diagnostics diagnostics;
        if (!fluid.ReadDiagnostics(driver, diagnostics)) {
          T8_LOG_ERROR("[FluidSelfTest] %-12s readback failed", label);
          ++failures;
          return false;
        }
        T8_LOG_INFO("[FluidSelfTest] %-12s samples=%u/%u meanX=%+.4f meanSpeed=%.4f maxSpeed=%.4f nonFinite=%u",
                    label, diagnostics.samples, expectedSamples, diagnostics.meanX,
                    diagnostics.meanSpeed, diagnostics.maxSpeed, diagnostics.nonFinite);
        if (result) *result = diagnostics;
        if (diagnostics.samples != expectedSamples || diagnostics.nonFinite != 0) {
          T8_LOG_ERROR("[FluidSelfTest] %-12s invalid: samples=%u expected=%u nonFinite=%u",
                       label, diagnostics.samples, expectedSamples, diagnostics.nonFinite);
          ++failures;
          return false;
        }
        return true;
      };

      // Stage 0: a seeded volume must read back as exactly 1.0 m/s on +X.
      float seededMeanX = 0.0f;
      if (!fluid.Seed(driver)) {
        T8_LOG_ERROR("[FluidSelfTest] Seed dispatch failed on API=%s", driver->ApiTag());
        ++failures;
      } else {
        t850::FluidSimulation::Diagnostics diagnostics;
        if (report("seed", &diagnostics)) seededMeanX = diagnostics.meanX;
        if (std::abs(seededMeanX - 1.0f) > 0.05f) {
          T8_LOG_ERROR("[FluidSelfTest] seed expected meanX=1.0, got %+.4f", seededMeanX);
          ++failures;
        }
      }

      // Stage 1: a field with known non-zero divergence must be projected, and
      // more Jacobi iterations must monotonically reduce the residual. Plain
      // Jacobi needs O(grid^2) iterations to converge, so the gate is the trend
      // plus "never grows", not an absolute ratio at one iteration count.
      if (failures == 0) {
        const int iterationSweep[] = {1, 8, 50};
        float ratios[3] = {0.0f, 0.0f, 0.0f};
        // SOR transiently amplifies the residual from a cold start; the monotonic
        // gate needs plain Gauss-Seidel. The live solver warm-starts its pressure.
        fluid.SetSOROmega(1.0f);
        for (int sweep = 0; sweep < 3 && failures == 0; ++sweep) {
          const int iterations = iterationSweep[sweep];
          if (!fluid.Seed(driver, true)) {
            T8_LOG_ERROR("[FluidSelfTest] divergent seed failed");
            ++failures;
            break;
          }
          t850::FluidSimulation::VolumeDump before;
          const std::string base = fluidDumpDirectory.empty() ? "." : fluidDumpDirectory;
          const std::string tag = "/iter" + std::to_string(iterations);
          if (!fluid.MeasureVolume(driver, before)) { ++failures; break; }
          if (!fluid.StepStage(driver, "divergence")) { ++failures; break; }
          for (int iteration = 0; iteration < iterations; ++iteration) {
            if (!fluid.StepStage(driver, "pressure")) {
              T8_LOG_ERROR("[FluidSelfTest] pressure iteration %d failed", iteration);
              ++failures;
            }
          }
          t850::FluidSimulation::VolumeDump after;
          if (failures != 0) break;
          if (!fluid.StepStage(driver, "gradient")) { ++failures; break; }
          if (!fluid.MeasureVolume(driver, after)) { ++failures; break; }
          if (!fluidDumpDirectory.empty())
            fluid.DumpVolume(driver, base + tag + "/after", after);
          ratios[sweep] = before.interiorMeanDivergence > 1e-6f
            ? after.interiorMeanDivergence / before.interiorMeanDivergence : 0.0f;
          // Free-surface transport must conserve volume. This runs the real Step
          // pipeline (which includes flux-form advection and the global
          // correction) rather than the staged pressure-only path. The correction
          // runs once per frame against the locked reference, so a little drift
          // between frames is expected and only the trend matters.
          float volumeBefore = 0.0f, volumeAfter = 0.0f;
          fluid.MeasureVolumeFraction(driver, volumeBefore);
          for (int frame = 0; frame < 60; ++frame) fluid.Step(driver);
          fluid.MeasureVolumeFraction(driver, volumeAfter);
          const float volumeDrift = volumeBefore > 0.0f
            ? 100.0f * (volumeAfter - volumeBefore) / volumeBefore : 0.0f;
          T8_LOG_INFO("[FluidSelfTest] massConservation frames=60 before=%.0f after=%.0f drift=%+.2f%%",
                      volumeBefore, volumeAfter, volumeDrift);
          if (std::abs(volumeDrift) > 2.0f) {
            T8_LOG_ERROR("[FluidSelfTest] volume drift %.2f%% exceeds 2%%", volumeDrift);
            ++failures;
          }
          T8_LOG_INFO("[FluidSelfTest] projection iterations=%3d pre=%.4f post=%.4f ratio=%.3f",
                      iterations, before.interiorMeanDivergence, after.interiorMeanDivergence, ratios[sweep]);
          // Wall acceptance: no velocity may exist inside a solid voxel, and no
          // fluid voxel may point into a wall face.
          T8_LOG_INFO("[FluidSelfTest] walls iterations=%3d solidSpeedSum=%.6f solidSamples=%u maxWallPenetration=%.6f",
                      iterations, after.solidSpeedSum, after.solidSamples, after.maxWallPenetration);
          if (after.solidSpeedSum > 1e-4f) {
            T8_LOG_ERROR("[FluidSelfTest] water leaked into %u solid voxels (sum |v| = %.6f)",
                         after.solidSamples, after.solidSpeedSum);
            ++failures;
          }
          if (after.maxWallPenetration > 1e-4f) {
            T8_LOG_ERROR("[FluidSelfTest] velocity points into a wall face (max %.6f m/s)",
                         after.maxWallPenetration);
            ++failures;
          }
        }
        if (failures == 0) {
          if (!(ratios[0] >= ratios[1] && ratios[1] >= ratios[2])) {
            T8_LOG_ERROR("[FluidSelfTest] divergence residual is not monotonically decreasing (%.3f, %.3f, %.3f)",
                         ratios[0], ratios[1], ratios[2]);
            ++failures;
          }
          // One red-black sweep from a zero guess leaves a checkerboard pressure whose
          // staggered gradient can briefly raise the residual; gate from 8 sweeps on.
          if (ratios[1] > 1.0f) {
            T8_LOG_ERROR("[FluidSelfTest] projection increased divergence (ratio=%.3f)", ratios[1]);
            ++failures;
          }
          if (ratios[2] > 0.85f) {
            T8_LOG_ERROR("[FluidSelfTest] 50 Gauss-Seidel sweeps removed almost nothing (ratio=%.3f)", ratios[2]);
            ++failures;
          }
        }
      }

      // A uniform field is divergence-free, so the previous pre/post check
      // measured only wall stencil noise. The divergent seed above is the real
      // projection test.

      // Full solver: the field must still be finite and moving.
      for (int frame = 0; failures == 0 && frame < fluidSelfTestFrames; ++frame) {
        if (!fluid.Step(driver)) {
          T8_LOG_ERROR("[FluidSelfTest] Step %d failed on API=%s", frame, driver->ApiTag());
          ++failures;
        }
      }
      t850::FluidSimulation::Diagnostics diagnostics;
      if (failures == 0 && report("full", &diagnostics)) {
        if (diagnostics.meanSpeed <= 0.0005f) {
          T8_LOG_ERROR("[FluidSelfTest] solver field is static (meanSpeed=%.6f)", diagnostics.meanSpeed);
          ++failures;
        }
      }
      if (!fluidDumpDirectory.empty()) {
        t850::FluidSimulation::VolumeDump dump;
        if (!fluid.DumpVolume(driver, fluidDumpDirectory + "/final", dump)) {
          T8_LOG_ERROR("[FluidSelfTest] final volume dump failed");
          ++failures;
        }
      }

      if (failures == 0) {
        T8_LOG_INFO("[FluidSelfTest] PASS: API=%s grid=%ux%ux%u frames=%d meanSpeed=%.4f",
                    driver->ApiTag(), settings.width, settings.height, settings.depth,
                    fluidSelfTestFrames, diagnostics.meanSpeed);
      } else {
        T8_LOG_ERROR("[FluidSelfTest] FAIL: API=%s failures=%d", driver->ApiTag(), failures);
      }
      fluid.Shutdown(driver);
      result = failures == 0 ? 0 : 1;
    }
  } else if (!t850::g_config.flags.dumpShaderPermutations) {
	  pFrameWork->UpdateApplication();
  }
  if (t850::g_config.flags.dumpShaderPermutations)
    t850::ShaderPermutationDump::Flush();
  pFrameWork->OnDestroyApplication();

  delete pFrameWork;
	delete pApp;

  if (!t850::ShaderPermutationDump::Flush()) {
    t850::Log::Shutdown();
    return 1;
  }
	t850::Log::Shutdown();

  return result;
} catch (const std::exception& error) {
  T8_LOG_ERROR("[App] Startup/runtime failure: %s", error.what());
  std::cerr << "Engine failure: " << error.what() << '\n';
  return 1;
}
