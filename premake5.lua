PROJECT_GENERATOR_VERSION = 2

require "cmake"

newoption({
	trigger = "gmcommon",
	description = "Sets the path to the garrysmod_common (https://github.com/danielga/garrysmod_common) directory",
	value = "../garrysmod_common"
})

newoption({
	trigger = "libffi_h",
	description = "Sets the path to the location of the libffi headers",
	value = ""
})

newoption({
	trigger = "dynohook_h",
	description = "Sets the path to the location of the libffi files",
	value = ""
})

newoption({
	trigger = "libffi_l",
	description = "Sets the libffi static library file for linkage",
	value = ""
})

newoption({
	trigger = "dynohook_l",
	description = "Sets the dynohook static library file for linkage",
	value = ""
})


includedirsafter = {}

local gmcommon = assert(_OPTIONS.gmcommon or os.getenv("GARRYSMOD_COMMON"),
	"you didn't provide a path to your garrysmod_common (https://github.com/danielga/garrysmod_common) directory")

CreateWorkspace({name = "garrysmod_nativecalls", abi_compatible = true})
	CreateProject({serverside = true})
		IncludeLuaShared()
		IncludeHelpersExtended()
		IncludeSDKCommon()
		IncludeSDKTier0()
		IncludeSDKTier1()
		IncludeSteamAPI()
		IncludeDetouring()
		IncludeScanning()

		links({_OPTIONS.libffi_l, _OPTIONS.dynohook_l})

		includedirs({_OPTIONS.dynohook_l, _OPTIONS.libffi_l})
		
		files({
			"source/main.cpp",

			"source/nativecall.cpp",
			"source/nativecall.h",

			"source/nativeutil.cpp",
			"source/nativeutil.h"
		})