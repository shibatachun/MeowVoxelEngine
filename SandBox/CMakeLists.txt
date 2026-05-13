add_executable(SandBox
    src/PrimitiveWorld.cpp
    src/main.cpp
)

target_link_libraries(SandBox
    PRIVATE
        MEngine
)

target_compile_features(SandBox PRIVATE cxx_std_17)
