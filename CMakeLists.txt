cmake_minimum_required(VERSION 2.8.7)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if(NOT COMPILER_SUPPORTS_CXX11)
  message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

if(USE_libstdcpp)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++")
  message("-- Warning: forcing libstdc++ (controlled by USE_libstdcpp option in cmake)")
endif()

# compile flags
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -O2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -D_REENTRANT -fPIC -Wall -Werror")


# ---[ gflags
include(CNStream/cmake/FindGFlags.cmake)
include_directories(${GFLAGS_INCLUDE_DIRS})

# ---[ Opencv
find_package(OpenCV REQUIRED core imgproc highgui features2d)
if(OpenCV_FOUND)
  include_directories(${OpenCV_INCLUDE_DIRS})
  message(STATUS "opencv include: ${OpenCV_INCLUDE_DIRS}")
  message(STATUS "opencv libraries: ${OpenCV_LIBS}")
else()
  message(FATAL_ERROR "opencv not found!")
endif()

# ---[ Rapidjson
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/3rdparty/rapidjson/include)


add_subdirectory(CNStream)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/modules)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/modules/source/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/modules/inference/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/modules/inference2/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/modules/util/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/framework/core/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/easydk/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/easydk/infer_server/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/lib)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/3rdparty/live555/lib)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/CNStream/3rdparty/libyuv/include)

if(NOT DEFINED ENV{NEUWARE_HOME})
  set(ENV{NEUWARE_HOME} /usr/local/neuware)
endif()
include_directories("$ENV{NEUWARE_HOME}/include")
link_directories("$ENV{NEUWARE_HOME}/lib64")

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src srcs)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)

add_executable(zhongyuan_server ${srcs})
target_link_libraries(zhongyuan_server cnstream_core cnstream_va easydk cnrt cndrv ${OpenCV_LIBS} pthread dl)

