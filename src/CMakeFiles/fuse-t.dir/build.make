# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.31

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /opt/homebrew/bin/cmake

# The command to remove a file.
RM = /opt/homebrew/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/gecko/src/one.finden

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/gecko/src/one.finden/src

# Include any dependencies generated for this target.
include CMakeFiles/fuse-t.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/fuse-t.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/fuse-t.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/fuse-t.dir/flags.make

CMakeFiles/fuse-t.dir/codegen:
.PHONY : CMakeFiles/fuse-t.dir/codegen

CMakeFiles/fuse-t.dir/main.cpp.o: CMakeFiles/fuse-t.dir/flags.make
CMakeFiles/fuse-t.dir/main.cpp.o: main.cpp
CMakeFiles/fuse-t.dir/main.cpp.o: CMakeFiles/fuse-t.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/Users/gecko/src/one.finden/src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/fuse-t.dir/main.cpp.o"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/fuse-t.dir/main.cpp.o -MF CMakeFiles/fuse-t.dir/main.cpp.o.d -o CMakeFiles/fuse-t.dir/main.cpp.o -c /Users/gecko/src/one.finden/src/main.cpp

CMakeFiles/fuse-t.dir/main.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/fuse-t.dir/main.cpp.i"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/gecko/src/one.finden/src/main.cpp > CMakeFiles/fuse-t.dir/main.cpp.i

CMakeFiles/fuse-t.dir/main.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/fuse-t.dir/main.cpp.s"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/gecko/src/one.finden/src/main.cpp -o CMakeFiles/fuse-t.dir/main.cpp.s

CMakeFiles/fuse-t.dir/fuse_server.cpp.o: CMakeFiles/fuse-t.dir/flags.make
CMakeFiles/fuse-t.dir/fuse_server.cpp.o: fuse_server.cpp
CMakeFiles/fuse-t.dir/fuse_server.cpp.o: CMakeFiles/fuse-t.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/Users/gecko/src/one.finden/src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object CMakeFiles/fuse-t.dir/fuse_server.cpp.o"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/fuse-t.dir/fuse_server.cpp.o -MF CMakeFiles/fuse-t.dir/fuse_server.cpp.o.d -o CMakeFiles/fuse-t.dir/fuse_server.cpp.o -c /Users/gecko/src/one.finden/src/fuse_server.cpp

CMakeFiles/fuse-t.dir/fuse_server.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/fuse-t.dir/fuse_server.cpp.i"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/gecko/src/one.finden/src/fuse_server.cpp > CMakeFiles/fuse-t.dir/fuse_server.cpp.i

CMakeFiles/fuse-t.dir/fuse_server.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/fuse-t.dir/fuse_server.cpp.s"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/gecko/src/one.finden/src/fuse_server.cpp -o CMakeFiles/fuse-t.dir/fuse_server.cpp.s

CMakeFiles/fuse-t.dir/nfs_server.cpp.o: CMakeFiles/fuse-t.dir/flags.make
CMakeFiles/fuse-t.dir/nfs_server.cpp.o: nfs_server.cpp
CMakeFiles/fuse-t.dir/nfs_server.cpp.o: CMakeFiles/fuse-t.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/Users/gecko/src/one.finden/src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building CXX object CMakeFiles/fuse-t.dir/nfs_server.cpp.o"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/fuse-t.dir/nfs_server.cpp.o -MF CMakeFiles/fuse-t.dir/nfs_server.cpp.o.d -o CMakeFiles/fuse-t.dir/nfs_server.cpp.o -c /Users/gecko/src/one.finden/src/nfs_server.cpp

CMakeFiles/fuse-t.dir/nfs_server.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/fuse-t.dir/nfs_server.cpp.i"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/gecko/src/one.finden/src/nfs_server.cpp > CMakeFiles/fuse-t.dir/nfs_server.cpp.i

CMakeFiles/fuse-t.dir/nfs_server.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/fuse-t.dir/nfs_server.cpp.s"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/gecko/src/one.finden/src/nfs_server.cpp -o CMakeFiles/fuse-t.dir/nfs_server.cpp.s

CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o: CMakeFiles/fuse-t.dir/flags.make
CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o: mount/mount_manager.cpp
CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o: CMakeFiles/fuse-t.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/Users/gecko/src/one.finden/src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Building CXX object CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o -MF CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o.d -o CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o -c /Users/gecko/src/one.finden/src/mount/mount_manager.cpp

CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.i"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/gecko/src/one.finden/src/mount/mount_manager.cpp > CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.i

CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.s"
	/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/gecko/src/one.finden/src/mount/mount_manager.cpp -o CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.s

# Object files for target fuse-t
fuse__t_OBJECTS = \
"CMakeFiles/fuse-t.dir/main.cpp.o" \
"CMakeFiles/fuse-t.dir/fuse_server.cpp.o" \
"CMakeFiles/fuse-t.dir/nfs_server.cpp.o" \
"CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o"

# External object files for target fuse-t
fuse__t_EXTERNAL_OBJECTS =

fuse-t: CMakeFiles/fuse-t.dir/main.cpp.o
fuse-t: CMakeFiles/fuse-t.dir/fuse_server.cpp.o
fuse-t: CMakeFiles/fuse-t.dir/nfs_server.cpp.o
fuse-t: CMakeFiles/fuse-t.dir/mount/mount_manager.cpp.o
fuse-t: CMakeFiles/fuse-t.dir/build.make
fuse-t: CMakeFiles/fuse-t.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/Users/gecko/src/one.finden/src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_5) "Linking CXX executable fuse-t"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/fuse-t.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/fuse-t.dir/build: fuse-t
.PHONY : CMakeFiles/fuse-t.dir/build

CMakeFiles/fuse-t.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/fuse-t.dir/cmake_clean.cmake
.PHONY : CMakeFiles/fuse-t.dir/clean

CMakeFiles/fuse-t.dir/depend:
	cd /Users/gecko/src/one.finden/src && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/gecko/src/one.finden /Users/gecko/src/one.finden /Users/gecko/src/one.finden/src /Users/gecko/src/one.finden/src /Users/gecko/src/one.finden/src/CMakeFiles/fuse-t.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/fuse-t.dir/depend

