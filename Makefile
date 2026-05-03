MAKE_DIR		:= $(PWD)

RELEASE_DIR		:= $(MAKE_DIR)/bin/release
PROFILING_DIR	:= $(MAKE_DIR)/bin/profiling
DEBUG_DIR		:= $(MAKE_DIR)/bin/debug

CC				:= clang++
BEAR			:= bear
SHADERC			:= glslangValidator

LINKS			:= -lglfw
LINKS			+= -lvulkan
LINKS			+= -lm

C_FLAGS			:= -I$(MAKE_DIR)/src
C_FLAGS			+= -I$(MAKE_DIR)/src/Engine
C_FLAGS			+= -I$(MAKE_DIR)/src/Application
C_FLAGS			+= -I$(MAKE_DIR)/vendor
C_FLAGS			+= -I$(MAKE_DIR)/vendor/vulkan
C_FLAGS			+= -Wall -Wextra -Wno-missing-braces -Wno-missing-field-initializers 
C_FLAGS 		+= -Wno-nullability-completeness
C_FLAGS			+= -std=c++23
C_FLAGS			+= -g

DEBUG_FLAGS 	:= -DDEBUG
DEBUG_FLAGS 	+= -fsanitize=address -fsanitize=undefined
DEBUG_FLAGS 	+= -O0

RELEASE_FLAGS 	:= -DRELEASE
RELEASE_FLAGS   += -O3

PROFILING_FLAGS := $(RELEASE_FLAGS) 
PROFILING_FLAGS += -DPROFILING

export MAKE_DIR RELEASE_DIR DEBUG_DIR PROFILING_DIR CC SHADERC LINKS C_FLAGS DEBUG_FLAGS RELEASE_FLAGS PROFILING_FLAGS

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
