add_executable(ulacr-wasm bridge.cpp)
target_link_libraries(ulacr-wasm PRIVATE ulacr)

target_link_options(ulacr-wasm PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=ULACRModule"
    "-sENVIRONMENT=web"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFORCE_FILESYSTEM=1"
    "-sEXPORTED_FUNCTIONS=['_ulacr_encode','_ulacr_decode','_ulacr_output_ptr','_ulacr_output_size','_ulacr_clear_output','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['HEAPU8']"
)

set_target_properties(ulacr-wasm PROPERTIES
    OUTPUT_NAME "ulacr"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/web-dist"
)
