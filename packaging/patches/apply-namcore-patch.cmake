# Applies the NAM core guard patch once. FetchContent re-runs PATCH_COMMAND on
# every reconfigure that touches the declaration, so a plain `git apply`
# fails the second time; skip when the patch is already in.
execute_process(COMMAND "${GIT}" apply --reverse --check --ignore-whitespace "${PATCH}"
                RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
if(already_applied EQUAL 0)
    return()
endif()
execute_process(COMMAND "${GIT}" apply --ignore-whitespace "${PATCH}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "SignalPatch: could not apply ${PATCH}")
endif()
