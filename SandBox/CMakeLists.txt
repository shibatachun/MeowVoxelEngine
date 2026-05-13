add_executable(SandBox
    src/PrimitiveWorld.cpp
    src/main.cpp
)

target_link_libraries(SandBox
    PRIVATE
        MEngine
        enkiTS
)

target_include_directories(SandBox
    PRIVATE
        ${enkits_SOURCE_DIR}/src
)

target_compile_features(SandBox PRIVATE cxx_std_17)

add_executable(MeowAssetBaker
    src/AssetBaker.cpp
)

if(TARGET assimp::assimp)
    set(MEOW_ASSIMP_TARGET assimp::assimp)
else()
    set(MEOW_ASSIMP_TARGET assimp)
endif()

target_link_libraries(MeowAssetBaker
    PRIVATE
        MEngine
        ${MEOW_ASSIMP_TARGET}
)

target_compile_features(MeowAssetBaker PRIVATE cxx_std_17)
