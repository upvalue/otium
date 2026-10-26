CC ?= cc
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror
RUNTIME = heap.c reader.c tokens.c runtime.c compiler.c vm.c builtins.c numeric.c time.c filesystem.c process.c ports.c strings.c bytevectors.c reflection.c support.c conditions.c restarts.c cleanup.c params.c classes.c instances.c records.c foreign.c foreign_types.c collections.c tables.c reclaim.c modules.c
RUNTIME_HEADERS = heap.h reader.h runtime.h internal.h foreign.h foreign_types.h builtins.def
CPPFLAGS += -DOTIUM_LIBRARY_DIR='"$(abspath ../lib)"'
CPPFLAGS += -DOTIUM_BOOTSTRAP_FILE='"$(CURDIR)/primitives.ot"'
CPPFLAGS += $(shell pkg-config --cflags libffi)
LDLIBS += -ldl -lm $(shell pkg-config --libs libffi)

.PHONY: all
all: otium-proto

otium-proto: $(RUNTIME) main.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) main.c $(LDLIBS) -o $@

bench_runtime: $(RUNTIME) bench_runtime.c $(RUNTIME_HEADERS) ffi_fixture.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) bench_runtime.c $(LDLIBS) -o $@

.PHONY: differential differential-foreign
differential: otium-proto
	python3 differential.py

differential-foreign: otium-proto ffi_fixture.so
	python3 differential_foreign.py

.PHONY: test test-heap test-reader test-runtime sanitize sanitize-heap sanitize-reader sanitize-runtime clean
test: test-heap-growth test-tokens test-properties test-records test-methods test-instances test-filesystem test-file-ports test-process test-port-shutdown test-port-reader test-ports test-time test-interrupt test-cleanup test-restarts test-cli test-bytevectors test-handlers test-params test-reflection test-strings test-numeric test-eval test-generics test-conditions test-shared-expander test-macros test-quasiquote test-modules test-namespaces test-defonce test-value test-heap test-reader test-runtime test-scope-roots test-slot-reuse test-foreign test-reload test-foreign-reload

test-heap: test_heap
	./test_heap

test-reader: test_reader
	./test_reader ../lib/otium/core.ot

test-runtime: test_runtime
	./test_runtime

.PHONY: test-foreign sanitize-foreign
test-foreign: test_foreign
	./test_foreign

ffi_fixture.so: ../tests/ffi/fixture.c
	$(CC) -O2 -g -fPIC -shared -pthread $< -o $@

ffi_other.so: ../tests/ffi/fixture.c
	$(CC) -O2 -g -fPIC -shared -pthread -DFIXTURE_ID=29 $< -o $@

ffi_extra.so: ffi_extra.c
	$(CC) $(CFLAGS) -fPIC -shared $< -o $@

test_foreign: $(RUNTIME) test_foreign.c $(RUNTIME_HEADERS) ffi_fixture.so ffi_other.so ffi_extra.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_foreign.c $(LDLIBS) -o $@

ffi_fixture_sanitize.so: ../tests/ffi/fixture.c
	$(CC) -O1 -g -fPIC -shared -pthread -fsanitize=address,undefined $< -o $@

ffi_other_sanitize.so: ../tests/ffi/fixture.c
	$(CC) -O1 -g -fPIC -shared -pthread -fsanitize=address,undefined -DFIXTURE_ID=29 $< -o $@

ffi_extra_sanitize.so: ffi_extra.c
	$(CC) -O1 -g -fPIC -shared -fsanitize=address,undefined $< -o $@

test_foreign_sanitize: $(RUNTIME) test_foreign.c $(RUNTIME_HEADERS) ffi_fixture_sanitize.so ffi_other_sanitize.so ffi_extra_sanitize.so
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_foreign.c $(LDLIBS) -o $@

sanitize-foreign: test_foreign_sanitize
	./test_foreign_sanitize --sanitize

test_runtime: $(RUNTIME) test_runtime.c $(RUNTIME_HEADERS)
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
	rm -f otium-proto-sanitize test_bytevectors test_bytevectors_sanitize
	rm -f test_port_shutdown test_port_shutdown_sanitize
	rm -f test_port_reader test_port_reader_sanitize
	rm -f test_ports test_ports_sanitize
	rm -f test_heap_growth test_heap_growth_sanitize
	rm -f test_tokens test_tokens_sanitize
	rm -f test_properties test_properties_sanitize
	rm -f test_records test_records_sanitize
	rm -f test_methods test_methods_sanitize
	rm -f test_instances test_instances_sanitize
	rm -f test_filesystem test_filesystem_sanitize
	rm -f test_file_ports test_file_ports_sanitize
	rm -f test_process test_process_sanitize
	rm -f test_time test_time_sanitize
	rm -f test_interrupt test_interrupt_sanitize
	rm -f test_cleanup test_cleanup_sanitize
	rm -f test_restarts test_restarts_sanitize
	rm -f test_handlers test_handlers_sanitize
	rm -f test_params test_params_sanitize
	rm -f test_reflection test_reflection_sanitize
	rm -f test_strings test_strings_sanitize
	rm -f test_numeric test_numeric_sanitize
	rm -f test_eval test_eval_sanitize
	rm -f test_generics test_generics_sanitize
	rm -f test_conditions test_conditions_sanitize
	rm -f test_shared_expander test_shared_expander_sanitize
	rm -f test_macros test_macros_sanitize
	rm -f test_quasiquote test_quasiquote_sanitize
	rm -f test_modules test_modules_sanitize
	rm -f test_namespaces test_namespaces_sanitize
	rm -f test_defonce test_defonce_sanitize
	rm -f bench_lua
	rm -f test_value test_value_portable test_value_sanitize test_value_portable_sanitize
	rm -f test_slot_reuse test_slot_reuse_sanitize
	rm -f test_scope_roots test_scope_roots_sanitize
	rm -f test_reload test_reload_sanitize test_foreign_reload test_foreign_reload_sanitize
	rm -f otium-proto bench_runtime test_heap test_heap_sanitize test_reader test_reader_sanitize test_reader_clang test_runtime test_runtime_sanitize test_runtime_clang
	rm -f test_foreign test_foreign_sanitize ffi_fixture.so ffi_other.so ffi_extra.so ffi_fixture_sanitize.so ffi_other_sanitize.so ffi_extra_sanitize.so

.PHONY: test-reload sanitize-reload
test_reload: $(RUNTIME) test_reload.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_reload.c $(LDLIBS) -o $@

test-reload: test_reload
	./test_reload

sanitize-reload:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_reload.c $(LDLIBS) -o test_reload_sanitize
	./test_reload_sanitize

.PHONY: test-foreign-reload sanitize-foreign-reload
test_foreign_reload: $(RUNTIME) test_foreign_reload.c $(RUNTIME_HEADERS) ffi_fixture.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_foreign_reload.c $(LDLIBS) -o $@

test-foreign-reload: test_foreign_reload
	./test_foreign_reload

sanitize-foreign-reload: ffi_fixture_sanitize.so
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_foreign_reload.c $(LDLIBS) -o test_foreign_reload_sanitize
	./test_foreign_reload_sanitize --sanitize

.PHONY: test-scope-roots sanitize-scope-roots
test_scope_roots: $(RUNTIME) test_scope_roots.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_scope_roots.c $(LDLIBS) -o $@

test-scope-roots: test_scope_roots
	./test_scope_roots

sanitize-scope-roots:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_scope_roots.c $(LDLIBS) -o test_scope_roots_sanitize
	./test_scope_roots_sanitize

.PHONY: test-slot-reuse sanitize-slot-reuse
test_slot_reuse: $(RUNTIME) test_slot_reuse.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_slot_reuse.c $(LDLIBS) -o $@

test-slot-reuse: test_slot_reuse
	./test_slot_reuse

sanitize-slot-reuse:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_slot_reuse.c $(LDLIBS) -o test_slot_reuse_sanitize
	./test_slot_reuse_sanitize

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

bench_lua: bench_lua.c
	$(CC) $(CFLAGS) bench_lua.c -ldl -o $@

.PHONY: test-benchmark-lua
test-benchmark-lua: bench_lua
	python3 test_benchmark_lua.py

.PHONY: test-defonce sanitize-defonce
test_defonce: $(RUNTIME) test_defonce.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_defonce.c $(LDLIBS) -o $@

test-defonce: test_defonce
	./test_defonce

sanitize-defonce:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_defonce.c $(LDLIBS) -o test_defonce_sanitize
	./test_defonce_sanitize

.PHONY: test-namespaces sanitize-namespaces
test_namespaces: $(RUNTIME) test_namespaces.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_namespaces.c $(LDLIBS) -o $@

test-namespaces: test_namespaces
	./test_namespaces

sanitize-namespaces:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_namespaces.c $(LDLIBS) -o test_namespaces_sanitize
	./test_namespaces_sanitize

.PHONY: test-modules sanitize-modules
test_modules: $(RUNTIME) test_modules.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_modules.c $(LDLIBS) -o $@

test-modules: test_modules
	./test_modules

sanitize-modules:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_modules.c $(LDLIBS) -o test_modules_sanitize
	./test_modules_sanitize

.PHONY: differential-modules
differential-modules: otium-proto
	python3 differential_modules.py

.PHONY: test-quasiquote sanitize-quasiquote
test_quasiquote: $(RUNTIME) test_quasiquote.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_quasiquote.c $(LDLIBS) -o $@

test-quasiquote: test_quasiquote
	./test_quasiquote

sanitize-quasiquote:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_quasiquote.c $(LDLIBS) -o test_quasiquote_sanitize
	./test_quasiquote_sanitize

.PHONY: differential-quasiquote
differential-quasiquote: otium-proto
	python3 differential_quasiquote.py

.PHONY: test-foreign-no-compiler compliance
test-foreign-no-compiler: test_foreign
	env PATH=/nonexistent OTIUM_NATIVE_CC=/nonexistent ./test_foreign

compliance: otium-proto
	python3 compliance.py

.PHONY: test-no-compiler
test-no-compiler: test_runtime test_foreign otium-proto
	env PATH=/nonexistent OTIUM_NATIVE_CC=/nonexistent ./test_runtime
	env PATH=/nonexistent OTIUM_NATIVE_CC=/nonexistent ./test_foreign

.PHONY: test-macros sanitize-macros
test_macros: $(RUNTIME) test_macros.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_macros.c $(LDLIBS) -o $@

test-macros: test_macros
	./test_macros

sanitize-macros:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_macros.c $(LDLIBS) -o test_macros_sanitize
	./test_macros_sanitize

.PHONY: differential-macros
differential-macros: otium-proto
	python3 differential_macros.py

.PHONY: test-shared-expander sanitize-shared-expander
test_shared_expander: $(RUNTIME) test_shared_expander.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_shared_expander.c $(LDLIBS) -o $@

test-shared-expander: test_shared_expander
	./test_shared_expander

sanitize-shared-expander:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_shared_expander.c $(LDLIBS) -o test_shared_expander_sanitize
	./test_shared_expander_sanitize

.PHONY: test-conditions sanitize-conditions
test_conditions: $(RUNTIME) test_conditions.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_conditions.c $(LDLIBS) -o $@

test-conditions: test_conditions
	./test_conditions

sanitize-conditions:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_conditions.c $(LDLIBS) -o test_conditions_sanitize
	./test_conditions_sanitize

.PHONY: differential-conditions
differential-conditions: otium-proto
	python3 differential_conditions.py

.PHONY: test-generics sanitize-generics
test_generics: $(RUNTIME) test_generics.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_generics.c $(LDLIBS) -o $@

test-generics: test_generics
	./test_generics

sanitize-generics:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_generics.c $(LDLIBS) -o test_generics_sanitize
	./test_generics_sanitize

.PHONY: differential-generics
differential-generics: otium-proto
	python3 differential_generics.py

.PHONY: test-eval sanitize-eval
test_eval: $(RUNTIME) test_eval.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_eval.c $(LDLIBS) -o $@

test-eval: test_eval
	./test_eval

sanitize-eval:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_eval.c $(LDLIBS) -o test_eval_sanitize
	./test_eval_sanitize

.PHONY: differential-eval
differential-eval: otium-proto
	python3 differential_eval.py

.PHONY: test-numeric sanitize-numeric
test_numeric: $(RUNTIME) test_numeric.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_numeric.c $(LDLIBS) -o $@

test-numeric: test_numeric
	./test_numeric

sanitize-numeric:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_numeric.c $(LDLIBS) -o test_numeric_sanitize
	./test_numeric_sanitize

.PHONY: differential-numeric
differential-numeric: otium-proto
	python3 differential_numeric.py

.PHONY: test-strings sanitize-strings
test_strings: $(RUNTIME) test_strings.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_strings.c $(LDLIBS) -o $@

test-strings: test_strings
	./test_strings

sanitize-strings:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_strings.c $(LDLIBS) -o test_strings_sanitize
	./test_strings_sanitize

.PHONY: differential-strings
differential-strings: otium-proto
	python3 differential_strings.py

.PHONY: test-reflection sanitize-reflection
test_reflection: $(RUNTIME) test_reflection.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_reflection.c $(LDLIBS) -o $@

test-reflection: test_reflection
	./test_reflection

sanitize-reflection:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_reflection.c $(LDLIBS) -o test_reflection_sanitize
	./test_reflection_sanitize

.PHONY: differential-reflection
differential-reflection: otium-proto
	python3 differential_reflection.py

.PHONY: test-params sanitize-params
test_params: $(RUNTIME) test_params.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_params.c $(LDLIBS) -o $@

test-params: test_params
	./test_params

sanitize-params:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_params.c $(LDLIBS) -o test_params_sanitize
	./test_params_sanitize

.PHONY: differential-params
differential-params: otium-proto
	python3 differential_params.py

.PHONY: test-handlers sanitize-handlers
test_handlers: $(RUNTIME) test_handlers.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_handlers.c $(LDLIBS) -o $@

test-handlers: test_handlers
	./test_handlers

sanitize-handlers:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_handlers.c $(LDLIBS) -o test_handlers_sanitize
	./test_handlers_sanitize

.PHONY: differential-handlers
differential-handlers: otium-proto
	python3 differential_handlers.py

.PHONY: test-bytevectors sanitize-bytevectors differential-bytevectors
test_bytevectors: $(RUNTIME) test_bytevectors.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_bytevectors.c $(LDLIBS) -o $@

test-bytevectors: test_bytevectors
	./test_bytevectors

sanitize-bytevectors:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_bytevectors.c $(LDLIBS) -o test_bytevectors_sanitize
	./test_bytevectors_sanitize

differential-bytevectors: otium-proto
	python3 differential_bytevectors.py

.PHONY: test-cli
test-cli: otium-proto
	python3 test_cli.py

.PHONY: sanitize-cli
sanitize-cli:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) main.c $(LDLIBS) -o otium-proto-sanitize
	OTIUM_TEST_CLI=./otium-proto-sanitize python3 test_cli.py

.PHONY: test-restarts sanitize-restarts
test_restarts: $(RUNTIME) test_restarts.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_restarts.c $(LDLIBS) -o $@

test-restarts: test_restarts
	./test_restarts

sanitize-restarts:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_restarts.c $(LDLIBS) -o test_restarts_sanitize
	./test_restarts_sanitize

.PHONY: differential-restarts
differential-restarts: otium-proto
	python3 differential_restarts.py

.PHONY: test-cleanup sanitize-cleanup
test_cleanup: $(RUNTIME) test_cleanup.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_cleanup.c $(LDLIBS) -o $@

test-cleanup: test_cleanup
	./test_cleanup

sanitize-cleanup:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_cleanup.c $(LDLIBS) -o test_cleanup_sanitize
	./test_cleanup_sanitize

.PHONY: differential-cleanup
differential-cleanup: otium-proto
	python3 differential_cleanup.py

.PHONY: test-interrupt sanitize-interrupt
test_interrupt: $(RUNTIME) test_interrupt.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_interrupt.c $(LDLIBS) -o $@

test-interrupt: otium-proto test_interrupt
	./test_interrupt
	python3 test_interrupt.py

sanitize-interrupt: sanitize-cli
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_interrupt.c $(LDLIBS) -o test_interrupt_sanitize
	./test_interrupt_sanitize
	OTIUM_TEST_CLI=./otium-proto-sanitize python3 test_interrupt.py

.PHONY: test-time sanitize-time differential-time
test_time: $(RUNTIME) test_time.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_time.c $(LDLIBS) -o $@

test-time: test_time
	./test_time

sanitize-time:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_time.c $(LDLIBS) -o test_time_sanitize
	./test_time_sanitize

differential-time: otium-proto
	python3 differential_time.py

.PHONY: test-ports sanitize-ports
test_ports: $(RUNTIME) test_ports.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_ports.c $(LDLIBS) -o $@

test-ports: test_ports otium-proto
	./test_ports
	python3 test_port_streams.py

sanitize-ports:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_ports.c $(LDLIBS) -o test_ports_sanitize
	./test_ports_sanitize

.PHONY: differential-ports
differential-ports: otium-proto
	python3 differential_ports.py

.PHONY: test-port-reader sanitize-port-reader
test_port_reader: $(RUNTIME) test_port_reader.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_port_reader.c $(LDLIBS) -o $@

test-port-reader: test_port_reader
	./test_port_reader

sanitize-port-reader:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_port_reader.c $(LDLIBS) -o test_port_reader_sanitize
	./test_port_reader_sanitize

.PHONY: test-port-shutdown sanitize-port-shutdown
test_port_shutdown: $(RUNTIME) test_port_shutdown.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_port_shutdown.c $(LDLIBS) -o $@

test-port-shutdown: test_port_shutdown otium-proto
	./test_port_shutdown
	python3 test_port_shutdown.py

sanitize-port-shutdown: sanitize-cli
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_port_shutdown.c $(LDLIBS) -o test_port_shutdown_sanitize
	./test_port_shutdown_sanitize
	OTIUM_TEST_CLI=./otium-proto-sanitize python3 test_port_shutdown.py

.PHONY: test-process sanitize-process
test_process: $(RUNTIME) test_process.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_process.c $(LDLIBS) -o $@

test-process: test_process
	./test_process

sanitize-process:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_process.c $(LDLIBS) -o test_process_sanitize
	./test_process_sanitize

.PHONY: test-file-ports sanitize-file-ports
test_file_ports: $(RUNTIME) test_file_ports.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_file_ports.c $(LDLIBS) -o $@
test-file-ports: test_file_ports
	./test_file_ports
sanitize-file-ports:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_file_ports.c $(LDLIBS) -o test_file_ports_sanitize
	./test_file_ports_sanitize

.PHONY: test-filesystem sanitize-filesystem
test_filesystem: $(RUNTIME) test_filesystem.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_filesystem.c $(LDLIBS) -o $@
test-filesystem: test_filesystem
	./test_filesystem
sanitize-filesystem:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_filesystem.c $(LDLIBS) -o test_filesystem_sanitize
	./test_filesystem_sanitize

.PHONY: test-instances sanitize-instances
test_instances: $(RUNTIME) test_instances.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_instances.c $(LDLIBS) -o $@
test-instances: test_instances
	./test_instances
sanitize-instances:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_instances.c $(LDLIBS) -o test_instances_sanitize
	./test_instances_sanitize

.PHONY: differential-instances
differential-instances: otium-proto
	python3 differential_instances.py

.PHONY: test-methods sanitize-methods
test_methods: $(RUNTIME) test_methods.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_methods.c $(LDLIBS) -o $@
test-methods: test_methods
	./test_methods
sanitize-methods:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_methods.c $(LDLIBS) -o test_methods_sanitize
	./test_methods_sanitize

.PHONY: differential-methods
differential-methods: otium-proto
	python3 differential_methods.py

.PHONY: test-records sanitize-records differential-records
test_records: $(RUNTIME) test_records.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_records.c $(LDLIBS) -o $@
test-records: test_records
	./test_records
sanitize-records:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_records.c $(LDLIBS) -o test_records_sanitize
	./test_records_sanitize
differential-records: otium-proto
	python3 differential_records.py

.PHONY: test-properties sanitize-properties differential-properties
test_properties: $(RUNTIME) test_properties.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_properties.c $(LDLIBS) -o $@
test-properties: test_properties
	./test_properties
sanitize-properties:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_properties.c $(LDLIBS) -o test_properties_sanitize
	./test_properties_sanitize
differential-properties: otium-proto
	python3 differential_properties.py

.PHONY: test-tokens sanitize-tokens
test_tokens: $(RUNTIME) test_tokens.c $(RUNTIME_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(RUNTIME) test_tokens.c $(LDLIBS) -o $@
test-tokens: test_tokens
	./test_tokens
sanitize-tokens:
	$(CC) $(CPPFLAGS) $(LDFLAGS) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $(RUNTIME) test_tokens.c $(LDLIBS) -o test_tokens_sanitize
	./test_tokens_sanitize

.PHONY: differential-tokens
differential-tokens: otium-proto
	python3 differential_tokens.py

.PHONY: test-heap-growth sanitize-heap-growth
test_heap_growth: heap.c test_heap_growth.c heap.h
	$(CC) $(CFLAGS) heap.c test_heap_growth.c -o $@
test-heap-growth: test_heap_growth
	./test_heap_growth
sanitize-heap-growth:
	$(CC) -O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined heap.c test_heap_growth.c -o test_heap_growth_sanitize
	./test_heap_growth_sanitize
