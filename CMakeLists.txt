cmake_minimum_required(VERSION 3.15)
project(kill_fuck_dpi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/W4 /O2 /MD /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

add_executable(dpi_bypass
    src/main.cpp
    src/ProtocolParser.cpp
    src/DesyncEngine.cpp
    src/ConfigManager.cpp
    src/HostlistManager.cpp
    src/ServiceController.cpp
    src/GuiManager.cpp
)

target_link_libraries(dpi_bypass PRIVATE
    WinDivert
    ws2_32
    advapi32
    shell32
    comctl32
    user32
    gdi32
)

# Автоматическое копирование необходимых runtime файлов в директорию сборки
add_custom_command(TARGET dpi_bypass POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/WinDivert.dll"
        "$<TARGET_FILE_DIR:dpi_bypass>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/WinDivert64.sys"
        "$<TARGET_FILE_DIR:dpi_bypass>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
        "$<TARGET_FILE_DIR:dpi_bypass>"
    COMMENT "Копирование WinDivert runtime файлов и конфигурации в папку вывода"
)
