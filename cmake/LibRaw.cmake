include(FetchContent)
FetchContent_Declare(libraw_source
  URL https://github.com/LibRaw/LibRaw/archive/refs/tags/0.22.3.tar.gz
  URL_HASH SHA256=7edf96c5b70cff88980a02ab0fc8af125cb6a4a377985573c654439d9d02355c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libraw_source)
file(GLOB_RECURSE RAW_SOURCES CONFIGURE_DEPENDS "${libraw_source_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE RAW_PLACEHOLDERS CONFIGURE_DEPENDS "${libraw_source_SOURCE_DIR}/src/*_ph.cpp")
list(REMOVE_ITEM RAW_SOURCES ${RAW_PLACEHOLDERS})
add_library(sifter_raw SHARED ${RAW_SOURCES})
target_include_directories(sifter_raw PUBLIC "${libraw_source_SOURCE_DIR}")
target_compile_definitions(sifter_raw PRIVATE LIBRAW_BUILDLIB)
set_target_properties(sifter_raw PROPERTIES OUTPUT_NAME "sifter_raw" AUTOMOC OFF AUTORCC OFF)
if(WIN32)
  target_link_libraries(sifter_raw PRIVATE ws2_32)
endif()
