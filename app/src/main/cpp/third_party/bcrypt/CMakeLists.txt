# crypt_blowfish (Openwall, public domain) as vendored by desktop MTA
# (mta_neon_source/vendor/bcrypt). Desktop builds the *.c files; x86.S is x86 only.
# Used by passwordHash / passwordVerify (LuaCryptApi.cpp).
add_library(neon_bcrypt STATIC crypt_blowfish.c crypt_gensalt.c wrapper.c)
target_compile_options(neon_bcrypt PRIVATE -w)
# Included as <bcrypt/ow-crypt.h>, as desktop does.
target_include_directories(neon_bcrypt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_target_properties(neon_bcrypt PROPERTIES POSITION_INDEPENDENT_CODE ON)
