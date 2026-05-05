project(DayScene LANGUAGES CXX)

add_executable(DayScene
	Application.cpp
	App.cpp
	DayScene.cpp
	SandboxScene.cpp)

t850_apply_app(DayScene)
t850_apply_profiler_defines(DayScene)
t850_stage_runtime(DayScene COPY_DRACO)

target_link_libraries(DayScene PRIVATE FrameworkImGui)