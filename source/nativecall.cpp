#include "nativecall.h"
#include "nativeutil.h"

#include <GarrysMod/Symbol.hpp>
#include <GarrysMod/FactoryLoader.hpp>
#include <GarrysMod/Lua/LuaShared.h>
#include <scanning/symbolfinder.hpp>

#ifdef ARCHITECTURE_X86 
#define DYNO_ARCH_X86 32
#elif defined ARCHITECTURE_X86_64
#define DYNO_ARCH_X86 64
#endif

#include <array>
#include <vector>
#include <map>
#include <utility>
#include <string>
#include <functional>
#include <memory>
#include <cstring>
#include <cassert>
#include <sstream>
#include <regex>

#include <dynohook/platform.h>
#include <dynohook/core.h>
#include <dynohook/manager.h>

#ifdef SYSTEM_LINUX
#if defined ARCHITECTURE_X86_64
#include <dynohook/conventions/x64/x64SystemVcall.h>
#elif defined ARCHITECTURE_X86
#include <dynohook/conventions/x86/x86GccCdecl.h>
#include <dynohook/conventions/x86/x86GccThiscall.h>
#endif
#else
#if defined ARCHITECTURE_X86_64
#include <dynohook/conventions/x64/x64MsFastcall.h>
#elif defined ARCHITECTURE_X86
#include <dynohook/conventions/x86/x86MsCdecl.h>
#include <dynohook/conventions/x86/x86MsThiscall.h>
#include <dynohook/conventions/x86/x86MsStdcall.h>
#include <dynohook/conventions/x86/x86MsFastcall.h>
#endif
#endif

using namespace dyno;

enum class DetourRet_e
{
	DETOUR_CONTINUE = 1, // continue the original call
	DETOUR_HANDLE // stop the original call
};

enum class FFIArg
{
	INT = 1,
	DOUBLE,
	STRING,
	ENTITY
};

enum class FFIRet
{
	INT = 1,
	DOUBLE = 2, 
	STRING = 3,
	ENTITY = 4,
	VOID = 5
};

bool NativeCall::CNativeCall::Prep()
{
	if (m_arg_types.size() <= 0)
		return false;

	m_status = ffi_prep_cif(&m_cif, m_callconv, m_arg_types.size(),
		m_rtype, m_arg_types.data());

	if (m_status != FFI_OK)
	{
		return false;
	}

	return true;
}

bool NativeCall::CNativeCall::Call()
{
	if ((m_status != FFI_OK) ||
		(m_callconv == FFI_FIRST_ABI) ||
		(!m_rtype) ||
		(!m_FnTarget))
	{
		return false;
	}

	ffi_call(&m_cif, FFI_FN(m_FnTarget), &m_rc, m_arg_values.data());

	m_arg_types.clear();

	return true;
}

void NativeCall::CNativeCall::ClearArgValues()
{
	for (int i = 0; i < m_arg_values.size(); i++)
	{
		// We DO NOT want to delete pointers that point to memory inside the engine!
		if (m_arg_mytypes[i] != (int)FFIArg::ENTITY)
		{
			if (m_arg_mytypes[i] == (int)FFIArg::STRING)
			{
				free(m_arg_values[i]);
			}
			else
			{
				delete m_arg_values[i];
			}
		}
	}
}

NativeCall::CNativeCall::~CNativeCall()
{
	ClearArgValues();

	if (m_FnClosure)
		ffi_closure_free(m_FnClosure);
}

namespace NativeCall
{
	int32_t metatype = 0;
	const char* metaname = "NativeCall_t";

	GarrysMod::Lua::ILuaBase* lua = nullptr;

	inline CNativeCall* Get(GarrysMod::Lua::ILuaBase* LUA, int index)
	{
		return LUA->GetUserType<CNativeCall>(index, metatype);
	}

	inline void Push(GarrysMod::Lua::ILuaBase* LUA, CNativeCall* pCall)
	{
		LUA->PushUserType(pCall, metatype);
		LUA->PushMetaTable(metatype);
		LUA->SetMetaTable(-2);

		LUA->CreateTable();
		LUA->SetFEnv(-2);
	}

	ReturnAction PreHook(HookType hookType, Hook& hook) {
		CNativeCall* pCall = reinterpret_cast<CNativeCall*>(hook.m_extradata);
		if (!pCall)
			return ReturnAction::Handled;

		int arg_num = pCall->GetArgTypes().size();

		GarrysMod::Lua::ILuaBase* LUA = lua;

		LUA->ReferencePush(pCall->GetDetourFunctionRef());

		int stack_size = LUA->Top();

		if (LUA->GetType(-1) == GarrysMod::Lua::Type::Function)
		{
			for (int i = 0; i < arg_num; i++)
			{
				int iArgType = pCall->GetMyArgTypes()[i];

				switch ((FFIArg)iArgType)
				{
				case FFIArg::INT: // int
				{
					int arg = hook.getArgument<int>(i);

					LUA->PushNumber(arg);
					break;
				}
				case FFIArg::DOUBLE: // double
				{
					double arg = hook.getArgument<double>(i);
					LUA->PushNumber(arg);

					break;
				}
				case FFIArg::STRING: // string
				{
					char* arg = hook.getArgument<char*>(i);
					LUA->PushString(arg);

					break;
				}
				case FFIArg::ENTITY: // entity
				{
					CBaseEntity* arg = hook.getArgument<CBaseEntity*>(i);
					if (auto PushEntity = FunctionPointers::LUA_PushEntity())
					{
						PushEntity(arg);
					}

					break;
				}
				default:
					break;
				}
			}

			LUA->PCall(arg_num, pCall->GetReturnType() && pCall->GetMyReturnType() != (int)FFIRet::VOID ? 1 : 0, 0);

			// max 2 returns
			int num_returns = LUA->Top() - stack_size;
			ReturnAction action = ReturnAction::Handled;

			if (num_returns > 0)
			{
				if (LUA->IsType(num_returns >= 2 ? -2 : -1, GarrysMod::Lua::Type::Number))
				{
					action = (ReturnAction)LUA->GetNumber(-2);
				}

				if (num_returns >= 2 && (action == ReturnAction::Override || action == ReturnAction::Supercede ))
				{
					switch ((FFIRet)pCall->GetMyReturnType())
					{
					case FFIRet::INT:
						if (!LUA->IsType(-1, GarrysMod::Lua::Type::Number))
						{
							LUA->TypeError(-1, "number");
							LUA->Pop(num_returns);

							return ReturnAction::Ignored;
						}

						hook.setReturnValue<int>(LUA->GetNumber(-1));

						break;
					case FFIRet::DOUBLE:
						if (!LUA->IsType(-1, GarrysMod::Lua::Type::Number))
						{
							LUA->TypeError(-1, "number");
							LUA->Pop(num_returns);

							return ReturnAction::Ignored;
						}

						hook.setReturnValue<double>(LUA->GetNumber(-1));

						break;
					case FFIRet::STRING:
						if (!LUA->IsType(-1, GarrysMod::Lua::Type::String))
						{
							LUA->TypeError(-1, "string");
							LUA->Pop(num_returns);

							return ReturnAction::Ignored;
						}

						hook.setReturnValue<const char*>(LUA->GetString(-1));

						break;
					case FFIRet::ENTITY:
						if (!LUA->IsType(-1, GarrysMod::Lua::Type::Entity))
						{
							LUA->TypeError(-1, "entity");
							LUA->Pop(num_returns);

							return ReturnAction::Ignored;
						}

						if (auto fnPtr = FunctionPointers::LUA_GetEntity())
						{
							CBaseEntity* pEnt = fnPtr(-1);
							hook.setReturnValue<CBaseEntity*>(pEnt);
						}

						break;
					default:
						LUA->Pop(num_returns);
						return ReturnAction::Ignored;
					}
				}

				LUA->Pop(num_returns);

				return (ReturnAction)action;
			}
		}

		
		return ReturnAction::Ignored;
	}

	LUA_FUNCTION_STATIC(gc)
	{
		CNativeCall* udata = Get(LUA, 1);
		if (udata == nullptr)
			return 0;

		delete udata;

		LUA->SetUserType(1, nullptr);

		return 0;
	}
	LUA_FUNCTION_STATIC(index)
	{
		LUA->CheckType(1, metatype);

		LUA->PushMetaTable(metatype);
		LUA->Push(2);
		LUA->RawGet(-2);
		if (!LUA->IsType(-1, GarrysMod::Lua::Type::NIL))
			return 1;

		LUA->Pop(2);

		LUA->GetFEnv(1);
		LUA->Push(2);
		LUA->RawGet(-2);

		return 1;
	}
	LUA_FUNCTION_STATIC(newindex)
	{
		LUA->CheckType(1, metatype);

		LUA->GetFEnv(1);
		LUA->Push(2);
		LUA->Push(3);
		LUA->RawSet(-3);
		return 0;
	}

	LUA_FUNCTION_STATIC(SetCallType)
	{
		LUA->CheckType(1, metatype);
		
		CNativeCall* udata = Get(LUA, 1);
		if (udata == nullptr)
			return 0;

		int iCallType = LUA->CheckNumber(2);
		ffi_abi ABIType = (ffi_abi)iCallType;

		switch (ABIType)
		{
		case FFI_SYSV:
			udata->SetCallType(FFI_SYSV);
			break;
		case FFI_STDCALL:
			udata->SetCallType(FFI_STDCALL);
			break;
		case FFI_THISCALL:
			udata->SetCallType(FFI_THISCALL);
			break;
		case FFI_FASTCALL:
			udata->SetCallType(FFI_FASTCALL);
			break;
		case FFI_MS_CDECL:
			udata->SetCallType(FFI_MS_CDECL);
			break;
		case FFI_PASCAL:
			udata->SetCallType(FFI_PASCAL);
			break;
		case FFI_REGISTER:
			udata->SetCallType(FFI_REGISTER);
			break;
		default:
			break;
		}

		return 0;
	}

	LUA_FUNCTION_STATIC(SetReturnType)
	{
		LUA->CheckType(1, metatype);
		CNativeCall* udata = Get(LUA, 1);
		FFIRet iRetType = (FFIRet)LUA->GetNumber(2); 

		switch (iRetType)
		{
		case FFIRet::INT:
			udata->SetReturnType(&ffi_type_sint, (int)FFIRet::INT);
			break;
		case FFIRet::DOUBLE:
			udata->SetReturnType(&ffi_type_double, (int)FFIRet::DOUBLE);
			break;
		case FFIRet::STRING:
			udata->SetReturnType(&ffi_type_pointer, (int)FFIRet::STRING);
			break;
		case FFIRet::ENTITY:
			udata->SetReturnType(&ffi_type_pointer, (int)FFIRet::ENTITY);
			break;
		case FFIRet::VOID:
			udata->SetReturnType(&ffi_type_void, (int)FFIRet::VOID);
			break;
		default:
			break;
		}

		return 0;
	}

	LUA_FUNCTION_STATIC(PushArgument)
	{
		LUA->CheckType(1, metatype);

		CNativeCall* udata = Get(LUA, 1);

		int iArgType = LUA->GetNumber(2); // what type we are using for ffi arg

		switch ((FFIArg)iArgType)
		{
		case FFIArg::INT: // int
		{
			int val = LUA->CheckNumber(3);
			udata->PushArgument(&ffi_type_sint, new int(val), (int)FFIArg::INT);

			break;
		}
		case FFIArg::DOUBLE: // double
		{
			double val = LUA->CheckNumber(3);
			udata->PushArgument(&ffi_type_double, new double(val), (int)FFIArg::DOUBLE);

			break;
		}
		case FFIArg::STRING: // string
		{
			const char* pszString = LUA->CheckString(3);
			char* pszStringCpy = strdup(pszString);

			if (!pszStringCpy)
			{
				LUA->ThrowError("Unable to copy C string!");
			}

			udata->PushArgument(&ffi_type_pointer, pszStringCpy, (int)FFIArg::STRING);

			break;
		}
		case FFIArg::ENTITY: // entity
		{
			LUA->CheckType(3, GarrysMod::Lua::Type::Entity);

			auto fn = FunctionPointers::LUA_GetEntity();

			if (fn)
			{
				CBaseEntity* pEnt = fn(3);
				udata->PushArgument(&ffi_type_pointer, pEnt, (int)FFIArg::ENTITY);
			}
			else
			{
				LUA->ThrowError("Can't call LUA_GetEntity!");
			}

			break;
		}
		default:
			break;
		}

		return 0;
	}

	LUA_FUNCTION_STATIC(PushArgumentType)
	{
		LUA->CheckType(1, metatype);

		CNativeCall* udata = Get(LUA, 1);

		int iArgType = LUA->GetNumber(2); // what type we are using for ffi arg

		switch ((FFIArg)iArgType)
		{
		case FFIArg::INT: // int
		{
			udata->PushArgument(&ffi_type_sint, NULL, (int)FFIArg::INT);

			break;
		}
		case FFIArg::DOUBLE: // double
		{
			udata->PushArgument(&ffi_type_double, NULL, (int)FFIArg::DOUBLE);

			break;
		}
		case FFIArg::STRING: // string
		{
			udata->PushArgument(&ffi_type_pointer, NULL, (int)FFIArg::STRING);

			break;
		}
		case FFIArg::ENTITY: // entity
		{
			udata->PushArgument(&ffi_type_pointer, NULL, (int)FFIArg::ENTITY);

			break;
		}
		default:
			break;
		}

		return 0;
	}

	LUA_FUNCTION_STATIC(ClearArguments)
	{
		LUA->CheckType(1, metatype);

		CNativeCall* udata = Get(LUA, 1);

		return 0;
	}

	LUA_FUNCTION_STATIC(SetSignature)
	{
		LUA->CheckType(1, metatype);

		CNativeCall* udata = Get(LUA, 1);

		std::string signature;
		int iSize;
		if (LUA->IsType(2, GarrysMod::Lua::Type::Table))
		{
			int i = 0;
			LUA->PushNil();
			while (LUA->Next(-2)) {
				LUA->Push(-2);

				int key = LUA->GetNumber(-1);
				int value = LUA->CheckNumber(-2);

				signature[i] = value;

				LUA->Pop(2);

				i++;
			}

			LUA->Pop();

			iSize = i + 1;
		}
		else
		{
			iSize = Util::GenerateSig(LUA->CheckString(2), signature);
		}

		auto symbol = SymbolRT::FromSignature(signature.c_str(), iSize);

		SourceSDK::FactoryLoader server_loader("server");
		const void* starting_point = nullptr;
		auto func_pointer = Util::symbol_finder.Resolve(
			server_loader.GetModule(), symbol.name.c_str(), symbol.length, starting_point);

		if (func_pointer)
		{
			udata->SetFunctionPtr(FFI_FN(func_pointer));
			LUA->PushBool(true);
		}
		else
		{
			LUA->PushBool(false);
		}
		
		return 0;
	}

	LUA_FUNCTION_STATIC(Prep)
	{
		LUA->CheckType(1, metatype);

		CNativeCall* udata = Get(LUA, 1);

		LUA->PushBool(udata->Prep());

		return 1;
	}

	LUA_FUNCTION_STATIC(Call)
	{
		LUA->CheckType(1, metatype);

		CNativeCall* udata = Get(LUA, 1);

		if (udata->GetStatus() != FFI_OK)
		{
			LUA->ThrowError("Cif status not OK...");
		}

		// check if ANY provided args are null
		for (const auto& arg : udata->GetArgValues())
			if (!arg)
				LUA->ThrowError("A provided argument is NULL!");
		
		ffi_call(&udata->GetCif(),
			FFI_FN(udata->GetFunctionPtr()),
			&udata->GetReturnPtr(), 
			udata->GetArgValues().data());

		int iRetType = udata->GetMyReturnType();
		int iReturns = 0; // should be either 0 or 1
		udata->GetReturnType(&iRetType);

		switch ((FFIRet)iRetType)
		{
		case FFIRet::INT:
		{
			int val = static_cast<int>(udata->GetReturnPtr());
			if (val)
			{
				LUA->PushNumber(val);
				iReturns++;
			}
			
			break;
		}
		case FFIRet::DOUBLE:
		{
			double val = static_cast<double>(udata->GetReturnPtr());
			if (val)
			{
				LUA->PushNumber(val);
				iReturns++;
			}

			break;
		}
		case FFIRet::STRING:
		{
			char* val = reinterpret_cast<char*>(udata->GetReturnPtr());
			if (val)
			{
				LUA->PushString(val);
				iReturns++;
			}

			break;
		}
		case FFIRet::ENTITY:
		{
			CBaseEntity* val = reinterpret_cast<CBaseEntity*>(udata->GetReturnPtr());
			if (val)
			{
				if (auto PushEntity = FunctionPointers::LUA_PushEntity())
				{
					PushEntity(val);
					iReturns++;
				}
			}

			break;
		}
		case FFIRet::VOID:
		{
			break;
		}
		default:
			break;
		}

		udata->ClearArgs();

		return iReturns;
	}

	LUA_FUNCTION_STATIC(Detour)
	{
		LUA->CheckType(1, metatype);
		LUA->CheckType(2, GarrysMod::Lua::Type::Function);

		CNativeCall* udata = Get(LUA, 1);

		if (!udata->GetFunctionPtr())
			LUA->ThrowError("Function pointer is NULL!");

		int ref = LUA->ReferenceCreate();
		
		udata->AddGlobalDetourFunctionRef(ref);
		udata->SetDetourFunctionRef(ref);

		HookManager& manager = HookManager::Get();

		ConvFunc convention;
		void* pFunc = udata->GetFunctionPtr();
		dyno::DataType ret;

		std::vector<dyno::DataObject> vecArgs;
		for (auto& v : udata->GetMyArgTypes())
		{
			FFIArg type = (FFIArg)v;

			switch (type)
			{
			case FFIArg::INT:
				vecArgs.push_back(dyno::DataType::Int);
				break;
			case FFIArg::DOUBLE:
				vecArgs.push_back(dyno::DataType::Double);
				break;
			case FFIArg::STRING:
				vecArgs.push_back(dyno::DataType::String);
				break;
			case FFIArg::ENTITY:
				vecArgs.push_back(dyno::DataType::Pointer);
				break;
			default:
				break;
			}
		}

		FFIRet ret_type = (FFIRet)udata->GetMyReturnType();
		switch (ret_type)
		{
		case FFIRet::INT:
			ret = DataType::Int;
			break;
		case FFIRet::DOUBLE:
			ret = DataType::Double;
			break;
		case FFIRet::STRING:
			ret = DataType::String;
			break;
		case FFIRet::ENTITY:
			ret = DataType::Pointer;
			break;
		case FFIRet::VOID:
			ret = DataType::Void;
			break;
		default:
			break;
		}

		ffi_abi calltype = udata->GetCallType();
		switch (calltype)
		{
#if defined(SYSTEM_LINUX) && defined(ARCHITECTURE_X86_64)
		case FFI_SYSV:
			convention = [vecArgs, ret] { return new x64SystemVcall(vecArgs, ret); };
			break;
#endif
#if defined(SYSTEM_WINDOWS) && defined(ARCHITECTURE_X86)
		case FFI_STDCALL:
			convention = [vecArgs, ret] { return new x86MsStdcall(vecArgs, ret); };
			break;
#endif
#ifdef ARCHITECTURE_X86 
		case FFI_THISCALL:
#ifdef SYSTEM_WINDOWS
			convention = [vecArgs, ret] { return new x86MsThiscall(vecArgs, ret); };
#elif defined(SYSTEM_LINUX)
			convention = [vecArgs, ret] { return new x86GccThiscall(vecArgs, ret); };
#endif
			break;
#endif
#ifdef SYSTEM_WINDOWS
		case FFI_FASTCALL:
#ifdef ARCHITECTURE_X86
			convention = [vecArgs, ret] { return new x86MsFastcall(vecArgs, ret); };
#elif defined(ARCHITECTURE_X86_64)
			convention = [vecArgs, ret] { return new x64MsFastcall(vecArgs, ret); };
#endif
			break;
#endif
#ifdef ARCHITECTURE_X86
		case FFI_MS_CDECL: // represents gcc too
#ifdef SYSTEM_WINDOWS
			convention = [vecArgs, ret] { return new x86MsCdecl(vecArgs, ret); };
#elif defined(SYSTEM_LINUX)
			convention = [vecArgs, ret] { return new x86GccCdecl(vecArgs, ret); };
#endif
			break;
#endif
		default:
			return 0;
		}

		Hook* hook = manager.hook(udata->GetFunctionPtr(), convention);
		hook->m_extradata = (void*)udata;
		hook->addCallback(HookType::Pre, (HookHandler*)&PreHook);
		
		return 0;
	}

	LUA_FUNCTION_STATIC(CNative)
	{
		CNativeCall* pCall = new CNativeCall();

		Push(LUA, pCall);

		return 1;
	}
	
	void InitEnums(GarrysMod::Lua::ILuaBase* LUA)
	{
		LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);

		// arg types 
		LUA->PushString("FFI_ARG_INT");
		LUA->PushNumber((int)FFIArg::INT);
		LUA->SetTable(-3);

		LUA->PushString("FFI_ARG_DOUBLE");
		LUA->PushNumber((int)FFIArg::DOUBLE);
		LUA->SetTable(-3);

		LUA->PushString("FFI_ARG_STRING");
		LUA->PushNumber((int)FFIArg::STRING);
		LUA->SetTable(-3);

		LUA->PushString("FFI_ARG_ENTITY");
		LUA->PushNumber((int)FFIArg::ENTITY);
		LUA->SetTable(-3);

		// return types
		LUA->PushString("FFI_RET_INT");
		LUA->PushNumber((int)FFIRet::INT);
		LUA->SetTable(-3);

		LUA->PushString("FFI_RET_DOUBLE");
		LUA->PushNumber((int)FFIRet::DOUBLE);
		LUA->SetTable(-3);

		LUA->PushString("FFI_RET_STRING");
		LUA->PushNumber((int)FFIRet::STRING);
		LUA->SetTable(-3);

		LUA->PushString("FFI_RET_ENTITY");
		LUA->PushNumber((int)FFIRet::ENTITY);
		LUA->SetTable(-3);

		LUA->PushString("FFI_RET_VOID");
		LUA->PushNumber((int)FFIRet::VOID);
		LUA->SetTable(-3);

		// ABI call types
		LUA->PushString("FFI_SYSV");
		LUA->PushNumber(FFI_SYSV);
		LUA->SetTable(-3);

		LUA->PushString("FFI_STDCALL");
		LUA->PushNumber(FFI_STDCALL);
		LUA->SetTable(-3);

		LUA->PushString("FFI_THISCALL");
		LUA->PushNumber(FFI_THISCALL);
		LUA->SetTable(-3);

		LUA->PushString("FFI_CDECL");
		LUA->PushNumber(FFI_MS_CDECL);
		LUA->SetTable(-3);

		LUA->PushString("FFI_PASCAL");
		LUA->PushNumber(FFI_PASCAL);
		LUA->SetTable(-3);

		LUA->PushString("FFI_REGISTER");
		LUA->PushNumber(FFI_REGISTER);
		LUA->SetTable(-3);

		// Detour ReturnActions
		LUA->PushString("Action_Ignored");
		LUA->PushNumber((int)ReturnAction::Ignored);
		LUA->SetTable(-3);

		LUA->PushString("Action_Handled");
		LUA->PushNumber((int)ReturnAction::Handled);
		LUA->SetTable(-3);

		LUA->PushString("Action_Overrride");
		LUA->PushNumber((int)ReturnAction::Override);
		LUA->SetTable(-3);

		LUA->PushString("Action_Supercede");
		LUA->PushNumber((int)ReturnAction::Supercede);
		LUA->SetTable(-3);


		LUA->Pop(); // pop SPECIAL_GLOB
	}

	void InitMT(GarrysMod::Lua::ILuaBase* LUA)
	{
		metatype = LUA->CreateMetaTable(metaname);

		LUA->PushCFunction(gc);
		LUA->SetField(-2, "__gc");

		LUA->PushCFunction(index);
		LUA->SetField(-2, "__index");

		LUA->PushCFunction(newindex);
		LUA->SetField(-2, "__newindex");

		LUA->PushCFunction(PushArgument);
		LUA->SetField(-2, "PushArgument");

		LUA->PushCFunction(PushArgumentType);
		LUA->SetField(-2, "PushArgumentType");

		LUA->PushCFunction(SetCallType);
		LUA->SetField(-2, "SetCallType");

		LUA->PushCFunction(ClearArguments);
		LUA->SetField(-2, "ClearArguments");

		LUA->PushCFunction(SetReturnType);
		LUA->SetField(-2, "SetReturnType");

		LUA->PushCFunction(SetSignature);
		LUA->SetField(-2, "SetSignature");

		LUA->PushCFunction(Prep);
		LUA->SetField(-2, "Prep");

		LUA->PushCFunction(Call);
		LUA->SetField(-2, "Call");

		LUA->PushCFunction(Detour);
		LUA->SetField(-2, "Detour");

		LUA->Pop();
	}
	
	void Initialize(GarrysMod::Lua::ILuaBase* LUA)
	{
		lua = LUA;

		InitEnums(LUA);
		InitMT(LUA);
		
		LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);

		LUA->PushCFunction(CNative);
		LUA->SetField(-2, "CNative");

		LUA->Pop(); // pop SPECIAL_GLOB
	}

	void Deinitialize(GarrysMod::Lua::ILuaBase* LUA)
	{
		LUA->PushNil();
		LUA->SetField(GarrysMod::Lua::INDEX_REGISTRY, metaname);

		HookManager& mng = HookManager::Get();

		mng.unhookAll();
	}
}