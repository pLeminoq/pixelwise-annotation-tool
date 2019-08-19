cmake_minimum_required(VERSION 2.6)
set (CMAKE_CXX_STANDARD 11)

find_package( OpenCV 3 REQUIRED PATHS "/homes/thuxohl/tools/opencv_install" )
include_directories( ${OpenCV_INCLUDE_DIRS} )

find_package(Boost COMPONENTS system filesystem program_options REQUIRED)
include_directories(${Boost_INCLUDE_DIR})

add_executable( annotation_tool src/annotate.cpp)
target_link_libraries( annotation_tool ${Boost_LIBRARIES} ${OpenCV_LIBS})
