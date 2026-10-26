#!/usr/bin/env python3
"""Apply the ot_/oti_ naming convention to prototype C sources.

Public embedding names use ot_ and internal linker-visible or shared names use
oti_. The rename is idempotent, so it can be reapplied to branches written
against the older names. String and character literals are never changed;
comments only have names containing an underscore changed, so prose such as
"Value" or "Frame" is left alone.

Usage: tools/rename.py FILE...
"""
import re
import sys

PUBLIC = {
    'Runtime': 'ot_runtime',
    'Value': 'ot_value',
    'RuntimeIOHost': 'ot_io_host',
    'RuntimeForeignStats': 'ot_foreign_stats',
    'V_NIL': 'OT_NIL',
    'V_FALSE': 'OT_FALSE',
    'V_TRUE': 'OT_TRUE',
    'V_EMPTY': 'OT_EMPTY',
    'V_EOF': 'OT_EOF',
    'RUNTIME_IO_READ': 'OT_IO_READ',
    'RUNTIME_IO_WRITE': 'OT_IO_WRITE',
    'RUNTIME_IO_CREATE': 'OT_IO_CREATE',
    'RUNTIME_IO_TRUNCATE': 'OT_IO_TRUNCATE',
    'RUNTIME_IO_APPEND': 'OT_IO_APPEND',
    'RUNTIME_IO_EXCLUSIVE': 'OT_IO_EXCLUSIVE',
    'value_int': 'ot_int',
    'value_is_int': 'ot_is_int',
    'value_to_int': 'ot_to_int',
    'value_int_signed': 'ot_int_bits',
    'runtime_create': 'ot_create',
    'runtime_destroy': 'ot_destroy',
    'runtime_set_io_host': 'ot_set_io_host',
    'runtime_heap_limit': 'ot_set_heap_limit',
    'runtime_heap_capacity': 'ot_heap_capacity',
    'runtime_set_arguments': 'ot_set_arguments',
    'runtime_flush_ports': 'ot_flush_ports',
    'runtime_interrupt_flag': 'ot_set_interrupt_flag',
    'runtime_poll_interrupt': 'ot_poll_interrupt',
    'runtime_interrupted': 'ot_interrupted',
    'runtime_load_expander': 'ot_load_expander',
    'runtime_load_core': 'ot_load_core',
    'runtime_load_path': 'ot_add_load_path',
    'runtime_eval': 'ot_eval',
    'runtime_call': 'ot_call',
    'runtime_exit_status': 'ot_exit_status',
    'runtime_error': 'ot_error',
    'runtime_error_offset': 'ot_error_offset',
    'runtime_collections': 'ot_collections',
    'runtime_max_frames': 'ot_max_frames',
    'runtime_gc_stress': 'ot_gc_stress',
    'runtime_foreign_stats': 'ot_get_foreign_stats',
    'runtime_foreign_signatures': 'ot_foreign_signatures',
    'runtime_write': 'ot_write',
    'runtime_print': 'ot_print',
}

INTERNAL = {
    # Shared types.
    'Heap': 'oti_heap',
    'Roots': 'oti_roots',
    'HeapObject': 'oti_object_header',
    'Function': 'oti_function',
    'Frame': 'oti_frame',
    'TryFrame': 'oti_try_frame',
    'RestartScope': 'oti_restart_scope',
    'Cleanup': 'oti_cleanup',
    'Documentation': 'oti_documentation',
    'ForeignState': 'oti_foreign_state',
    'ModuleState': 'oti_module_state',
    'BufferInfo': 'oti_buffer_info',
    'ClassInfo': 'oti_class_info',
    'ArrayInfo': 'oti_array_info',
    'TableInfo': 'oti_table_info',
    'Instruction': 'oti_instruction',
    'Capture': 'oti_capture',
    'Opcode': 'oti_opcode',
    'Ast': 'oti_ast',
    'AstKind': 'oti_ast_kind',
    'Reader': 'oti_reader',
    'ArenaBlock': 'oti_arena_block',
    'Scalar': 'oti_scalar',
    'AbiNode': 'oti_abi_node',
    'Signature': 'oti_signature',
    'ForeignSlot': 'oti_foreign_slot',
    'ForeignCallback': 'oti_foreign_callback',
    # File-local types. Prefixed so they cannot collide with variables or with
    # each other when the sources are concatenated into one file.
    'Compiler': 'oti_compiler',
    'Lifetime': 'oti_lifetime',
    'Reference': 'oti_reference',
    'Slot': 'oti_slot',
    'SlotPool': 'oti_slot_pool',
    'QQKind': 'oti_qq_kind',
    'QQNode': 'oti_qq_node',
    'QQPlan': 'oti_qq_plan',
    'Piece': 'oti_qq_piece',
    'Pending': 'oti_qq_pending',
    'Task': 'oti_reclaim_task',
    'Mark': 'oti_reclaim_mark',
    'Module': 'oti_module',
    'Import': 'oti_import',
    'MethodInfo': 'oti_method_info',
    'Comparison': 'oti_comparison',
    'Port': 'oti_port',
    'Pointer': 'oti_pointer',
    'ForeignError': 'oti_foreign_error',
    'Library': 'oti_library',
    'Callback': 'oti_callback',
    'Memory': 'oti_memory',
    # Linker-visible functions and data outside the old rt_ prefix.
    'heap_init': 'oti_heap_init',
    'heap_destroy': 'oti_heap_destroy',
    'heap_set_limit': 'oti_heap_set_limit',
    'heap_collect': 'oti_heap_collect',
    'heap_alloc': 'oti_heap_alloc',
    'roots_push': 'oti_roots_push',
    'roots_pop': 'oti_roots_pop',
    'reader_init': 'oti_reader_init',
    'reader_destroy': 'oti_reader_destroy',
    'reader_allocate': 'oti_reader_allocate',
    'reader_next': 'oti_reader_next',
    'reader_one': 'oti_reader_one',
    'ast_symbol': 'oti_ast_symbol',
    'foreign_scalars': 'oti_foreign_scalars',
    'foreign_scalar': 'oti_foreign_scalar',
    'foreign_scalar_code': 'oti_foreign_scalar_code',
    'foreign_list_length': 'oti_foreign_list_length',
    'foreign_free_abi_nodes': 'oti_foreign_free_abi_nodes',
    'foreign_signature': 'oti_foreign_signature',
}

# Names that mean different things in different files.
PER_FILE = {
    'compiler.c': {'Binding': 'oti_local'},
    'foreign.c': {'Binding': 'oti_foreign_binding'},
    'heap.c': {'Object': 'oti_object_header'},
}

TOKEN = re.compile(r'''
    (?P<line_comment>//[^\n]*)
  | (?P<block_comment>/\*.*?\*/)
  | (?P<string>"(?:\\.|[^"\\\n])*")
  | (?P<char>'(?:\\.|[^'\\\n])*')
  | (?P<identifier>[A-Za-z_][A-Za-z0-9_]*)
''', re.S | re.X)

IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


def rename_identifier(name, names):
    if name in names:
        return names[name]
    if name.startswith('rt_'):
        return 'oti_' + name[3:]
    return name


def rename_comment(text, names):
    return IDENTIFIER.sub(
        lambda m: rename_identifier(m.group(), names) if '_' in m.group() else m.group(), text)


def rename(source, names):
    def replace(match):
        kind = match.lastgroup
        text = match.group()
        if kind == 'identifier':
            return rename_identifier(text, names)
        if kind in ('line_comment', 'block_comment'):
            return rename_comment(text, names)
        return text
    return TOKEN.sub(replace, source)


def main():
    for path in sys.argv[1:]:
        names = dict(PUBLIC, **INTERNAL)
        names.update(PER_FILE.get(path.rsplit('/', 1)[-1], {}))
        with open(path, encoding='utf-8') as file:
            source = file.read()
        renamed = rename(source, names)
        if renamed != source:
            with open(path, 'w', encoding='utf-8') as file:
                file.write(renamed)


if __name__ == '__main__':
    main()
