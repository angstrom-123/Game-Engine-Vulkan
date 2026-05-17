ENGINE_SRC	:= $(shell find Engine -name "*.cpp" 2>/dev/null)
APP_SRC		:= $(shell find Application -name "*.cpp" 2>/dev/null)
MAIN_SRC	:= main.cpp

BUILD_TYPE	?= release

ifeq ($(BUILD_TYPE), release)
	FLAGS 		:= $(RELEASE_FLAGS)
	BUILD_DIR	:= $(RELEASE_DIR)
else ifeq ($(BUILD_TYPE), profiling)
	FLAGS 		:= $(PROFILING_FLAGS)
	BUILD_DIR	:= $(PROFILING_DIR)
else
	FLAGS 		:= $(DEBUG_FLAGS)
	BUILD_DIR	:= $(DEBUG_DIR)
endif

FLAGS 		+= $(C_FLAGS)

OBJ_DIR		:= $(BUILD_DIR)/src

ENGINE_OBJ	:= $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(ENGINE_SRC))
APP_OBJ		:= $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(APP_SRC))
MAIN_OBJ	:= $(OBJ_DIR)/main.o

DEP_FILES	:= $(ENGINE_OBJ:.o=.d) $(APP_OBJ:.o=.d) $(MAIN_OBJ:.o-.d)

LIBRARY		:= $(BUILD_DIR)/libengine.a 
TARGET		:= $(BUILD_DIR)/output$(EXE_EXT)

# Engine sources (.o)
$(OBJ_DIR)/Engine/%.o: Engine/%.cpp
	@mkdir -p $(dir $@)
	@$(CC) $(FLAGS) -c $< -o $@

# App sources (.o)
$(OBJ_DIR)/Application/%.o: Application/%.cpp
	@mkdir -p $(dir $@)
	@$(CC) $(FLAGS) -c $< -o $@

# Main source (.o)
$(MAIN_OBJ): $(MAIN_SRC)
	@mkdir -p $(dir $@)
	@$(CC) $(FLAGS) -c $< -o $@

# Engine library (.a)
$(LIBRARY): $(ENGINE_OBJ)
	@echo "BUILDING ENGINE LIBRARY"
	@ar rcs $@ $^


# Final app
ifeq ($(TARGET_OS), windows)
$(TARGET): $(LIBRARY) $(APP_OBJ) $(MAIN_OBJ)
	@echo "LINKING APPLICATION ($(BUILD_TYPE))"
	@$(CC) $(APP_OBJ) $(MAIN_OBJ) $(FLAGS) -L$(BUILD_DIR) -lengine $(LINKS) -o $@
	@cp $(MAKE_DIR)/vendor/windows_dlls/libgcc_s_seh-1.dll $(BUILD_DIR)
	@cp $(MAKE_DIR)/vendor/windows_dlls/libstdc++-6.dll $(BUILD_DIR)
else
$(TARGET): $(LIBRARY) $(APP_OBJ) $(MAIN_OBJ)
	@echo "LINKING APPLICATION ($(BUILD_TYPE))"
	@$(CC) $(APP_OBJ) $(MAIN_OBJ) $(FLAGS) -L$(BUILD_DIR) -lengine $(LINKS) -o $@
endif

-include $(DEP_FILES)

.PHONY: debug 
debug: $(TARGET)

.PHONY: release 
release: $(TARGET)

.PHONY: profiling 
profiling: $(TARGET)

.PHONY: clean 
clean:
	@rm -r $(DEBUG_DIR) || true
	@rm -r $(RELEASE_DIR) || true
	@rm -r $(PROFILING_DIR) || true

.PHONY: info
info:
	@echo ENGINE SOURCES
	@echo $(ENGINE_SRC)
	@echo ENGINE OBJECTS
	@echo $(ENGINE_OBJ)
	@echo APP SOURCES
	@echo $(APP_SRC)
	@echo APP OBJECTS
	@echo $(APP_OBJ)
