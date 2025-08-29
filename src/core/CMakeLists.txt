add_library(
    redis_core SHARED
    error/simple_message.cpp
    resp/parser.cpp
    resp/value.cpp
    utils/get_class_logger.cpp
    utils/deadline_timer.cpp
    utils/create_temp_file.cpp
    utils/toupper.cpp)

add_library(redis::core ALIAS redis_core)

target_compile_options(redis_core PUBLIC -fsanitize=address)
target_link_options(redis_core PUBLIC -fsanitize=address)
target_link_libraries(redis_core PUBLIC spdlog::spdlog magic_enum::magic_enum
                                  FastFloat::fast_float)

target_include_directories(
    redis_core PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INCLUDE_DIR}>)
