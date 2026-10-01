  
cmake_minimum_required(VERSION 3.15.0)
# MSVC runtime library flags are selected by an abstraction
# https://cmake.org/cmake/help/latest/policy/CMP0091.html
cmake_policy(SET CMP0091 NEW) 
cmake_policy(SET CMP0067 NEW)

option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_HEXRAYS_SUPPORT "Use the Hex-Rays SDK to provide Ponce feedback on the pseudocode" ON)

set(IDA_INSTALLED_DIR "" CACHE PATH "Path to directory where IDA is installed. If set, triton plugin will be moved there after building")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set (PROJECT_NAME Ponce)
project(${PROJECT_NAME})


if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "IDA plugins >7.0 can't be built as 32 bits modules")
endif()

file(GLOB PONCE_SOURCE_FILES
    src/*.cpp
)
file(GLOB PONCE_HEADER_FILES
    src/*.hpp
)

add_library(${PROJECT_NAME} SHARED ${PONCE_SOURCE_FILES} ${PONCE_HEADER_FILES})
add_library(${PROJECT_NAME}64 SHARED ${PONCE_SOURCE_FILES} ${PONCE_HEADER_FILES})

#                       #
# Look for dependencies #
#                       # 

# Look for IDA SDK 
set(IDASDK_ROOT_DIR "" CACHE PATH "Path to IDA SDK root (containing include/, or src/include/)")

if(NOT IDASDK_ROOT_DIR)
	message(FATAL_ERROR "You should set IDASDK_ROOT_DIR to the IDA SDK path")
endif()

if(EXISTS "${IDASDK_ROOT_DIR}/src/include/ida.hpp")
	set(IDASDK_ROOT_DIR "${IDASDK_ROOT_DIR}/src")
endif()
set(IDA_INCLUDE_DIR ${IDASDK_ROOT_DIR}/include)
file(STRINGS "${IDA_INCLUDE_DIR}/pro.h" sdk_version_line REGEX "^#define IDA_SDK_VERSION[ \t]+[0-9]+")
string(REGEX MATCH "[0-9]+$" sdk_version "${sdk_version_line}")
if(sdk_version GREATER_EQUAL 900)
	# IDA 9 uses a single 64-bit-address-space plugin binary.
	set_target_properties(${PROJECT_NAME} PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# IDA SDK libs.
if(WIN32)
	set(idasdk_ea32 "${IDASDK_ROOT_DIR}/lib/x64_win_vc_32/ida.lib")
	set(idasdk_ea64 "${IDASDK_ROOT_DIR}/lib/x64_win_vc_64/ida.lib")
elseif(APPLE)
	if(EXISTS "${IDASDK_ROOT_DIR}/lib/x64_mac_clang_64/libida.dylib")
		set(idasdk_ea32 "${IDASDK_ROOT_DIR}/lib/x64_mac_clang_32/libida.dylib")
		set(idasdk_ea64 "${IDASDK_ROOT_DIR}/lib/x64_mac_clang_64/libida.dylib")
	elseif(USE_CLANG)
		set(idasdk_ea32 "${IDASDK_ROOT_DIR}/lib/x64_mac_clang_32/libida.dylib")
		set(idasdk_ea64 "${IDASDK_ROOT_DIR}/lib/x64_mac_clang_64/libida64.dylib")
	else()
		set(idasdk_ea32 "${IDASDK_ROOT_DIR}/lib/x64_mac_gcc_32/libida.dylib")
		set(idasdk_ea64 "${IDASDK_ROOT_DIR}/lib/x64_mac_gcc_64/libida64.dylib")
	endif()
elseif(UNIX) # APPLE is also UNIX, so it MUST be before this elseif().
	set(idasdk_ea32 "${IDASDK_ROOT_DIR}/lib/x64_linux_gcc_32/libida.so")
	if(EXISTS "${IDASDK_ROOT_DIR}/lib/x64_linux_gcc_64/libida.so")
		set(idasdk_ea64 "${IDASDK_ROOT_DIR}/lib/x64_linux_gcc_64/libida.so")
	else()
		set(idasdk_ea64 "${IDASDK_ROOT_DIR}/lib/x64_linux_gcc_64/libida64.so")
	endif()
	set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
else()
	message(FATAL_ERROR "Unsupported system type: ${CMAKE_SYSTEM_NAME}")
endif()

if(sdk_version GREATER_EQUAL 900 AND NOT EXISTS "${idasdk_ea64}")
	message(FATAL_ERROR "Missing IDA SDK ${sdk_version} library: ${idasdk_ea64}")
endif()

# Find Triton
find_package(triton CONFIG REQUIRED)

if(BUILD_EXAMPLES)
	set_property(GLOBAL PROPERTY USE_FOLDERS ON)

	add_executable(crackme_hash examples/crackme_hash.cpp)
	add_executable(crackme_xor examples/crackme_xor.cpp)
	add_executable(fread_SAGE examples/fread_SAGE.cpp)
	add_executable(long_time_to_solve examples/long_time_to_solve.cpp)
	set_target_properties(crackme_hash
		PROPERTIES
		FOLDER "Examples")
	set_target_properties(crackme_xor
		PROPERTIES
		FOLDER "Examples")
	set_target_properties(fread_SAGE
		PROPERTIES
		FOLDER "Examples")
	set_target_properties(long_time_to_solve
		PROPERTIES
		FOLDER "Examples")
endif()

# Look for hexrays SDK to provide Ponce feedback in the pseudocode	
find_file(HEXRAYS_PATH hexrays.hpp PATHS ${IDA_INCLUDE_DIR} NO_DEFAULT_PATH)	
if(BUILD_HEXRAYS_SUPPORT)	
	if (NOT HEXRAYS_PATH)	
		message(FATAL_ERROR "You should add hexrays.hpp to ${IDA_INCLUDE_DIR}")	
	else()	
		target_compile_definitions(${PROJECT_NAME} PRIVATE BUILD_HEXRAYS_SUPPORT)	
		target_compile_definitions(${PROJECT_NAME}64 PRIVATE BUILD_HEXRAYS_SUPPORT)	
	endif()	
endif()

get_filename_component(a_dir ${IDASDK_ROOT_DIR} DIRECTORY)



# Now we create the projects (one for x86 and other for x64)
target_include_directories(${PROJECT_NAME} PRIVATE ${IDA_INCLUDE_DIR})
target_include_directories(${PROJECT_NAME}64 PRIVATE ${IDA_INCLUDE_DIR})
if(UNIX)
	# IDA supplies libida.so at runtime; never embed the build SDK's path.
	set_target_properties(${PROJECT_NAME} ${PROJECT_NAME}64 PROPERTIES SKIP_BUILD_RPATH TRUE)
endif()

target_link_libraries(
    ${PROJECT_NAME}
	PRIVATE
    triton::triton
	${idasdk_ea32}
)

target_link_libraries(
    ${PROJECT_NAME}64
	PRIVATE
    triton::triton
	${idasdk_ea64}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE __X64__ __IDP__)
target_compile_definitions(${PROJECT_NAME}64 PRIVATE __X64__ __IDP__ __EA64__)

if(WIN32)
	target_compile_definitions(${PROJECT_NAME} PRIVATE __NT__)
	target_compile_definitions(${PROJECT_NAME}64 PRIVATE __NT__)
	set(PLUGIN_EXTENSION dll)
	add_definitions(/MP)
	# If using the static library we should use the static runtime too
	set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
	set_property(TARGET ${PROJECT_NAME}64 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
elseif (APPLE)
	set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS_RELEASE "-Wl,-dead_strip")
	set_target_properties(${PROJECT_NAME}64 PROPERTIES LINK_FLAGS_RELEASE "-Wl,-dead_strip")
	target_compile_definitions(${PROJECT_NAME} PRIVATE __MAC__ USE_DANGEROUS_FUNCTIONS USE_STANDARD_FILE_FUNCTIONS)
	target_compile_definitions(${PROJECT_NAME}64 PRIVATE __MAC__ USE_DANGEROUS_FUNCTIONS USE_STANDARD_FILE_FUNCTIONS)
	# Prevent creating ponce binaries as libPonce.so but do Ponce.so
	SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES PREFIX "")
	SET_TARGET_PROPERTIES(${PROJECT_NAME}64 PROPERTIES PREFIX "")
	set(PLUGIN_EXTENSION dylib)
elseif (UNIX AND NOT APPLE)
	set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "-ffunction-sections -fdata-sections" )
	set_target_properties(${PROJECT_NAME}64 PROPERTIES COMPILE_FLAGS "-ffunction-sections -fdata-sections" )
	set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS_RELEASE "-s -Wl,--gc-sections")
	set_target_properties(${PROJECT_NAME}64 PROPERTIES LINK_FLAGS_RELEASE "-s -Wl,--gc-sections")
	target_compile_definitions(${PROJECT_NAME} PRIVATE __LINUX__ USE_DANGEROUS_FUNCTIONS)
	target_compile_definitions(${PROJECT_NAME}64 PRIVATE __LINUX__ USE_DANGEROUS_FUNCTIONS)
	# Prevent creating ponce binaries as libPonce.so but do Ponce.so
	SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES PREFIX "")
	SET_TARGET_PROPERTIES(${PROJECT_NAME}64 PROPERTIES PREFIX "")
	set(PLUGIN_EXTENSION so)
endif()


# Clean as much symbols as we can in OSX and Linux
if(NOT WIN32)	
	if(EXISTS "/usr/bin/strip")	
		message(STATUS "[-] Symbols will be stripped using strip after build")
		# Strip binary for release builds
		if (CMAKE_BUILD_TYPE STREQUAL Release)
			add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
					COMMAND strip -S $<TARGET_FILE:${PROJECT_NAME}>
					COMMENT "Symbols stripped from ${PROJECT_NAME}")
			add_custom_command(TARGET ${PROJECT_NAME}64 POST_BUILD
					COMMAND strip -S $<TARGET_FILE:${PROJECT_NAME}64>
					COMMENT "Symbols stripped from ${PROJECT_NAME}64")
		endif ()
	endif()
endif()

if(IDA_INSTALLED_DIR)
	message(STATUS "[-] Ponce built plugin and pdb file will be moved to '${IDA_INSTALLED_DIR}/plugins/'. The build system should have permisions to write there or it will error.")
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${IDA_INSTALLED_DIR}/plugins/Ponce.${PLUGIN_EXTENSION}
		COMMENT "Created ${IDA_INSTALLED_DIR}/plugins/${PROJECT_NAME}.${PLUGIN_EXTENSION}"
	)
	
	add_custom_command(TARGET ${PROJECT_NAME}64 POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}64> ${IDA_INSTALLED_DIR}/plugins/Ponce64.${PLUGIN_EXTENSION}
			COMMENT "Created ${IDA_INSTALLED_DIR}/plugins/${PROJECT_NAME}64.${PLUGIN_EXTENSION}"
		)

	# Move symbols for debugging in Windows
	if(WIN32)
		add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/Debug/${PROJECT_NAME}.pdb ${IDA_INSTALLED_DIR}/plugins/Ponce.pdb
		COMMENT "Created ${IDA_INSTALLED_DIR}/plugins/${PROJECT_NAME}.${PLUGIN_EXTENSION}"
		)
		
		add_custom_command(TARGET ${PROJECT_NAME}64 POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/Debug/${PROJECT_NAME}64.pdb ${IDA_INSTALLED_DIR}/plugins/Ponce64.pdb
			COMMENT "Created ${IDA_INSTALLED_DIR}/plugins/${PROJECT_NAME}64.${PLUGIN_EXTENSION}"
		)
	endif()
endif()
