#pragma once
#include <vector>

#include <GarrysMod/Symbol.hpp>
#include <scanning/symbolfinder.hpp>
#include <GarrysMod/FactoryLoader.hpp>
#include <dynohook/detour.h>

#include "Platform.hpp"

class CBaseEntity;

namespace NativeCall
{
	namespace Util
	{
		extern SymbolFinder symbol_finder;

		int GenerateSig(const std::string& pszSig, std::string& str);
		int GenerateSigFromIDASeq(const std::string& ida_sig, std::string& str);
	}

	namespace FunctionPointers
	{
		typedef CBaseEntity* (*LUA_GetEntity_t)(int index);
		LUA_GetEntity_t LUA_GetEntity();

		typedef void (*LUA_PushEntity_t)(CBaseEntity* pEnt);
		LUA_PushEntity_t LUA_PushEntity();
	}

	namespace Symbols
	{
		extern const Symbol LUA_GetEntity;
		extern const Symbol LUA_PushEntity;
	}

	struct SymbolRT : public Symbol
	{
	public:

		const Type type = Type::None;
		const std::string name;
		const size_t length = 0;

		inline SymbolRT() = default;

		static inline Symbol FromSignature(const char* signature, size_t size)
		{
			return SymbolRT(Type::Signature, std::string(signature, size - 1));
		}
	private:
		inline SymbolRT(Type _type, std::string&& _name) :
			type(_type), name(std::move(_name)), length(type == Type::Signature ? name.size() : 0) { }
	};
}