# libssh2_esp32 26814617, using the project's IDF mbedTLS and lwIP.
set(camera_ssh_sources
         src/channel.c src/comp.c src/crypt.c src/crypto.c src/global.c
         src/hostkey.c src/keepalive.c src/kex.c src/mac.c src/misc.c
         src/packet.c src/pem.c src/session.c src/transport.c src/userauth.c
         src/userauth_kbd_packet.c src/cipher-chachapoly.c src/chacha.c
         src/poly1305.c src/bcrypt_pbkdf.c)
if(COMMAND idf_component_register)
    idf_component_register(SRCS ${camera_ssh_sources} INCLUDE_DIRS include PRIV_INCLUDE_DIRS src
                           PRIV_REQUIRES mbedtls esp_netif)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H LIBSSH2_MBEDTLS HAVE_SELECT)
else()
    add_library(camera_ssh_host STATIC ${camera_ssh_sources})
    target_include_directories(camera_ssh_host PUBLIC include PRIVATE src)
    target_compile_definitions(camera_ssh_host PRIVATE LIBSSH2_MBEDTLS HAVE_IOCTLSOCKET_FIONBIO)
    target_link_libraries(camera_ssh_host PRIVATE mbedcrypto mbedx509 ws2_32)
endif()
