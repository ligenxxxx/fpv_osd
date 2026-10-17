cmake_minimum_required(VERSION 3.10)

project(fpv_osd C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB SRC_FILES_CORE   "src/core/*.c" "src/core/*.h")
file(GLOB SRC_FILES_DRIVER "src/driver/*.c"  "src/driver/*.cpp" "src/driver/*.h")

set(SRC_FILES
	${SRC_FILES_CORE}
	${SRC_FILES_DRIVER}
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    src/
	src/core
	src/driver
)


find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)

target_include_directories(${PROJECT_NAME} PRIVATE
	${DRM_INCLUDE_DIRS}
	${LIBCAMERA_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${DRM_LIBRARIES}
	${LIBCAMERA_LIBRARIES}
)
