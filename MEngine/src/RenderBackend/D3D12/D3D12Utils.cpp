#include "D3D12Utils.hpp"

#include <fstream>
#include <stdexcept>

namespace MEngine::RenderBackend::D3D12 {

void checkHResult(HRESULT result, const char* operation)
{
    if (FAILED(result)) {
        throw std::runtime_error(std::string(operation) + " failed with HRESULT 0x" + std::to_string(static_cast<uint32_t>(result)));
    }
}

std::string narrowPathForLog(const std::filesystem::path& path)
{
    return path.u8string();
}

std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + narrowPathForLog(path));
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("File is empty: " + narrowPathForLog(path));
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("Failed to read file: " + narrowPathForLog(path));
    }
    return bytes;
}

D3D12_RESOURCE_BARRIER transitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES beforeState,
    D3D12_RESOURCE_STATES afterState)
{
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    return barrier;
}

ComPtr<ID3D12Resource> createBuffer(
    ID3D12Device* device,
    UINT64 byteSize,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    const char* operation)
{
    D3D12_HEAP_PROPERTIES heapProperties {};
    heapProperties.Type = heapType;

    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = byteSize;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> buffer;
    checkHResult(device->CreateCommittedResource(
                     &heapProperties,
                     D3D12_HEAP_FLAG_NONE,
                     &desc,
                     initialState,
                     nullptr,
                     IID_PPV_ARGS(&buffer)),
        operation);
    return buffer;
}

} // namespace MEngine::RenderBackend::D3D12
