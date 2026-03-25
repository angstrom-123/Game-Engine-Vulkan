MAKE_DIR		:= $(PWD)

PLATFORM		:= PLAT_LINUX_WAYLAND
# PLATFORM		:= PLAT_LINUX_X11
# PLATFORM		:= PLAT_WINDOWS

SRC_DIR			:= $(MAKE_DIR)/src
VEN_DIR 		:= $(MAKE_DIR)/vendor
BIN_DIR			:= $(MAKE_DIR)/bin

MAIN_FILE		:= $(SRC_DIR)/main.cpp

OUTPUT			:= $(BIN_DIR)/renderer
BEAR_OUTPUT		:= $(MAKE_DIR)/compile_commands.json

INCLUDE_PATHS	:= 
INCLUDE_PATHS	+= -I$(SRC_DIR)/
INCLUDE_PATHS	+= -I$(SRC_DIR)/Renderer/
INCLUDE_PATHS	+= -I$(VEN_DIR)/VkBootstrap/
INCLUDE_PATHS	+= -I$(VEN_DIR)/vulkan/

LIB_PATHS		:=

CC				= clang++
BEAR			= bear

LINK_FLAGS		:= 
LINK_FLAGS		+= -lglfw
LINK_FLAGS		+= -lvulkan
LINK_FLAGS		+= -lm 

C_FLAGS			:= 
C_FLAGS			+= $(INCLUDE_PATHS) $(LIB_PATHS)
C_FLAGS			+= -Wall -Wextra -Wno-missing-braces -Wno-missing-field-initializers
C_FLAGS			+= -D$(PLATFORM)
C_FLAGS			+= -std=c++23

DEBUG_FLAGS		:=
DEBUG_FLAGS		+= -fsanitize=address 
DEBUG_FLAGS		+= -fsanitize=undefined 
DEBUG_FLAGS		+= -g -O0
DEBUG_FLAGS		+= -DDEBUG

RELEASE_FLAGS	:= 
RELEASE_FLAGS	+= -g -O3
RELEASE_FLAGS	+= -DRELEASE

export MAKE_DIR SRC_DIR VEN_DIR BIN_DIR CC BEAR C_FLAGS LINK_FLAGS MAIN_FILE DEBUG_FLAGS RELEASE_FLAGS OUTPUT BEAR_OUTPUT

.PHONY: release
release:
	@echo COMPILING SHADERS
	@$(MAKE) -C $(MAKE_DIR)/src/Shader
	@echo COMPILING RELEASE
	@$(MAKE) -C $(MAKE_DIR)/src release

.PHONY: debug
debug:
	@echo COMPILING SHADERS
	@$(MAKE) -C $(MAKE_DIR)/src/Shader
	@echo COMPILING DEBUG
	@$(MAKE) -C $(MAKE_DIR)/src debug

.PHONY: compiledb
compiledb:
	@echo COMPILING SHADERS
	@$(MAKE) -C $(MAKE_DIR)/src/Shader
	@echo COMPILING DATABASE
	@$(MAKE) -C $(MAKE_DIR)/src compiledb

.PHONY: shaders 
shaders:
	@echo COMPILING SHADERS
	@$(MAKE) -C $(MAKE_DIR)/src/Shader

.PHONY: clean
clean:
	@echo CLEANING SHADERS
	@$(MAKE) -C $(MAKE_DIR)/src/Shader clean
	@echo CLEANING SOURCE
	@$(MAKE) -C $(MAKE_DIR)/src clean

