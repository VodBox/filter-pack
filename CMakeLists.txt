project(filter-pack)

option(sRGB_SUPPORT "Whether to build assuming sRGB is available" ON)

include_directories("${CMAKE_SOURCE_DIR}/UI")

find_package(Qt5Widgets REQUIRED)

set(filter-pack_SOURCES
	filter-pack.cpp
	corner-pin-filter.cpp
	corner-pin-widget.cpp
	lens-distortion-filter.cpp
	stroke-filter.cpp)
	
set(filter-pack_HEADERS
	corner-pin-widget.hpp)
	
add_library(filter-pack MODULE
	${filter-pack_SOURCES}
	${filter-pack_HEADERS})
	
target_link_libraries(filter-pack
	libobs
	obs-frontend-api
	Qt5::Widgets)

set_target_properties(filter-pack PROPERTIES FOLDER "plugins")

if(sRGB_SUPPORT)
    target_compile_definitions(filter-pack PRIVATE sRGB_SUPPORT)
endif()

install_obs_plugin_with_data(filter-pack data)