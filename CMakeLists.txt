project(filter-pack)

include_directories("${CMAKE_SOURCE_DIR}/UI")

find_package(Qt5Widgets REQUIRED)

set(filter-pack_SOURCES
	filter-pack.cpp
	corner-pin-filter.cpp
	corner-pin-widget.cpp
	lens-distortion-filter.cpp)
	
set(filter-pack_HEADERS
	corner-pin-widget.hpp)
	
add_library(filter-pack MODULE
	${filter-pack_SOURCES}
	${filter-pack_HEADERS})
	
target_link_libraries(filter-pack
	libobs
	obs-frontend-api
	Qt5::Widgets)

install_obs_plugin_with_data(filter-pack data)