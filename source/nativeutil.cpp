#include "nativeutil.h"
#include <regex>
#include <sstream>


namespace NativeCall
{
	namespace Util
	{
		SymbolFinder symbol_finder;

		template<class T>
		T ResolveSymbol(
			SourceSDK::FactoryLoader& loader, const Symbol& symbol,
			const void* starting_point = nullptr)
		{
			if (symbol.type == Symbol::Type::None)
				return nullptr;

			return reinterpret_cast<T>(symbol_finder.Resolve(
				loader.GetModule(), symbol.name.c_str(), symbol.length, starting_point
			));
		}

		std::vector<std::string> Explode(std::string const& s, char delim)
		{
			std::vector<std::string> result;
			std::istringstream iss(s);

			for (std::string token; std::getline(iss, token, delim); )
			{
				result.push_back(std::move(token));
			}

			return result;
		}

		int GenerateSig(const std::string& pszSig, std::string& str)
		{
			auto sig = Explode(pszSig, '\\');
			sig.erase(sig.begin());

			str.clear();

			for (int i = 0; i < sig.size(); i++)
			{
				sig[i] = "0" + sig[i];
				str[i] = (char)std::stoi(sig[i], nullptr, 16);
			}

			return sig.size();
		}

		int GenerateSigFromIDASeq(const std::string& ida_sig, std::string& str)
		{
			std::regex r(R"([\w\?]+)");

			std::string _byte;
			int iIndex = 0;
			for (std::sregex_iterator i = std::sregex_iterator(ida_sig.begin(), ida_sig.end(), r);
				i != std::sregex_iterator();
				++i)
			{
				_byte = (*i).str();

				if (_byte == "??")
				{
					str[iIndex] = '\x2A';
				}
				else
				{
					str[iIndex] = (char)std::stoi("0x" + _byte, nullptr, 16);
				}

				iIndex++;
			}

			return iIndex + 1;
		}
	}
}

namespace NativeCall
{
	namespace FunctionPointers
	{
		LUA_GetEntity_t LUA_GetEntity()
		{
			static LUA_GetEntity_t func_pointer = nullptr;
			if (func_pointer == nullptr)
			{
				SourceSDK::FactoryLoader server_loader("server");
				func_pointer = Util::ResolveSymbol<LUA_GetEntity_t>(
					server_loader, Symbols::LUA_GetEntity
				);
			}

			return func_pointer;
		}

		LUA_PushEntity_t LUA_PushEntity()
		{
			static LUA_PushEntity_t func_pointer = nullptr;
			if (func_pointer == nullptr)
			{
				SourceSDK::FactoryLoader server_loader("server");
				func_pointer = Util::ResolveSymbol<LUA_PushEntity_t>(
					server_loader, Symbols::LUA_PushEntity
				);
			}

			return func_pointer;
		}
	}

	namespace Symbols
	{
#if defined SYSTEM_WINDOWS
#if defined ARCHITECTURE_X86_64
		const Symbol LUA_GetEntity = Symbol::FromSignature("\x40\x53\x48\x83\xEC\x20\x48\x8B\xD1\x48\x8B\x49\x78\x48\x8B\x01\xFF\x90\x90\x01\x00\x00\x48\x8B\x0D\xC3\x1A\xBC\x00"); // REF: "Tried to use a NULL entity!"
		const Symbol LUA_PushEntity = Symbol::FromSignature("\x40\x53\x48\x83\xEC\x40\x48\x8B\x05\x2A\x2A\x2A\x2A\x48\x8D"); // REF: "Global 'NULL' is not an entity! It has been replaced somehow\n"
#elif defined ARCHITECTURE_X86
		const Symbol LUA_GetEntity = Symbol::FromSignature("\x55\x8B\xEC\x8B\x0D\x2A\x2A\x2A\x2A\x53\x56\x8B\x75\x08\x8B");
		const Symbol LUA_PushEntity = Symbol::FromSignature("\x55\x8B\xEC\x83\xEC\x14\x83\x3D\x2A\x2A\x2A\x2A\x2A\x74\x64");
#endif
#elif defined SYSTEM_LINUX
#if defined ARCHITECTURE_X86_64
		const Symbol LUA_GetEntity = Symbol::FromSignature("\x55\x48\x89\xE5\x41\x55\x41\x89\xF5\x41\x54\x41\x89\xFC");
		const Symbol LUA_PushEntity = Symbol::FromSignature("\xE8\x2A\x2A\x2A\x2A\x48\x85\xDB\x74\x52");
#elif defined ARCHITECTURE_X86
		const Symbol LUA_GetEntity = Symbol::FromSignature("\x55\x89\xE5\x56\x53\x83\xEC\x2A\xA1\x2A\x2A\x2A\x2A\x8B\x5D\x2A\x8B\x75\x2A\x8B\x10\x89\x04\x24\x89\x5C\x24\x2A\xFF\x92");
		const Symbol LUA_PushEntity = Symbol::FromSignature("\xE8\x2A\x2A\x2A\x2A\x85\xDB\x74\x48");
#endif 
#endif
	}
}