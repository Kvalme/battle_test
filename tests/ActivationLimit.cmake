file(MAKE_DIRECTORY "${TEST_DIR}")
set(scenario "${TEST_DIR}/scenario.txt")
set(prefix "CREATE_MAP 2 1\nSPAWN_SWORDSMAN 1 0 0 10 1 0 0\n")

file(WRITE "${scenario}" "${prefix}SPAWN_SWORDSMAN 2 1 0 2 0 0 0\n")
execute_process(COMMAND "${PROGRAM}" "${scenario}" WORKING_DIRECTORY "${TEST_DIR}"
    TIMEOUT 5 RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT "${result}" STREQUAL "2" OR NOT errors MATCHES "^Simulation activation limit reached [(]1[)]")
    message(FATAL_ERROR "Expected exit 2 with an activation-limit diagnostic: ${result}\n${errors}")
endif()
if(NOT output MATCHES "UNIT_ATTACKED attackerUnitId=1 targetUnitId=2 damage=1 targetHp=1"
        OR output MATCHES "limit|UNIT_DIED")
    message(FATAL_ERROR "Unexpected event stream at the activation limit:\n${output}")
endif()

file(WRITE "${scenario}" "${prefix}SPAWN_SWORDSMAN 2 1 0 1 0 0 0\n")
execute_process(COMMAND "${PROGRAM}" "${scenario}" WORKING_DIRECTORY "${TEST_DIR}"
    TIMEOUT 5 RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT "${result}" STREQUAL "0" OR NOT "${errors}" STREQUAL "" OR NOT output MATCHES "UNIT_DIED unitId=2")
    message(FATAL_ERROR "Ordinary completion on the last permitted activation must succeed:\n${result}\n${output}\n${errors}")
endif()
