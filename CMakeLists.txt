cmake_minimum_required(VERSION 3.10)

project(nbv_simulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV 4.4.0 REQUIRED)

find_package(Boost REQUIRED COMPONENTS thread)

find_package(Eigen3 3.3.9 REQUIRED)

find_package(PCL 1.9.1 REQUIRED)

find_package(octomap 1.9.1 REQUIRED)


set(NBV_SIMULATION_SOURCES nbv_simulation/main.cpp)
set(NBV_SIMULATION_HEADERS
        nbv_simulation/Information.hpp
        nbv_simulation/Share_Data.hpp
        nbv_simulation/View_Space.hpp)

add_executable(nbv_simulation ${NBV_SIMULATION_SOURCES})

target_include_directories(nbv_simulation PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/nbv_simulation>
        $<INSTALL_INTERFACE:include>
        ${OCTOMAP_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS}
        ${PCL_INCLUDE_DIRS})

target_link_libraries(nbv_simulation PUBLIC
        Eigen3::Eigen
        ${OpenCV_LIBRARIES}
        ${PCL_LIBRARIES}
        ${OCTOMAP_LIBRARIES}
        Boost::thread)