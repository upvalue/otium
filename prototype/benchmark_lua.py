"""LuaJIT reference kernels, usable with the installed shared library alone."""
import ctypes.util
import json
import os
import subprocess

LUA_CASES = {
    'ffi1000000': '''local ffi = require("ffi")
    ffi.cdef("int echo_int(int value);")
    local library = ffi.load("prototype/ffi_fixture.so")
    local echo = library.echo_int
    local function kernel(n)
      local total = 0
      while n > 0 do total = total + echo(n); n = n - 1 end
      return total
    end''',
    'float1000000': '''local function kernel(n)
      local total, step = 0.0, 0.5
      while n > 0 do total = total + step; n = n - 1 end
      return total
    end''',
    'sum1000000': '''local function kernel(n)
      local total = 0
      while n > 0 do total = total + n; n = n - 1 end
      return total
    end''',
    'fib30': '''local function kernel(n)
      if n < 2 then return n end
      return kernel(n - 1) + kernel(n - 2)
    end''',
    'optional100000': '''local function sum_optional(a, b, c)
      if b == nil then b = 1 end
      if c == nil then c = 2 end
      return a + b + c
    end
    local function kernel(n)
      local total = 0
      while n > 0 do total = total + sum_optional(n); n = n - 1 end
      return total
    end''',
    'rest100000': '''local function sum_rest(a, ...)
      local rest = {...}
      return a + rest[1] + rest[2]
    end
    local function kernel(n)
      local total = 0
      while n > 0 do total = total + sum_rest(n, 1, 2); n = n - 1 end
      return total
    end''',
    'apply100000': '''local function sum_three(a, b, c) return a + b + c end
    local function kernel(n)
      local total, tail = 0, {1, 2}
      while n > 0 do total = total + sum_three(n, unpack(tail)); n = n - 1 end
      return total
    end''',
    'pairs50000': '''local function kernel(n)
      local total = 0
      while n > 0 do
        local p = {n, {n - 1, false}}
        total = total + p[1] + p[2][1]
        n = n - 1
      end
      return total
    end''',
    'arrays50000': '''local function kernel(n)
      local a, i, total = {}, 0, 0
      while i < n do a[i + 1] = i; i = i + 1 end
      i = 0
      while i < n do total = total + a[i + 1]; i = i + 1 end
      return total
    end''',
    'objectkeys5000': '''local function kernel(n)
      local keys, t, i, total = {}, {}, 0, 0
      while i < n do
        local key = {}
        keys[i + 1] = key
        t[key] = i * 2
        i = i + 1
      end
      i = 0
      while i < n do total = total + t[keys[i + 1]]; i = i + 1 end
      return total
    end''',
    'tables10000': '''local function kernel(n)
      local t, i, total = {}, 0, 0
      while i < n do t[i] = i * 2; i = i + 1 end
      i = 0
      while i < n do total = total + t[i]; i = i + 1 end
      return total
    end''',
}


def available_library():
    return os.environ.get('OTIUM_LUAJIT_LIBRARY') or ctypes.util.find_library('luajit-5.1')


def run_lua(root, directory, library, workload, argument, expected, enabled):
    runtime = 'luajit-jit' if enabled else 'luajit-interpreter'
    script = directory / 'kernel.lua'
    script.write_text(f'''local jit = require("jit")
jit.flush()
jit.{'on' if enabled else 'off'}()
assert(jit.status() == {str(enabled).lower()})
local starts, stops = 0, 0
local function trace_event(event)
  if event == "start" then starts = starts + 1 end
  if event == "stop" then stops = stops + 1 end
end
jit.off(trace_event)
jit.attach(trace_event, "trace")
{LUA_CASES[workload]}
local function measure()
  local samples, iterations, discarded = {{}}, 0, 0
  while #samples < 5 do
    iterations = iterations + 1
    assert(iterations <= 100, "JIT did not stabilize within 100 calls")
    local before = starts
    local begin = bench_clock()
    local answer = kernel({argument})
    local elapsed = bench_clock() - begin
    assert(answer == {expected}, "wrong benchmark result")
    if starts ~= before then
      discarded = discarded + #samples
      samples = {{}}
    elseif iterations > 2 then
      samples[#samples + 1] = elapsed
    end
  end
  io.write('{{"workload":{json.dumps(workload)},"runtime":{json.dumps(runtime)},"samples":[')
  for i = 1, #samples do
    if i > 1 then io.write(",") end
    io.write(string.format("%.9f", samples[i]))
  end
  io.write(string.format('],"warmups":%d,"discarded_samples":%d,"trace_starts":%d,"trace_stops":%d,',
    iterations - 5, discarded, starts, stops))
  io.write(string.format('"jit_version":%q,"jit_arch":%q,"jit_os":%q,"jit_enabled":%s}}\\n',
    jit.version, jit.arch, jit.os, tostring(jit.status())))
end
jit.off(measure)
measure()
''')
    result = subprocess.run([str(root / 'prototype/bench_lua'), library, str(script)],
                            cwd=root, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError((workload, runtime, result.stderr))
    measured = json.loads(result.stdout)
    assert len(measured['samples']) == 5
    return measured
