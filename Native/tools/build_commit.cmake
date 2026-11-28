cmake_minimum_required(VERSION 3.21)

set(build_commit "")
if(GIT_EXECUTABLE AND EXISTS "${SOURCE_ROOT}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_ROOT}"
        OUTPUT_VARIABLE build_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_result
        ERROR_QUIET
    )
    if(NOT git_result STREQUAL "0" OR NOT build_commit MATCHES "^[0-9a-f]+$")
        set(build_commit "")
    endif()
endif()
file(CONFIGURE OUTPUT "${OUTPUT_FILE}"
    CONTENT "constexpr std::string_view buildCommit = \"@build_commit@\";\n"
    @ONLY
)
