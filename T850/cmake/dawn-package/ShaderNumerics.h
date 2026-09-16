#pragma once
#include <webgpu/webgpu_cpp.h>
struct IDXGIAdapter;

void CheckBlurNumerics(const wgpu::Instance& instance, const wgpu::Device& device,
                       IDXGIAdapter* adapter, const wgpu::ShaderModule& module);
void CheckShaderFunctionNumerics(const wgpu::Instance& instance, const wgpu::Device& device,
                                 IDXGIAdapter* adapter);