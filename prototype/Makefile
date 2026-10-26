CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror
RUNTIME = heap.c reader.c runtime.c compiler.c vm.c builtins.c native.c module.c foreign.c collections.c tables.c reclaim.c modules.c
CPPFLAGS += -DOTIUM_NATIVE_INCLUDE='"$(CURDIR)"'
LDLIBS += -ldl
LDFLAGS += -rdynamic

.PHONY: all
all: otium-proto

otium-proto: $(RUNTIME) main.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) main.c $(LDLIBS) -o $@

bench_runtime: $(RUNTIME) bench_runtime.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h ffi_fixture.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) bench_runtime.c $(LDLIBS) -o $@

.PHONY: differential differential-foreign
differential: otium-proto
	python3 differential.py

differential-foreign: otium-proto ffi_fixture.so
	python3 differential_foreign.py

.PHONY: test test-heap test-reader test-runtime sanitize sanitize-heap sanitize-reader sanitize-runtime clean
test: test-quasiquote test-modules test-namespaces test-defonce test-value test-heap test-reader test-runtime test-scope-roots test-slot-reuse test-foreign test-reload test-foreign-reload

test-heap: test_heap
	./test_heap

test-reader: test_reader
	./test_reader ../lib/otium/core.ot

test-runtime: test_runtime
	./test_runtime

.PHONY: test-foreign sanitize-foreign
test-foreign: test_foreign
	./test_foreign
	./test_foreign --native

ffi_fixture.so: ../tests/ffi/fixture.c
	$(CC) -O2 -g -fPIC -shared -pthread $< -o $@

ffi_other.so: ../tests/ffi/fixture.c
	$(CC) -O2 -g -fPIC -shared -pthread -DFIXTURE_ID=29 $< -o $@

ffi_extra.so: ffi_extra.c
	$(CC) $(CFLAGS) -fPIC -shared $< -o $@

test_foreign: $(RUNTIME) test_foreign.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h ffi_fixture.so ffi_other.so ffi_extra.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_foreign.c $(LDLIBS) -o $@

ffi_fixture_sanitize.so: ../tests/ffi/fixture.c
	$(CC) -O1 -g -fPIC -shared -pthread -fsanitize=address,undefined $< -o $@

ffi_other_sanitize.so: ../tests/ffi/fixture.c
	$(CC) -O1 -g -fPIC -shared -pthread -fsanitize=address,undefined -DFIXTURE_ID=29 $< -o $@

ffi_extra_sanitize.so: ffi_extra.c
	$(CC) -O1 -g -fPIC -shared -fsanitize=address,undefined $< -o $@

test_foreign_sanitize: $(RUNTIME) test_foreign.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h ffi_fixture_sanitize.so ffi_other_sanitize.so ffi_extra_sanitize.so
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_foreign.c $(LDLIBS) -o $@

sanitize-foreign: test_foreign_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_foreign_sanitize --sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_foreign_sanitize --sanitize --native

.PHONY: test-native sanitize-native
test-native: test_runtime test-native-calls test-native-scalar test-native-numeric
	./test_runtime --native
	./test_runtime --tiered

sanitize-native: sanitize-runtime
	OTIUM_NATIVE_SANITIZE=1 ./test_runtime_sanitize --native

test_runtime: $(RUNTIME) test_runtime.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_runtime.c $(LDLIBS) -o $@

test_heap: heap.c heap.h test_heap.c
	$(CC) $(CFLAGS) heap.c test_heap.c -o $@

test_reader: reader.c reader.h test_reader.c
	$(CC) $(CFLAGS) reader.c test_reader.c -o $@

sanitize: sanitize-heap sanitize-reader sanitize-runtime

sanitize-heap:
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined heap.c test_heap.c -o test_heap_sanitize
	./test_heap_sanitize

sanitize-reader:
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined reader.c test_reader.c -o test_reader_sanitize
	./test_reader_sanitize ../lib/otium/core.ot

sanitize-runtime:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_runtime.c $(LDLIBS) -o test_runtime_sanitize
	./test_runtime_sanitize

clean:
	rm -f test_quasiquote test_quasiquote_sanitize
	rm -f test_modules test_modules_sanitize
	rm -f test_namespaces test_namespaces_sanitize
	rm -f test_defonce test_defonce_sanitize
	rm -f test_native_numeric test_native_numeric_sanitize
	rm -f bench_lua
	rm -f test_native_scalar test_native_scalar_sanitize
	rm -f test_value test_value_portable test_value_sanitize test_value_portable_sanitize
	rm -f test_slot_reuse test_slot_reuse_sanitize
	rm -f test_scope_roots test_scope_roots_sanitize
	rm -f test_native_calls test_native_calls_sanitize
	rm -f test_reload test_reload_sanitize test_foreign_reload test_foreign_reload_sanitize
	rm -f otium-proto bench_runtime test_heap test_heap_sanitize test_reader test_reader_sanitize test_reader_clang test_runtime test_runtime_sanitize test_runtime_clang
	rm -f test_foreign test_foreign_sanitize ffi_fixture.so ffi_other.so ffi_extra.so ffi_fixture_sanitize.so ffi_other_sanitize.so ffi_extra_sanitize.so

.PHONY: test-reload sanitize-reload
test_reload: $(RUNTIME) test_reload.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_reload.c $(LDLIBS) -o $@

test-reload: test_reload
	./test_reload
	./test_reload --native

sanitize-reload:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_reload.c $(LDLIBS) -o test_reload_sanitize
	./test_reload_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_reload_sanitize --native

.PHONY: test-foreign-reload sanitize-foreign-reload
test_foreign_reload: $(RUNTIME) test_foreign_reload.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h ffi_fixture.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_foreign_reload.c $(LDLIBS) -o $@

test-foreign-reload: test_foreign_reload
	./test_foreign_reload
	./test_foreign_reload --native

sanitize-foreign-reload: ffi_fixture_sanitize.so
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_foreign_reload.c $(LDLIBS) -o test_foreign_reload_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_foreign_reload_sanitize --sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_foreign_reload_sanitize --sanitize --native

.PHONY: test-native-calls sanitize-native-calls
test_native_calls: $(RUNTIME) test_native_calls.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_native_calls.c $(LDLIBS) -o $@

test-native-calls: test_native_calls
	./test_native_calls

sanitize-native-calls:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_native_calls.c $(LDLIBS) -o test_native_calls_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_native_calls_sanitize

.PHONY: test-scope-roots sanitize-scope-roots
test_scope_roots: $(RUNTIME) test_scope_roots.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_scope_roots.c $(LDLIBS) -o $@

test-scope-roots: test_scope_roots
	./test_scope_roots

sanitize-scope-roots:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_scope_roots.c $(LDLIBS) -o test_scope_roots_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_scope_roots_sanitize

.PHONY: test-slot-reuse sanitize-slot-reuse
test_slot_reuse: $(RUNTIME) test_slot_reuse.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_slot_reuse.c $(LDLIBS) -o $@

test-slot-reuse: test_slot_reuse
	./test_slot_reuse

sanitize-slot-reuse:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_slot_reuse.c $(LDLIBS) -o test_slot_reuse_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_slot_reuse_sanitize

.PHONY: test-value sanitize-value
test_value: test_value.c heap.h
	$(CC) $(CFLAGS) test_value.c -o $@

test_value_portable: test_value.c heap.h
	$(CC) $(CFLAGS) -DOTIUM_PORTABLE_INTS test_value.c -o $@

test-value: test_value test_value_portable
	./test_value
	./test_value_portable

sanitize-value:
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined test_value.c -o test_value_sanitize
	./test_value_sanitize
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DOTIUM_PORTABLE_INTS test_value.c -o test_value_portable_sanitize
	./test_value_portable_sanitize

.PHONY: test-native-scalar sanitize-native-scalar
test_native_scalar: $(RUNTIME) test_native_scalar.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_native_scalar.c $(LDLIBS) -o $@

test-native-scalar: test_native_scalar
	./test_native_scalar

sanitize-native-scalar:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_native_scalar.c $(LDLIBS) -o test_native_scalar_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_native_scalar_sanitize

bench_lua: bench_lua.c
	$(CC) $(CFLAGS) bench_lua.c -ldl -o $@

.PHONY: test-benchmark-lua
test-benchmark-lua: bench_lua
	python3 test_benchmark_lua.py

.PHONY: test-native-numeric sanitize-native-numeric
test_native_numeric: $(RUNTIME) test_native_numeric.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_native_numeric.c $(LDLIBS) -o $@

test-native-numeric: test_native_numeric
	./test_native_numeric

sanitize-native-numeric:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_native_numeric.c $(LDLIBS) -o test_native_numeric_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_native_numeric_sanitize

.PHONY: test-defonce sanitize-defonce
test_defonce: $(RUNTIME) test_defonce.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_defonce.c $(LDLIBS) -o $@

test-defonce: test_defonce
	./test_defonce

sanitize-defonce:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_defonce.c $(LDLIBS) -o test_defonce_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_defonce_sanitize

.PHONY: test-namespaces sanitize-namespaces
test_namespaces: $(RUNTIME) test_namespaces.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_namespaces.c $(LDLIBS) -o $@

test-namespaces: test_namespaces
	./test_namespaces

sanitize-namespaces:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_namespaces.c $(LDLIBS) -o test_namespaces_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_namespaces_sanitize

.PHONY: test-modules sanitize-modules
test_modules: $(RUNTIME) test_modules.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_modules.c $(LDLIBS) -o $@

test-modules: test_modules
	./test_modules

sanitize-modules:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_modules.c $(LDLIBS) -o test_modules_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_modules_sanitize

.PHONY: differential-modules
differential-modules: otium-proto
	python3 differential_modules.py

.PHONY: test-quasiquote sanitize-quasiquote
test_quasiquote: $(RUNTIME) test_quasiquote.c heap.h reader.h runtime.h internal.h foreign.h native_numeric.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_quasiquote.c $(LDLIBS) -o $@

test-quasiquote: test_quasiquote
	./test_quasiquote

sanitize-quasiquote:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_quasiquote.c $(LDLIBS) -o test_quasiquote_sanitize
	OTIUM_NATIVE_SANITIZE=1 ./test_quasiquote_sanitize

.PHONY: differential-quasiquote
differential-quasiquote: otium-proto
	python3 differential_quasiquote.py
