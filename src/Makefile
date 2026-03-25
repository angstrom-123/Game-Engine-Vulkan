SOURCES := $(wildcard $(VEN_DIR)/VkBootstrap/*.cpp)
SOURCES += $(wildcard $(SRC_DIR)/Renderer/*.cpp)
SOURCES += $(MAIN_FILE)

.PHONY: release
release: $(SOURCES)
	@$(CC) $(C_FLAGS) $(RELEASE_FLAGS) $(LINK_FLAGS) $^ -o $(OUTPUT)

.PHONY: debug
debug: $(SOURCES)
	@$(CC) $(C_FLAGS) $(DEBUG_FLAGS) $(LINK_FLAGS) $^ -o $(OUTPUT)

.PHONY: compiledb
compiledb: $(SOURCES)
	@rm -rf $(BEAR_OUTPUT)
	@$(BEAR) --output $(BEAR_OUTPUT) -- $(CC) $(C_FLAGS) $(DEBUG_FLAGS) $(LINK_FLAGS) $^ -o $(OUTPUT)

.PHONY: clean
clean:
	@rm $(OUTPUT)
