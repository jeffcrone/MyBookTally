# Automated tests, written with Qt Test. Each tst_*.cpp is its own small
# executable and its own CTest test, so a failure names the area at once and
# one crashing test does not hide the results of the others.
#
# Run them all with:  ctest --preset <preset-name>
# (see "Running the automated tests" in README.md).

# Adds one Qt Test executable and registers it with CTest.
#   name   - the test's name, and the source file is ${name}.cpp
#   ARGN   - libraries to link besides Qt Test
function(mybooktally_add_test name)
	qt_add_executable(${name} ${name}.cpp TestSupport.h)
	target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
	target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	add_test(NAME ${name} COMMAND ${name})

	# "offscreen" lets the widget tests create windows on a machine with no
	# display, such as the GitHub Actions runners. The database tests do not
	# need it but it does them no harm.
	#
	# QTEST_DISABLE_STACK_DUMP stops a crashing test from attaching a
	# debugger to print a stack trace, which can hang indefinitely on a
	# machine where the debugger waits for input.
	set(test_environment "QT_QPA_PLATFORM=offscreen;QTEST_DISABLE_STACK_DUMP=1")

	if(WIN32)
		# On Windows a program finds DLLs on PATH, and a fresh build folder
		# does not have Qt's DLLs next to the test .exe files. Put Qt's bin
		# folder (found from where CMake found Qt) at the front of PATH for
		# the tests only, so they run without deploying Qt first.
		get_filename_component(qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
		set_tests_properties(${name} PROPERTIES
			ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${qt_bin_dir}")
	endif()

	# A test that hangs (a dialog left waiting for a click, say) is failed
	# after two minutes instead of stalling the build forever. The whole
	# suite normally finishes in a few seconds.
	set_tests_properties(${name} PROPERTIES
		ENVIRONMENT "${test_environment}"
		TIMEOUT 120)
endfunction()

mybooktally_add_test(tst_isbn mybooktally_core)
mybooktally_add_test(tst_database mybooktally_core)
mybooktally_add_test(tst_lookup_repository mybooktally_core)
mybooktally_add_test(tst_book_repository mybooktally_core)
mybooktally_add_test(tst_review_repository mybooktally_core)
mybooktally_add_test(tst_list_repository mybooktally_core)
mybooktally_add_test(tst_json_exchange mybooktally_core)
mybooktally_add_test(tst_book_table_model mybooktally_core)
mybooktally_add_test(tst_gui mybooktally_gui)

# Linux only: checks that installing produces a menu entry and icons, which
# are the only way a Linux app shows its icon. Not a Qt Test program, so it
# is added directly rather than with mybooktally_add_test().
if(UNIX AND NOT APPLE)
	add_test(NAME check_linux_install
		COMMAND "${CMAKE_COMMAND}"
			-DBUILD_DIR=${PROJECT_BINARY_DIR}
			-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-check
			-P "${CMAKE_CURRENT_SOURCE_DIR}/check_linux_install.cmake")
	set_tests_properties(check_linux_install PROPERTIES TIMEOUT 120)
endif()
