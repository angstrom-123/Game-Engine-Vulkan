VERT_SOURCES 	:= $(wildcard *.vert)
FRAG_SOURCES 	:= $(wildcard *.frag)
COMP_SOURCES	:= $(wildcard *.comp)
VERT_OUTPUTS 	:= $(patsubst %.vert, %.vert.spirv, $(VERT_SOURCES))
FRAG_OUTPUTS 	:= $(patsubst %.frag, %.frag.spirv, $(FRAG_SOURCES))
COMP_OUTPUTS 	:= $(patsubst %.comp, %.comp.spirv, $(COMP_SOURCES))

$(VERT_OUTPUTS) $(FRAG_OUTPUTS): $(VERT_SOURCES) $(FRAG_SOURCES) $(COMP_SOURCES)
	@for src in $^; do\
		$(SHADERC) --target-env vulkan1.1 $$src -o $$src.spirv;\
	done

all: $(VERT_OUTPUTS) $(FRAG_OUTPUTS) $(COMP_SOURCES);
