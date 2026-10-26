-- OpenPreCEPatch build file.
--
-- The layout follows GTAIV.EFLC.FusionFix: a workspace under build/, one shared
-- library per shipped ASI, and a .env file (not tracked) that names the local
-- game folder for the post-build copy.

newoption {
    trigger = "with-version",
    value = "STRING",
    description = "Current version",
}

newoption {
    trigger = "parity-baseline",
    description = "Force every folded improvement group off (legacy path)",
}

local envkeys = nil
function envdir(key)
    if not envkeys then
        envkeys = {}
        local text = io.readfile(path.join(_SCRIPT_DIR, ".env")) or ""
        for line in text:gmatch("[^\r\n]+") do
            local k, v = line:match("^%s*([%w_]+)%s*=%s*(.-)%s*$")
            if k and v ~= "" then
                v = v:gsub('^"', ""):gsub('"$', ""):gsub("^'", ""):gsub("'$", "")
                envkeys[k] = v
            end
        end
    end
    local value = envkeys[key]
    if not value then return nil end
    value = value:gsub("[%s\\/]+$", "")
    if value == "" then return nil end
    return path.translate(value)
end

workspace "OpenPreCEPatch"
    configurations { "Release", "Debug" }
    architecture "x86"
    location "build"
    cppdialect "C++latest"
    kind "SharedLib"
    language "C++"
    targetdir "bin/%{cfg.buildcfg}"
    targetextension ".asi"
    buildoptions { "/Zc:__cplusplus", "/utf-8" }
    staticruntime "On"
    characterset "Unicode"
    multiprocessorcompile "On"
    startproject "IV.EFLC.OpenPreCEPatch"

    defines { "rsc_CompanyName=\"OpenPreCEPatch\"" }
    defines { "rsc_LegalCopyright=\"GPL-3.0-or-later\"" }
    defines { "rsc_InternalName=\"%{prj.name}\"", "rsc_ProductName=\"%{prj.name}\"" }
    defines { "rsc_FileDescription=\"OpenPreCEPatch\"" }
    defines { "rsc_UpdateUrl=\"https://github.com/OpenZDecomp/OpenPreCEPatch\"" }
    defines { "rsc_OriginalFilename=\"%{cfg.buildtarget.name}\"" }
    defines { "_CRT_SECURE_NO_WARNINGS" }
    if _OPTIONS["parity-baseline"] then
        defines { "OPCEP_PARITY_BASELINE" }
    end

    local major = os.date("%d")
    local minor = os.date("%m")
    local build = os.date("%Y")
    local revision = os.date("%H") .. os.date("%M")
    if _OPTIONS["with-version"] then
        local t = {}
        for i in _OPTIONS["with-version"]:gmatch("([^.]+)") do
            t[#t + 1], _ = i:gsub("%D+", "")
        end
        while #t < 4 do t[#t + 1] = 0 end
        major, minor = math.min(tonumber(t[1]), 255), math.min(tonumber(t[2]), 255)
        build, revision = math.min(tonumber(t[3]), 65535), math.min(tonumber(t[4]), 65535)
    end
    local githash = ""
    local f = io.popen("git rev-parse --short HEAD")
    if f then githash = f:read("*a"):gsub("%s+", ""); f:close() end
    local productVersion = major .. "." .. minor .. "." .. build .. "." .. revision
    if githash ~= "" then productVersion = productVersion .. "-" .. githash end
    defines { "rsc_FileVersion_MAJOR=" .. major }
    defines { "rsc_FileVersion_MINOR=" .. minor }
    defines { "rsc_FileVersion_BUILD=" .. build }
    defines { "rsc_FileVersion_REVISION=" .. revision }
    defines { "rsc_ProductVersion=\"" .. productVersion .. "\"" }
    defines { "rsc_GitSHA1=\"" .. githash .. "\"" }
    defines { "rsc_GitSHA1W=L\"" .. githash .. "\"" }

    includedirs { "source" }
    includedirs { "external/injector/include" }
    includedirs { "external/hooking" }
    includedirs { "external/inireader" }

project "IV.EFLC.OpenPreCEPatch"
    targetdir "bin"
    postbuildcommands {
        "copy /y \"data\\plugins\\IV.EFLC.OpenPreCEPatch.ini\" \"$(TargetDir)IV.EFLC.OpenPreCEPatch.ini\"",
    }
    files { "source/**.cpp", "source/**.ixx", "source/**.hxx", "source/**.h" }
    removefiles { "source/compat/**", "source/xliveless_features.ixx" }
    files { "external/hooking/Hooking.Patterns.h", "external/hooking/Hooking.Patterns.cpp" }
    links { "version", "user32", "shlwapi" }
    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"
    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
    local gamedir = envdir("GTAIV_DIR")
    if gamedir then
        postbuildcommands {
            "if exist \"" .. gamedir .. "\\IV.EFLC.OpenPreCEPatch.asi\" copy /y \"$(TargetPath)\" \"" .. gamedir .. "\\\"",
        }
    end

-- XLivelessAddon successor: separate source tree (xlivelessaddon/) and a
-- separate ASI. Do not mix it into IV.EFLC.OpenPreCEPatch.asi.
project "XLivelessAddon"
    targetdir "bin"
    files { "xlivelessaddon/source/**.cpp", "xlivelessaddon/source/**.h" }
    files { "external/hooking/Hooking.Patterns.h", "external/hooking/Hooking.Patterns.cpp" }
    links { "user32" }
    postbuildcommands {
        "copy /y \"xlivelessaddon\\data\\XLivelessAddon.ini\" \"$(TargetDir)XLivelessAddon.ini\"",
    }
    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"
    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
