add_executable(redis_server actions / db.cpp actions / snapshot.cpp actions /
			   gate.cpp commands / cmd.cpp commands / cmd.cpp commands /
			   command_type.cpp error / error.cpp net / http_server.cpp net /
			   http_query_handler.cpp main.cpp primitive / value.cpp utils /
			   create_temp_file.cpp utils / error_ret_formatter.cpp utils /
			   split_by_space.cpp)

	target_include_directories(redis_server PUBLIC.) target_link_libraries(
		redis_server PUBLIC
			spdlog::spdlog Boost::system Boost::thread cereal::cereal)
		target_compile_definitions(redis_server PUBLIC BOOST_ASIO_NO_DEPRECATED)
			target_compile_options(redis_server PUBLIC - fsanitize = address)
				target_link_options(redis_server PUBLIC - fsanitize = address)
