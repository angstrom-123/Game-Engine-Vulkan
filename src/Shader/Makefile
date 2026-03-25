VERT_SOURCES 	:= $(wildcard $(SRC_DIR)/Shader/*.vert)
FRAG_SOURCES 	:= $(wildcard $(SRC_DIR)/Shader/*.frag)
VERT_OUTPUTS 	:= $(patsubst %.vert, %.vert.spirv, $(VERT_SOURCES))
FRAG_OUTPUTS 	:= $(patsubst %.frag, %.frag.spirv, $(FRAG_SOURCES))

SHADERC			:= glslangValidator

all: $(VERT_SOURCES) $(FRAG_SOURCES)
	@for src in $^; do\
		$(SHADERC) --target-env vulkan1.1 $$src -o $$src.spirv;\
	done

.PHONY: clean
clean: $(VERT_OUTPUTS) $(FRAG_OUTPUTS)
	@rm $^
