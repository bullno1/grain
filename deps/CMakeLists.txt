# --- cute_framework ---
if (${RELOADABLE})
	set(CF_FRAMEWORK_STATIC OFF CACHE BOOL "")
else ()
	set(CF_FRAMEWORK_STATIC ON CACHE BOOL "")
endif ()
set(CF_FRAMEWORK_BUILD_SAMPLES OFF CACHE BOOL "")
set(CF_FRAMEWORK_BUILD_TESTS OFF CACHE BOOL "")
set(CF_RUNTIME_SHADER_COMPILATION OFF CACHE BOOL "")
set(CF_CUTE_SHADERC OFF CACHE BOOL "")
set(PHYSFS_ARCHIVE_7Z FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_GRP FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_WAD FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_CSM FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_HOG FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_MVL FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_QPAK FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_ROFS FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_SLB FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_ISO9660 FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_VDF FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_LECARCHIVES FALSE CACHE BOOL "")
set(PHYSFS_ARCHIVE_POD FALSE CACHE BOOL "")

add_subdirectory("cute_framework" EXCLUDE_FROM_ALL)

# Make the include path system so that targets linking to cute framework can
# use a stricter warning settings.
target_include_directories(cute SYSTEM PUBLIC BEFORE "cute_framework/include")
target_include_directories(cute SYSTEM PUBLIC BEFORE "cute_framework/libraries")
target_include_directories(cute SYSTEM PUBLIC BEFORE "cute_framework/libraries/cimgui")
target_include_directories(cute SYSTEM PUBLIC BEFORE "cute_framework/libraries/cimgui/imgui")

# --- remodule --
add_library(remodule INTERFACE)
target_include_directories(remodule INTERFACE remodule)

# --- blibs --
add_library(blibs INTERFACE)
target_include_directories(blibs INTERFACE blibs)

# --- clay ---
add_library(clay INTERFACE)
target_include_directories(clay INTERFACE clay)
