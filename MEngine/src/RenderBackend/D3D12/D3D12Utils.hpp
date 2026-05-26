#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MEngine::RenderBackend::D3D12 {

using Microsoft::WRL::ComPtr;

void checkHResult(HRESULT result, const char* operation);
std::string narrowPathForLog(const std::filesystem::path& path);
std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path);
[[nodiscard]] D3D12_RESOURCE_BARRIER transitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES beforeState,
    D3D12_RESOURCE_STATES afterState);
[[nodiscard]] ComPtr<ID3D12Resource> createBuffer(
    ID3D12Device* device,
    UINT64 byteSize,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    const char* operation);

} // namespace MEngine::RenderBackend::D3D12
