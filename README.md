# garrysmod_nativecalls
A **WIP** FFI and hooking module for Garry's Mod.

## Requirements
* [garrysmod_common](https://github.com/danielga/garrysmod_common)
* [DynoHook](https://github.com/Code4Cookie/DynoHook)
* [LibFFI](https://github.com/libffi/libffi)

## Building
Included in premake5.lua are custom options for the requirements' paths. They must be set or errors will occur.
Ensure that you are building DynoHook and LibFFI as static libraries, along with the correct runtimes.

## Examples
```lua
function NativeDetour()
    local sig = [[\x55\x8B\xEC\x51\x53\x56\x8B\x35\x2A\x2A\x2A\x2A\x8B\xD9\x57\x8B\xCE\x89\x75]] -- CBaseAnimating::SetModel

    local native = CNative()
    native:SetSignature( sig )
    native:SetCallType( FFI_THISCALL )
    native:SetReturnType( FFI_RET_VOID )

    -- Push Argument types in order
    native:PushArgumentType( FFI_ARG_ENTITY )
    native:PushArgumentType( FFI_ARG_STRING )

    native:Prep()

    native:Detour( function( ent, str )
        print( str )
    end )
end

function NativeCall()
    local sig = [[\x55\x8B\xEC\x8B\x55\x08\x85\xD2\x7E\x27]] -- UTIL_GetEntityByIndex

    local native = CNative()
    native:SetSignature( sig )
    native:SetCallType( FFI_MS_CDECL )
    native:SetReturnType( FFI_RET_ENTITY )

    native:PushArgument( FFI_ARG_INT, 1 )

    native:Prep()

    local ent = native:Call()

    print( ent )
end
```

## Confirmed Working
| Tested | OS | Arch |
| --- | --- | --- |
|  ✔ | Linux | x32 |
| ✔ | Windows | x32 |
|  | Linux | x64 |
|  | Windows | x64 |
