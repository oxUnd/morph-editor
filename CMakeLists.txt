cmake_minimum_required(VERSION 3.16)
project(morph-editor C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(
	-Wall -Wextra -Werror
	-Wstrict-prototypes
	-Wold-style-definition
	-Wdeclaration-after-statement
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUV REQUIRED libuv)

add_executable(morph-editor
	src/main.c
	src/arena.c
	src/editor.c
	src/image.c
	src/render.c
	src/bbox.c
	src/mask.c
	src/draw.c
	src/canvas.c
	src/transform.c
	src/video.c
	src/protocol.c
	src/headless.c
	src/history.c
	src/terminal.c
	src/json_util.c
	src/layout.c
	src/arrow.c
	src/util.c
	vendor/cJSON.c
	vendor/termbox2_impl.c
)

target_include_directories(morph-editor PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/vendor
	${LIBUV_INCLUDE_DIRS}
)

target_link_libraries(morph-editor PRIVATE
	${LIBUV_LIBRARIES}
	m
)

target_link_directories(morph-editor PRIVATE
	${LIBUV_LIBRARY_DIRS}
)

target_compile_options(morph-editor PRIVATE
	${LIBUV_CFLAGS_OTHER}
)

# termbox2 uses C99+ declarations, disable strict C89 check for it
set_source_files_properties(vendor/termbox2_impl.c PROPERTIES
	COMPILE_FLAGS "-Wno-declaration-after-statement -Wno-unused-function"
)

# Common library sources for tests
set(TEST_LIB_SOURCES
	src/arena.c
	src/image.c
	src/bbox.c
	src/mask.c
	src/draw.c
	src/render.c
	src/canvas.c
	src/transform.c
	src/history.c
	src/json_util.c
	src/layout.c
	src/arrow.c
	src/util.c
	src/terminal.c
	vendor/cJSON.c
	vendor/termbox2_impl.c
)

# STB implementations are in image.c/mask.c/draw.c, need defines
add_library(morph_test_lib STATIC ${TEST_LIB_SOURCES})
target_include_directories(morph_test_lib PRIVATE
	${CMAKE_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/vendor
	${LIBUV_INCLUDE_DIRS}
)
target_link_libraries(morph_test_lib PRIVATE m ${LIBUV_LIBRARIES})
target_link_directories(morph_test_lib PRIVATE ${LIBUV_LIBRARY_DIRS})
target_compile_options(morph_test_lib PRIVATE ${LIBUV_CFLAGS_OTHER})

set_source_files_properties(vendor/termbox2_impl.c PROPERTIES
	COMPILE_FLAGS "-Wno-declaration-after-statement -Wno-unused-function"
)

# Test executables
function(add_morph_test name)
	add_executable(${name} tests/${name}.c)
	target_link_libraries(${name} PRIVATE morph_test_lib m)
	target_include_directories(${name} PRIVATE
		${CMAKE_SOURCE_DIR}/src
		${CMAKE_SOURCE_DIR}/vendor
		${LIBUV_INCLUDE_DIRS}
	)
	target_link_directories(${name} PRIVATE ${LIBUV_LIBRARY_DIRS})
	target_compile_options(${name} PRIVATE ${LIBUV_CFLAGS_OTHER})
	add_test(NAME ${name} COMMAND ${name})
endfunction()

enable_testing()
add_morph_test(test_arena)
add_morph_test(test_bbox)
add_morph_test(test_mask)
add_morph_test(test_draw)
add_morph_test(test_transform)
add_morph_test(test_canvas)
add_morph_test(test_protocol)
add_morph_test(test_history)
add_morph_test(test_image)
