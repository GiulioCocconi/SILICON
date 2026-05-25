# Modified by Giulio Cocconi, for use in the SILICON Project, 2026
# Original code is licensed under LGPL version 2.1
# Copyright (C) 2004-2013  Monash University

# to see the diff between the current version and the one in the original repo run:

# LINK=https://raw.githubusercontent.com/Aksem/adaptagrams/refs/tags/v1.0.6/cola/libavoid/CMakeLists.txt
# diff <(curl $LINK) vendor/libavoid/CMakeLists.txt -u

cmake_minimum_required(VERSION 3.16)

project(avoid)

add_library(${PROJECT_NAME}
    actioninfo.cpp
    connectionpin.cpp
    connector.cpp
    connend.cpp
    geometry.cpp
    geomtypes.cpp
    graph.cpp
    hyperedge.cpp
    hyperedgeimprover.cpp
    hyperedgetree.cpp
    junction.cpp
    makepath.cpp
    mtst.cpp
    obstacle.cpp
    orthogonal.cpp
    router.cpp
    scanline.cpp
    shape.cpp
    timer.cpp
    vertices.cpp
    viscluster.cpp
    visibility.cpp
    vpsc.cpp
    uniqueid.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
