CC = gcc
CFLAGS = -Wall -Wextra -I../firmware/app/include -DPP_TARGET_TEST
TESTS = test_pp_block test_pp_protocol test_pp_graph

all: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t; done

test_pp_block: test_pp_block.c ../firmware/app/src/pp_block.c ../firmware/app/src/pp_block_representation.c ../firmware/app/src/pp_block_pretraitement.c ../firmware/app/src/pp_block_estimation.c ../firmware/app/src/pp_block_detection.c ../firmware/app/src/pp_block_validation.c ../firmware/app/src/pp_block_suivi.c
	$(CC) $(CFLAGS) -o $@ $^

test_pp_protocol: test_pp_protocol.c ../firmware/app/src/pp_protocol.c
	$(CC) $(CFLAGS) -o $@ $^

test_pp_graph: test_pp_graph.c ../firmware/app/src/pp_graph.c ../firmware/app/src/pp_protocol.c ../firmware/app/src/pp_block.c ../firmware/app/src/pp_block_representation.c ../firmware/app/src/pp_block_pretraitement.c ../firmware/app/src/pp_block_estimation.c ../firmware/app/src/pp_block_detection.c ../firmware/app/src/pp_block_validation.c ../firmware/app/src/pp_block_suivi.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TESTS)
