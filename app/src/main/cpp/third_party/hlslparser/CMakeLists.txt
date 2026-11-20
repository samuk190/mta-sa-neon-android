add_library(neon_hlsl STATIC
    Engine.cpp CodeWriter.cpp HLSLTokenizer.cpp HLSLTree.cpp HLSLParser.cpp GLSLGenerator.cpp)
target_include_directories(neon_hlsl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(neon_hlsl PROPERTIES POSITION_INDEPENDENT_CODE ON)
