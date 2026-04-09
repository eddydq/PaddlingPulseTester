CC = gcc
CFLAGS = -Wall -Wextra -I../firmware/app/include -DPP_TARGET_TEST
TESTS = test_pp_block test_pp_protocol test_pp_graph

all: $(TESTS)
	@for t in $(TESTS); do echo "--- $$t ---"; ./$$t; done

test_pp_block: test_pp_block.c ../firmware/app/src/pp_block.c ../firmware/app/src/pp_block_representation.c ../firmware/app/src/pp_block_pretraitement.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TESTS)
