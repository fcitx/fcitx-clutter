PKG_CHECK_MODULES(GLIB2 REQUIRED "glib-2.0" )
PKG_CHECK_MODULES(DBUS_GLIB REQUIRED "dbus-glib-1")

_pkgconfig_invoke("glib-2.0" GLIB2 GLIB_GENMARSHAL "" "--variable=glib_genmarshal")

FIND_PROGRAM(GLIB_GENMARSHAL ${GLIB2_GLIB_GENMARSHAL})
PKG_CHECK_MODULES(CLUTTER_IM_CONTEXT REQUIRED "clutter-imcontext-0.1" )
PKG_CHECK_MODULES(CLUTTER_X11 REQUIRED "clutter-x11-1.0" )

_pkgconfig_invoke("clutter-imcontext-0.1" CLUTTER_IM_CONTEXT BINARY_VERSION "" "--variable=gtk_binary_version")
_pkgconfig_invoke("clutter-imcontext-0.1" CLUTTER_IM_CONTEXT LIBDIR "" "--variable=libdir")
set(CLUTTER_IM_MODULEDIR
 "${CLUTTER_IM_CONTEXT_LIBDIR}/clutter-imcontext/immodules"
)

include_directories(${CLUTTER_IM_CONTEXT_INCLUDE_DIRS}
                       ${CLUTTER_X11_INCLUDE_DIRS}
                       ${DBUS_GLIB_INCLUDE_DIRS}
                       ${CMAKE_CURRENT_BINARY_DIR}
                       ${PROJECT_BINARY_DIR}
)
link_directories(${CLUTTER_X11_LIBRARY_DIRS} ${CLUTTER_IM_CONTEXT_LIBRARY_DIRS} ${DBUS_GLIB_LIBRARY_DIRS})

add_custom_command(OUTPUT marshall.c
                   COMMAND ${GLIB_GENMARSHAL} --body --prefix=fcitx_marshall ${CMAKE_CURRENT_SOURCE_DIR}/marshall.list > marshall.c
)
add_custom_command(OUTPUT marshall.h
                   COMMAND ${GLIB_GENMARSHAL} --header --prefix=fcitx_marshall ${CMAKE_CURRENT_SOURCE_DIR}/marshall.list > marshall.h
)

set(FCITX_CLUTTER_IM_MODULE_SOURCES
    fcitxim.c
    fcitximcontext.c
    client.c
    ${CMAKE_CURRENT_BINARY_DIR}/marshall.c
    ${CMAKE_CURRENT_BINARY_DIR}/marshall.h
)

add_library(im-fcitx MODULE ${FCITX_CLUTTER_IM_MODULE_SOURCES})
set_target_properties( im-fcitx PROPERTIES PREFIX "" COMPILE_FLAGS "-fvisibility=hidden" LINK_FLAGS "-Wl,--no-undefined")
target_link_libraries( im-fcitx ${CLUTTER_X11_LIBRARIES} ${CLUTTER_IM_CONTEXT_LIBRARIES} ${DBUS_GLIB_LIBRARIES} fcitx-utils)

install(TARGETS im-fcitx DESTINATION ${CLUTTER_IM_MODULEDIR})
