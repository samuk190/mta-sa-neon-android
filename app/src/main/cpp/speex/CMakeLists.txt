cmake_minimum_required(VERSION 3.22)
project(neon_speex C)
add_library(neon_speex STATIC
    libspeex/cb_search.c
    libspeex/exc_10_32_table.c
    libspeex/exc_8_128_table.c
    libspeex/filters.c
    libspeex/gain_table.c
    libspeex/hexc_table.c
    libspeex/high_lsp_tables.c
    libspeex/lsp.c
    libspeex/ltp.c
    libspeex/speex.c
    libspeex/stereo.c
    libspeex/vbr.c
    libspeex/vq.c
    libspeex/bits.c
    libspeex/exc_10_16_table.c
    libspeex/exc_20_32_table.c
    libspeex/exc_5_256_table.c
    libspeex/exc_5_64_table.c
    libspeex/gain_table_lbr.c
    libspeex/hexc_10_32_table.c
    libspeex/lpc.c
    libspeex/lsp_tables_nb.c
    libspeex/modes.c
    libspeex/modes_wb.c
    libspeex/nb_celp.c
    libspeex/quant_lsp.c
    libspeex/sb_celp.c
    libspeex/speex_callbacks.c
    libspeex/speex_header.c
    libspeex/window.c
)
target_include_directories(neon_speex PUBLIC include PRIVATE libspeex)
target_compile_definitions(neon_speex PRIVATE FLOATING_POINT USE_ALLOCA HAVE_ALLOCA_H EXPORT= SPEEX_VERSION="1.2.1" SPEEX_MAJOR_VERSION=1 SPEEX_MINOR_VERSION=2 SPEEX_MICRO_VERSION=1 SPEEX_EXTRA_VERSION="")
set_target_properties(neon_speex PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(neon_speex PUBLIC m)
