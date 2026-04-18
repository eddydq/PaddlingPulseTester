EMCC = emcc
CFLAGS = -O2 -I../../firmware/app/include -DPP_TARGET_WASM
LDFLAGS = -s EXPORTED_FUNCTIONS='["_pp_wasm_catalog_json","_pp_wasm_run_graph_json","_pp_wasm_set_source_data","_malloc","_free"]' \
          -s EXPORTED_RUNTIME_METHODS='["UTF8ToString","stringToUTF8","lengthBytesUTF8"]'

BLOCK_SRCS = \
	../../firmware/app/src/pp_block.c \
	../../firmware/app/src/pp_block_source.c \
	../../firmware/app/src/pp_block_representation.c \
	../../firmware/app/src/pp_block_pretraitement.c \
	../../firmware/app/src/pp_block_estimation.c \
	../../firmware/app/src/pp_block_detection.c \
	../../firmware/app/src/pp_block_validation.c \
	../../firmware/app/src/pp_block_suivi.c

CORE_SRCS = \
	../../firmware/app/src/pp_protocol.c \
	../../firmware/app/src/pp_graph.c

WASM_SRCS = pp_wasm_entry.c pp_wasm_source_stub.c

all: flow-runtime.wasm

flow-runtime.wasm: $(WASM_SRCS) $(BLOCK_SRCS) $(CORE_SRCS)
	$(EMCC) $(CFLAGS) $(LDFLAGS) -o flow-runtime.js $^

clean:
	rm -f flow-runtime.js flow-runtime.wasm
