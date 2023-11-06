#include <GarrysMod/InterfacePointers.hpp>
#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Symbol.hpp>

#include "nativecall.h"

#include <string_view>
namespace global {
static constexpr std::string_view Version = "garrysmod_nativecalls 1.0.00";
// version num follows LuaJIT style, xxyyzz
static constexpr uint32_t VersionNum = 100000;

static IServer *server = nullptr;

static void PreInitialize(GarrysMod::Lua::ILuaBase *LUA) {
  server = InterfacePointers::Server();
}

static void Initialize(GarrysMod::Lua::ILuaBase *LUA) {

	NativeCall::Initialize(LUA);
}

static void Deinitialize(GarrysMod::Lua::ILuaBase *LUA) {

}
} // namespace global

GMOD_MODULE_OPEN() {
  global::PreInitialize(LUA);
  global::Initialize(LUA);
  return 1;
}

GMOD_MODULE_CLOSE() {
  global::Deinitialize(LUA);
  return 0;
}