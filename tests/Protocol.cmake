# Run in an empty working directory: the executable must not require units.xml beside it.
file(MAKE_DIRECTORY "${TEST_DIR}")
file(WRITE "${TEST_DIR}/scenario.txt" "CREATE_MAP 3 1\nSPAWN_SWORDSMAN 9 0 0 10 1 1000 5\nSPAWN_SWORDSMAN 1 1 0 4 1 0 0\n")
execute_process(COMMAND "${PROGRAM}" "${TEST_DIR}/scenario.txt"
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status OUTPUT_VARIABLE actual ERROR_VARIABLE errors TIMEOUT 10)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "")
    message(FATAL_ERROR "CLI failed: ${status} ${errors}")
endif()
set(expected "[0] MAP_CREATED width=3 height=1 \n[0] UNIT_SPAWNED unitId=9 unitType=swordsman x=0 y=0 \n[0] UNIT_SPAWNED unitId=1 unitType=swordsman x=1 y=0 \n[1] UNIT_ABILITY_USED abilityUnitId=9 abilityName=rending \n[1] UNIT_ATTACKED attackerUnitId=9 targetUnitId=1 damage=4 targetHp=0 \n[1] UNIT_DIED unitId=1 \n")
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "Output protocol mismatch. Expected:\n${expected}\nActual:\n${actual}")
endif()
execute_process(COMMAND "${PROGRAM}" "${SOURCE_DIR}/scenario_4.txt"
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status OUTPUT_VARIABLE first ERROR_VARIABLE errors TIMEOUT 10)
execute_process(COMMAND "${PROGRAM}" "${SOURCE_DIR}/scenario_4.txt"
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status2 OUTPUT_VARIABLE second TIMEOUT 10)
if(NOT status EQUAL 0 OR NOT status2 EQUAL 0 OR NOT first STREQUAL second)
    message(FATAL_ERROR "Hunter scenario failed or was not reproducible: ${errors}")
endif()
if(NOT first MATCHES "unitType=hunter")
    message(FATAL_ERROR "Hunter was not spawned from existing input command")
endif()

function(reject_command command diagnostic)
    file(WRITE "${TEST_DIR}/invalid.txt" "  // setup\nCREATE_MAP +3 1 // valid map\n\n${command}\n")
    execute_process(COMMAND "${PROGRAM}" "${TEST_DIR}/invalid.txt"
        WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
    if(NOT status STREQUAL "1" OR NOT output STREQUAL "[0] MAP_CREATED width=3 height=1 \n"
        OR NOT errors MATCHES "^Error: Line 4: ${diagnostic}\n$")
        message(FATAL_ERROR "Incorrect rejection of '${command}': ${status}\n${output}${errors}")
    endif()
endfunction()

reject_command("SPAWN_SWORDSMAN 1 0 0 10" "SPAWN_SWORDSMAN: Missing field 'strength'")
reject_command("SPAWN_SWORDSMAN 1 0 0 -1 1 0 0" "SPAWN_SWORDSMAN: Invalid unsigned integer for field 'hp': -1")
reject_command("SPAWN_SWORDSMAN 1 0 0 4294967296 1 0 0" "SPAWN_SWORDSMAN: Value out of range for field 'hp': 4294967296")
reject_command("SPAWN_SWORDSMAN 1 0 0 10 1 0 2x" "SPAWN_SWORDSMAN: Invalid unsigned integer for field 'rending': 2x")
reject_command("SPAWN_SWORDSMAN 1 0 0 10 1 0 0 extra" "SPAWN_SWORDSMAN: Unexpected argument: extra")
reject_command("SPAWN_SWORDSMAN 1 0 0 10 1 1001 0"
    "SPAWN_SWORDSMAN: swordsman.xml: unit 'swordsman', ability #1 .*rending.*: property 'chanceStat' referencing unit stat 'rending_chance' must be between 0 and 1000 .*")
reject_command("UNKNOWN 1" "Unknown command: UNKNOWN")
