if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED SOURCE_DIR OR
   NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "Git patch arguments are incomplete")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET
    ERROR_QUIET)
if(already_applied EQUAL 0)
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE patch_check
    ERROR_VARIABLE patch_check_error)
if(NOT patch_check EQUAL 0)
    message(FATAL_ERROR "Patch cannot be applied: ${patch_check_error}")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --unidiff-zero "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE patch_result
    ERROR_VARIABLE patch_error)
if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "Patch failed: ${patch_error}")
endif()
