add_library(ulacr STATIC
    # core
    core/types.cpp
    core/codec_config.cpp
    # io
    io/wav_reader.cpp
    io/wav_writer.cpp
    io/bitstream.cpp
    io/ulacr_file.cpp
    # analysis
    analysis/segmenter.cpp
    analysis/feature_extractor.cpp
    analysis/harmonic_model.cpp
    # graph
    graph/block_graph.cpp
    graph/lsh.cpp
    # dictionary
    dictionary/spectral_dict.cpp
    # transform
    transform/wavelet.cpp
    transform/fft.cpp
    # coding
    coding/rans_coder.cpp
    coding/context_model.cpp
    coding/residual_island.cpp
    coding/bitplane.cpp
    # top-level
    encoder.cpp
    decoder.cpp
)

target_include_directories(ulacr
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(ulacr PUBLIC cxx_std_20)
