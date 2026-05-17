MAKE_DIR		:= $(PWD)

RELEASE_DIR		:= $(MAKE_DIR)/bin/release
PROFILING_DIR	:= $(MAKE_DIR)/bin/profiling
DEBUG_DIR		:= $(MAKE_DIR)/bin/debug

C_FLAGS			:= -I$(MAKE_DIR)/src
C_FLAGS			+= -I$(MAKE_DIR)/src/Engine
C_FLAGS			+= -I$(MAKE_DIR)/src/Application
C_FLAGS			+= -I$(MAKE_DIR)/vendor
C_FLAGS			+= -I$(MAKE_DIR)/vendor/vulkan
C_FLAGS			+= -Wall -Wextra -Wno-missing-braces -Wno-missing-field-initializers 
C_FLAGS 		+= -Wno-nullability-completeness
C_FLAGS			+= -std=c++23
C_FLAGS			+= -g
C_FLAGS			+= -MMD -MP

MINGW_PREFIX	?= x86_64-w64-mingw32-
TARGET_OS		?= linux

CC				:= clang++

EXE_EXT			:=

LINKS			:= -lglfw
LINKS			+= -lvulkan
LINKS			+= -lm

DEBUG_FLAGS 	:= -DDEBUG
DEBUG_FLAGS 	+= -fsanitize=address -fsanitize=undefined
DEBUG_FLAGS 	+= -O0

ifeq ($(TARGET_OS), windows)
	CC				:= $(MINGW_PREFIX)g++

	RELEASE_DIR		:= $(MAKE_DIR)/winbin/release
	PROFILING_DIR	:= $(MAKE_DIR)/winbin/profiling
	DEBUG_DIR		:= $(MAKE_DIR)/winbin/debug

	EXE_EXT			:= .exe

	MINGW_LIB_DIR	:= $(MAKE_DIR)/vendor/mingw-libs
	C_FLAGS			+= -I$(MINGW_LIB_DIR)/include
	C_FLAGS			+= -L$(MINGW_LIB_DIR)/lib

	LINKS			:= -lglfw3
	LINKS			+= -lvulkan-1
	LINKS			+= -lgdi32 -luser32 -lkernel32

	DEBUG_FLAGS 	:= -DDEBUG
	DEBUG_FLAGS 	+= -O0
endif

BEAR			:= bear
SHADERC			:= glslangValidator

DEBUG_FLAGS 	:= -DDEBUG
DEBUG_FLAGS 	+= -fsanitize=address -fsanitize=undefined
DEBUG_FLAGS 	+= -O0

RELEASE_FLAGS 	:= -DRELEASE
RELEASE_FLAGS   += -O3

PROFILING_FLAGS := $(RELEASE_FLAGS) 
PROFILING_FLAGS += -DPROFILING

export MAKE_DIR RELEASE_DIR DEBUG_DIR PROFILING_DIR CC EXE_EXT SHADERC LINKS C_FLAGS DEBUG_FLAGS RELEASE_FLAGS PROFILING_FLAGS

.PHONY: shaders
shaders:
	@echo COMPILING SHADERS 
	@$(MAKE) -C src/Engine/Resource/Shader 

.PHONY: debug 
debug: shaders
	@echo "COMPILING (DEBUG)"
	@$(MAKE) -C src BUILD_TYPE=debug debug

.PHONY: release 
release: shaders
	@echo "COMPILING (release)"
	@$(MAKE) -C src BUILD_TYPE=release release

.PHONY: profiling 
profiling: shaders
	@echo "COMPILING (profiling)"
	@$(MAKE) -C src BUILD_TYPE=profiling profiling

.PHONY: compiledb
compiledb: shaders
	@echo "COMPILING (compiledb)"
	@$(BEAR) --output $(MAKE_DIR)/compile_commands.json -- $(MAKE) -C src BUILD_TYPE=debug debug

.PHONY: clean 
clean:
	@echo CLEANING 
	@$(MAKE) -C src clean 

.PHONY: info 
info:
	@$(MAKE) -C src info
