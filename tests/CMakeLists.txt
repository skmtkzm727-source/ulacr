add_executable(test_rans test_rans.cpp)
target_link_libraries(test_rans PRIVATE ulacr)
add_test(NAME test_rans COMMAND test_rans)

add_executable(test_wavelet test_wavelet.cpp)
target_link_libraries(test_wavelet PRIVATE ulacr)
add_test(NAME test_wavelet COMMAND test_wavelet)

add_executable(test_bitplane_island test_bitplane_island.cpp)
target_link_libraries(test_bitplane_island PRIVATE ulacr)
add_test(NAME test_bitplane_island COMMAND test_bitplane_island)

add_executable(test_roundtrip test_roundtrip.cpp)
target_link_libraries(test_roundtrip PRIVATE ulacr)
add_test(NAME test_roundtrip COMMAND test_roundtrip)
