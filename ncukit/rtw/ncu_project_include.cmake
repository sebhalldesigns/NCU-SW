include_guard(GLOBAL)

# Inject NCU project wiring after project() has completed for the real build,
# but avoid polluting CMake's internal compiler-detection try_compile projects.
if(PROJECT_NAME STREQUAL "CMAKE_TRY_COMPILE")
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../ncukit.cmake")
