# libchdr (https://github.com/rtissera/libchdr) -- BSD-3-Clause.  See LICENSE.txt.
add_library(chdr STATIC
    src/libchdr_bitstream.c
    src/libchdr_cdrom.c
    src/libchdr_chd.c
    src/libchdr_codec_cdfl.c
    src/libchdr_codec_cdlz.c
    src/libchdr_codec_cdzl.c
    src/libchdr_codec_cdzs.c
    src/libchdr_codec_flac.c
    src/libchdr_codec_huff.c
    src/libchdr_codec_lzma.c
    src/libchdr_codec_zlib.c
    src/libchdr_codec_zstd.c
    src/libchdr_flac.c
    src/libchdr_huffman.c
    deps/lzma-25.01/src/LzmaDec.c
    deps/miniz-3.1.1/miniz.c
    deps/zstd-1.5.7/zstddeclib.c
)
target_include_directories(chdr PUBLIC include)
target_compile_definitions(chdr PRIVATE
    MINIZ_NO_ARCHIVE_APIS
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_STDIO
    MINIZ_NO_TIME
    WANT_RAW_DATA_SECTOR=1
    WANT_SUBCODE=1
    VERIFY_BLOCK_CRC=1
)
set_target_properties(chdr PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(chdr PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
