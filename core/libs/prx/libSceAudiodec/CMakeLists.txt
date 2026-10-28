include(${CMAKE_CURRENT_SOURCE_DIR}/AudiodecLibrary.cmake)
add_sce_audiodec_library(libSceAudiodec)

if(BUILD_TESTING)
    add_test_executable(guest_audiodec_tests ${LIBS_INCLUDE_DIR}/tests/GuestAudiodec.cpp)
    target_include_directories(guest_audiodec_tests PRIVATE ${LIBS_INCLUDE_DIR})
    target_link_libraries(guest_audiodec_tests PRIVATE libSceAudiodec libc)
    configure_windows_unwind(guest_audiodec_tests)
    add_test(NAME guest_audiodec COMMAND guest_audiodec_tests)
    if(WIN32)
        set_tests_properties(guest_audiodec PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceAudiodec>")
    endif()

    add_test_executable(audiodec_context_lifetime_tests ${LIBS_INCLUDE_DIR}/tests/AudiodecContextLifetime.cpp)
    target_include_directories(audiodec_context_lifetime_tests PRIVATE
        ${LIBS_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/3rdparty/LibAtrac9/C/src
    )
    target_link_libraries(audiodec_context_lifetime_tests PRIVATE atrac9 ffmpeg libc)
    configure_windows_unwind(audiodec_context_lifetime_tests)
    add_test(NAME audiodec_context_lifetime COMMAND audiodec_context_lifetime_tests)
    set_tests_properties(audiodec_context_lifetime PROPERTIES TIMEOUT 30)
    if(WIN32)
        set_tests_properties(audiodec_context_lifetime PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
endif()
