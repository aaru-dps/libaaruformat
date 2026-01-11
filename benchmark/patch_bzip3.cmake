# Patch bzip3's CMakeLists.txt to fix CMAKE_PROJECT_NAME issue
file(READ "${SOURCE_DIR}/CMakeLists.txt" CONTENT)

# Replace ${CMAKE_PROJECT_NAME} with bzip3 in config file lines
string(REPLACE "\${CMAKE_PROJECT_NAME}-config.cmake" "bzip3-config.cmake" CONTENT "${CONTENT}")
string(REPLACE "\${CMAKE_PROJECT_NAME}-targets" "bzip3-targets" CONTENT "${CONTENT}")
string(REPLACE "NAMESPACE \${CMAKE_PROJECT_NAME}::" "NAMESPACE bzip3::" CONTENT "${CONTENT}")
string(REPLACE "cmake/\${CMAKE_PROJECT_NAME}" "cmake/bzip3" CONTENT "${CONTENT}")

# Write back
file(WRITE "${SOURCE_DIR}/CMakeLists.txt" "${CONTENT}")

message(STATUS "Patched bzip3 CMakeLists.txt to fix CMAKE_PROJECT_NAME references")

