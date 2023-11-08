#pragma once

#include <GarrysMod/Lua/Interface.h>

#include <ffi.h>
#include <vector>
#include <unordered_map>

namespace NativeCall
{
	void Initialize(GarrysMod::Lua::ILuaBase* LUA);
	void Deinitialize(GarrysMod::Lua::ILuaBase* LUA);

	class CNativeCall
	{
	public:
		void PushArgument(ffi_type* pType, void* pVal, int my_type)
		{
			m_arg_types.push_back(pType);
			m_arg_values.push_back(pVal);
			m_arg_mytypes.push_back(my_type);
		}

		void SetCallType(ffi_abi callconv)
		{
			m_callconv = callconv;
		}

		void SetReturnType(ffi_type* rtype, int my_type)
		{
			m_rtype = rtype;
			m_iMyRType = my_type;
		}

		void SetDetourFunctionRef(int iDetourRef)
		{
			m_iLuaDetourRef = iDetourRef;
		}

		void SetDetourClosure(ffi_closure* pClosure)
		{
			m_FnClosure = pClosure;
		}

		ffi_type* GetReturnType(int* my_type = NULL)
		{
			if (my_type)
				*my_type = m_iMyRType;

			return m_rtype;
		}

		ffi_abi GetCallType()
		{
			return m_callconv;
		}

		int GetMyReturnType()
		{
			return m_iMyRType;
		}

		ffi_cif& GetCif()
		{
			return m_cif;
		}

		void* GetFunctionPtr()
		{
			return m_FnTarget;
		}

		int GetDetourFunctionRef()
		{
			return m_iLuaDetourRef;
		}

		ffi_arg& GetReturnPtr()
		{
			return m_rc;
		}

		ffi_status GetStatus()
		{
			return m_status;
		}

		std::vector<void*>& GetArgValues()
		{
			return m_arg_values;
		}

		std::vector<ffi_type*>& GetArgTypes()
		{
			return m_arg_types;
		}

		std::vector<int>& GetMyArgTypes()
		{
			return m_arg_mytypes;
		}

		void ClearArguments()
		{
			m_arg_mytypes.clear();
			m_arg_types.clear();
			m_arg_values.clear();
		}

		void AddGlobalDetourFunctionRef(int ref)
		{
		//	m_DetourRefs[ref] = ref;
		}

		void SetFunctionPtr(void (*fn)())
		{
			m_FnTarget = fn;
		}

		void ClearArgs()
		{
			ClearArgValues();

			m_arg_values.clear();
			m_arg_mytypes.clear();
		}

		~CNativeCall();

		bool Prep();
		bool Call();
	private:
		void ClearArgValues();

		// parallel vectors so we can call the FFI functions without needing to construct another array.
		std::vector<void*> m_arg_values;
		std::vector<ffi_type*> m_arg_types;
		std::vector<int> m_arg_mytypes;

		//static std::unordered_map<int, int> m_DetourRefs;

		ffi_cif m_cif;
		ffi_abi m_callconv = FFI_FIRST_ABI;
		ffi_arg m_rc; // return value storage
		ffi_type* m_rtype = NULL; // return type
		ffi_status m_status;
		ffi_closure* m_FnClosure = NULL;

		int	m_iMyRType = 0;
		int m_iLuaDetourRef = 0;

		void (*m_FnTarget)() = NULL;
		
	};
}