file(GLOB kernel_sources CONFIGURE_DEPENDS "*.c" "*.S")
add_library(kernelx STATIC ${kernel_sources})
set_property(SOURCE ${kernel_sources} PROPERTY LANGUAGE C)