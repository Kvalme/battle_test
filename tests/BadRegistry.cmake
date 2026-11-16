file(MAKE_DIRECTORY "${TEST_DIR}")
execute_process(COMMAND "${PROGRAM}" "${TEST_DIR}/absent.txt"
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT status STREQUAL "1" OR NOT output STREQUAL "")
    message(FATAL_ERROR "Bad registry should fail before producing events: ${status}: ${output}")
endif()
if(NOT errors MATCHES "^Duplicate ability registration 'rending' at [^\n]+\n$"
    OR NOT errors MATCHES "BadRegistry\\.cpp:[0-9]+" OR NOT errors MATCHES "Rending\\.cpp:[0-9]+")
    message(FATAL_ERROR "Expected exactly one diagnostic identifying both registrars: ${errors}")
endif()
