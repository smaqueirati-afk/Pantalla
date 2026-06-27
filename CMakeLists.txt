cmake_minimum_required(VERSION 3.16)

# IMPORTANTE: el include DEBE ir antes de project()
# ESP-IDF usa este archivo para configurar el toolchain (compilador RISC-V)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(sm_domotica)
