# Checks the Linux desktop integration that "cmake --install" produces.
#
# Linux programs cannot carry their own icon (see src/app/CMakeLists.txt),
# so the menu entry and the icon theme files are the app's only icon. This
# installs the build into a scratch folder and checks that:
#   - mybooktally.desktop exists, names the icon, and its Exec= line is the
#     full path of the installed program (not the template placeholder);
#   - an icon named mybooktally exists at every size and as SVG;
#   - desktop-file-validate accepts the menu entry, when that tool is present.
#
# Run by CTest as:
#   cmake -DBUILD_DIR=<build folder> -DWORK_DIR=<scratch folder> -P check_linux_install.cmake

foreach(required BUILD_DIR WORK_DIR)
	if(NOT DEFINED ${required})
		message(FATAL_ERROR "${required} must be given with -D")
	endif()
endforeach()

set(prefix "${WORK_DIR}/prefix")
file(REMOVE_RECURSE "${WORK_DIR}")

execute_process(
	COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}"
	RESULT_VARIABLE install_result
	OUTPUT_VARIABLE install_output
	ERROR_VARIABLE install_output
)
if(NOT install_result EQUAL 0)
	message(FATAL_ERROR "cmake --install failed:\n${install_output}")
endif()

set(program "${prefix}/bin/MyBookTally")
if(NOT EXISTS "${program}")
	message(FATAL_ERROR "The program was not installed to ${program}")
endif()

set(desktop "${prefix}/share/applications/mybooktally.desktop")
if(NOT EXISTS "${desktop}")
	message(FATAL_ERROR "The menu entry was not installed to ${desktop}")
endif()
file(READ "${desktop}" desktop_text)

if(desktop_text MATCHES "@")
	message(FATAL_ERROR "The menu entry still contains a template placeholder:\n${desktop_text}")
endif()
string(FIND "${desktop_text}" "Exec=\"${program}\"" exec_at)
if(exec_at EQUAL -1)
	message(FATAL_ERROR "Expected Exec=\"${program}\" in the menu entry:\n${desktop_text}")
endif()
string(FIND "${desktop_text}" "\nIcon=mybooktally\n" icon_at)
if(icon_at EQUAL -1)
	message(FATAL_ERROR "Expected Icon=mybooktally in the menu entry:\n${desktop_text}")
endif()

foreach(size 16 24 32 48 64 128 256 512)
	set(icon "${prefix}/share/icons/hicolor/${size}x${size}/apps/mybooktally.png")
	if(NOT EXISTS "${icon}")
		message(FATAL_ERROR "Missing icon ${icon}")
	endif()
endforeach()
if(NOT EXISTS "${prefix}/share/icons/hicolor/scalable/apps/mybooktally.svg")
	message(FATAL_ERROR "Missing the scalable SVG icon")
endif()

find_program(DESKTOP_FILE_VALIDATE desktop-file-validate)
if(DESKTOP_FILE_VALIDATE)
	execute_process(
		COMMAND "${DESKTOP_FILE_VALIDATE}" "${desktop}"
		RESULT_VARIABLE validate_result
		OUTPUT_VARIABLE validate_output
		ERROR_VARIABLE validate_output
	)
	if(NOT validate_result EQUAL 0)
		message(FATAL_ERROR "desktop-file-validate rejected the menu entry:\n${validate_output}")
	endif()
else()
	message(STATUS "desktop-file-validate not found; skipped that check")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "Linux install layout OK")
