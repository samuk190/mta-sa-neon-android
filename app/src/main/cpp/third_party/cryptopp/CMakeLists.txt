# Crypto++ 8.9.0 as vendored by desktop MTA (mta_neon_source/vendor/cryptopp). The file list
# is the desktop premake5.lua list. CRYPTOPP_DISABLE_ASM is the desktop ARM setting
# (premake5.lua filter platforms:arm*); it is also used for the host build so both
# builds run the same portable code. Used by encodeString/decodeString (aes128, rsa,
# base64, base32) and generateKeyPair (LuaCryptApi.cpp).
add_library(neon_cryptopp STATIC
    3way.cpp
    adler32.cpp
    algebra.cpp
    algparam.cpp
    allocate.cpp
    arc4.cpp
    aria.cpp
    ariatab.cpp
    asn.cpp
    authenc.cpp
    base32.cpp
    base64.cpp
    basecode.cpp
    bfinit.cpp
    blake2.cpp
    blake2b_simd.cpp
    blake2s_simd.cpp
    blowfish.cpp
    blumshub.cpp
    camellia.cpp
    cast.cpp
    casts.cpp
    cbcmac.cpp
    ccm.cpp
    chacha.cpp
    chachapoly.cpp
    chacha_avx.cpp
    chacha_simd.cpp
    cham.cpp
    cham_simd.cpp
    channels.cpp
    cmac.cpp
    cpu.cpp
    crc.cpp
    crc_simd.cpp
    cryptlib.cpp
    darn.cpp
    default.cpp
    des.cpp
    dessp.cpp
    dh.cpp
    dh2.cpp
    dll.cpp
    donna_32.cpp
    donna_64.cpp
    donna_sse.cpp
    dsa.cpp
    eax.cpp
    ec2n.cpp
    eccrypto.cpp
    ecp.cpp
    elgamal.cpp
    emsa2.cpp
    eprecomp.cpp
    esign.cpp
    files.cpp
    filters.cpp
    fips140.cpp
    gcm.cpp
    gcm_simd.cpp
    gf256.cpp
    gf2n.cpp
    gf2n_simd.cpp
    gf2_32.cpp
    gfpcrypt.cpp
    gost.cpp
    gzip.cpp
    hc128.cpp
    hc256.cpp
    hex.cpp
    hight.cpp
    hmac.cpp
    hrtimer.cpp
    ida.cpp
    idea.cpp
    integer.cpp
    iterhash.cpp
    kalyna.cpp
    kalynatab.cpp
    keccak.cpp
    keccak_core.cpp
    keccak_simd.cpp
    lea.cpp
    lea_simd.cpp
    lsh256.cpp
    lsh256_avx.cpp
    lsh256_sse.cpp
    lsh512.cpp
    lsh512_avx.cpp
    lsh512_sse.cpp
    luc.cpp
    mars.cpp
    marss.cpp
    md2.cpp
    md4.cpp
    md5.cpp
    misc.cpp
    modes.cpp
    mqueue.cpp
    mqv.cpp
    nbtheory.cpp
    neon_simd.cpp
    oaep.cpp
    osrng.cpp
    padlkrng.cpp
    primetab.cpp
    panama.cpp
    pch.cpp
    pkcspad.cpp
    poly1305.cpp
    polynomi.cpp
    power7_ppc.cpp
    power8_ppc.cpp
    power9_ppc.cpp
    ppc_simd.cpp
    pssr.cpp
    pubkey.cpp
    queue.cpp
    rabbit.cpp
    rabin.cpp
    randpool.cpp
    rc2.cpp
    rc5.cpp
    rc6.cpp
    rdrand.cpp
    rdtables.cpp
    rijndael.cpp
    rijndael_simd.cpp
    ripemd.cpp
    rng.cpp
    rsa.cpp
    rw.cpp
    safer.cpp
    salsa.cpp
    scrypt.cpp
    seal.cpp
    seed.cpp
    serpent.cpp
    sha.cpp
    sha3.cpp
    shacal2.cpp
    shacal2_simd.cpp
    shake.cpp
    shark.cpp
    sharkbox.cpp
    sha_simd.cpp
    simeck.cpp
    simon.cpp
    simon128_simd.cpp
    simple.cpp
    skipjack.cpp
    sm3.cpp
    sm4.cpp
    sm4_simd.cpp
    sosemanuk.cpp
    speck.cpp
    speck128_simd.cpp
    square.cpp
    squaretb.cpp
    sse_simd.cpp
    strciphr.cpp
    tea.cpp
    tftables.cpp
    threefish.cpp
    tiger.cpp
    tigertab.cpp
    ttmac.cpp
    tweetnacl.cpp
    twofish.cpp
    vmac.cpp
    wake.cpp
    whrlpool.cpp
    xed25519.cpp
    xtr.cpp
    xtrcrypt.cpp
    xts.cpp
    zdeflate.cpp
    zinflate.cpp
    zlib.cpp
)
target_compile_definitions(neon_cryptopp PUBLIC CRYPTOPP_DISABLE_ASM CRYPTOPP_ENABLE_NAMESPACE_WEAK=1)
# Desktop builds it with warnings "Off".
target_compile_options(neon_cryptopp PRIVATE -w)
# Headers are included as <cryptopp/...>, as desktop does.
target_include_directories(neon_cryptopp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
set_target_properties(neon_cryptopp PROPERTIES POSITION_INDEPENDENT_CODE ON)
# cpu.cpp includes the NDK cpufeatures helper on Android (see the comment at cpu.cpp:67-70).
# It ships with the NDK, so it is built from there instead of being vendored.
if(ANDROID)
    set(NEON_CPUFEATURES_DIR "${ANDROID_NDK}/sources/android/cpufeatures")
    add_library(neon_cpufeatures STATIC "${NEON_CPUFEATURES_DIR}/cpu-features.c")
    target_compile_options(neon_cpufeatures PRIVATE -w)
    target_include_directories(neon_cpufeatures PUBLIC "${NEON_CPUFEATURES_DIR}")
    target_link_libraries(neon_cpufeatures PUBLIC dl)
    set_target_properties(neon_cpufeatures PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(neon_cryptopp PRIVATE neon_cpufeatures)
endif()
