# MeowEngine

`MEngine` is the static engine library. `SandBox` is the executable project that links against that library and exercises the engine entry point.

## Layout

```text
MeowEngine/
  CMakeLists.txt
  ThirdParty/
    NVRHI/
    SDL/
  MEngine/
    include/MEngine/
      Windows/
      RenderBackend/
        Vulkan/
          VulkanBuffer.hpp
          VulkanCommandContext.hpp
          VulkanDevice.hpp
          VulkanImage.hpp
          VulkanRenderThread.hpp
          VulkanRHI.hpp
          VulkanSwapchain.hpp
          VulkanUtils.hpp
      Physics/
      Audio/
      InputSystem/
      AnimationSystem/
    src/
      RenderBackend/
        Vulkan/
  SandBox/
    src/main.cpp
```

## Build

```powershell
cmake -S MeowEngine -B MeowEngine/build
cmake --build MeowEngine/build
.\MeowEngine\build\bin\Debug\SandBox.exe
.\MeowEngine\build\bin\Debug\SandBox.exe --api vulkan
```

For single-config generators, the executable may be at:

```powershell
.\MeowEngine\build\bin\SandBox.exe
```
