#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct lua_State lua_State;
typedef int (*LuaFunction)(lua_State*);
static void (*push_number)(lua_State*, double);

static int clock_value(lua_State* state) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t)) abort();
  push_number(state, (double)t.tv_sec + (double)t.tv_nsec / 1e9);
  return 1;
}

/* Lua 5.1's public ABI, loaded dynamically so a runtime package is sufficient. */
int main(int argc, char** argv) {
  if (argc != 3) {
    fputs("usage: bench_lua LUAJIT_LIBRARY SCRIPT\n", stderr);
    return 2;
  }
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    fprintf(stderr, "%s\n", dlerror());
    return 1;
  }
  lua_State* (*luaL_newstate)(void);
  void (*luaL_openlibs)(lua_State*);
  int (*luaL_loadfile)(lua_State*, const char*);
  int (*lua_pcall)(lua_State*, int, int, int);
  const char* (*lua_tolstring)(lua_State*, int, size_t*);
  void (*lua_close)(lua_State*);
  void (*lua_pushcclosure)(lua_State*, LuaFunction, int);
  void (*lua_setfield)(lua_State*, int, const char*);
#define LOAD(destination, symbol_name)                                                             \
  do {                                                                                             \
    void* symbol = dlsym(library, symbol_name);                                                    \
    if (!symbol) {                                                                                 \
      fprintf(stderr, "%s\n", dlerror());                                                          \
      dlclose(library);                                                                            \
      return 1;                                                                                    \
    }                                                                                              \
    _Static_assert(sizeof(destination) == sizeof(symbol), "POSIX function pointer size");          \
    memcpy(&(destination), &symbol, sizeof(destination));                                          \
  } while (0)
  LOAD(luaL_newstate, "luaL_newstate");
  LOAD(luaL_openlibs, "luaL_openlibs");
  LOAD(luaL_loadfile, "luaL_loadfile");
  LOAD(lua_pcall, "lua_pcall");
  LOAD(lua_tolstring, "lua_tolstring");
  LOAD(lua_close, "lua_close");
  LOAD(lua_pushcclosure, "lua_pushcclosure");
  LOAD(lua_setfield, "lua_setfield");
  LOAD(push_number, "lua_pushnumber");
#undef LOAD
  lua_State* state = luaL_newstate();
  if (!state) {
    dlclose(library);
    return 1;
  }
  luaL_openlibs(state);
  lua_pushcclosure(state, clock_value, 0);
  lua_setfield(state, -10002, "bench_clock");
  int status = luaL_loadfile(state, argv[2]);
  if (!status) status = lua_pcall(state, 0, 0, 0);
  if (status) {
    const char* message = lua_tolstring(state, -1, NULL);
    fprintf(stderr, "%s\n", message ? message : "non-string Lua error");
  }
  lua_close(state);
  dlclose(library);
  return status ? 1 : 0;
}
