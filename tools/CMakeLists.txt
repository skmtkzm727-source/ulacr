add_executable(ulacr-encode ulacr_encode.cpp)
target_link_libraries(ulacr-encode PRIVATE ulacr)

add_executable(ulacr-decode ulacr_decode.cpp)
target_link_libraries(ulacr-decode PRIVATE ulacr)

add_executable(gen_test_wav gen_test_wav.cpp)
target_link_libraries(gen_test_wav PRIVATE ulacr)
