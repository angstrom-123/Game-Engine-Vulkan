VERT_SOURCES 	:= $(wildcard *.vert)
FRAG_SOURCES 	:= $(wildcard *.frag)
VERT_OUTPUTS 	:= $(patsubst %.vert, %.vert.spirv, $(VERT_SOURCES))
FRAG_OUTPUTS 	:= $(patsubst %.frag, %.frag.spirv, $(FRAG_SOURCES))

$(VERT_OUTPUTS) $(FRAG_OUTPUTS): $(VERT_SOURCES) $(FRAG_SOURCES)
	@for src in $^; do\
		$(SHADERC) --target-env vulkan1.1 $$src -o $$src.spirv;\
	done

all: $(VERT_OUTPUTS) $(FRAG_OUTPUTS)
