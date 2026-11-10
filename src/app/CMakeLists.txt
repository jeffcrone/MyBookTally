# mybooktally_gui: the windows and dialogs, as a library so the tests can
# create them too. The executable itself is just main.cpp on top.
add_library(mybooktally_gui STATIC
	BookDetailsDialog.cpp
	BookDetailsDialog.h
	BookEditorDialog.cpp
	BookEditorDialog.h
	CoverImage.cpp
	CoverImage.h
	LinkListEditor.cpp
	LinkListEditor.h
	MainWindow.cpp
	MainWindow.h
	ManageDialog.cpp
	ManageDialog.h
	SmallDialogs.cpp
	SmallDialogs.h
	UiUtil.cpp
	UiUtil.h
)

target_include_directories(mybooktally_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mybooktally_gui PUBLIC mybooktally_core Qt6::Widgets)

# The icons live in one folder at the root of the repository and are used
# from there rather than copied. BASE strips that folder from the resource
# path, so the app loads them as ":/icons/MyBookTally_Icon_64x64.png".
set(icon_dir "${PROJECT_SOURCE_DIR}/MyBookTally_Icons")
qt_add_resources(mybooktally_gui "icons"
	PREFIX "/icons"
	BASE "${icon_dir}"
	FILES
		"${icon_dir}/MyBookTally_Icon_16x16.png"
		"${icon_dir}/MyBookTally_Icon_24x24.png"
		"${icon_dir}/MyBookTally_Icon_32x32.png"
		"${icon_dir}/MyBookTally_Icon_48x48.png"
		"${icon_dir}/MyBookTally_Icon_64x64.png"
		"${icon_dir}/MyBookTally_Icon_128x128.png"
		"${icon_dir}/MyBookTally_Icon_256x256.png"
)

# WIN32_EXECUTABLE stops Windows from opening a console window alongside the
# app. It has no effect on Linux.
qt_add_executable(MyBookTally WIN32 main.cpp)
target_link_libraries(MyBookTally PRIVATE mybooktally_gui)

if(WIN32)
	# Gives the .exe its icon in Explorer and the taskbar, and fills in the
	# version shown under Properties > Details. The .rc is generated so its
	# version numbers come from project() and its icon path points at the
	# shared icons folder.
	set(MYBOOKTALLY_ICO "${icon_dir}/MyBookTally_Icon.ico")
	file(TO_CMAKE_PATH "${MYBOOKTALLY_ICO}" MYBOOKTALLY_ICO)
	configure_file(MyBookTally.rc.in "${CMAKE_CURRENT_BINARY_DIR}/MyBookTally.rc" @ONLY)
	target_sources(MyBookTally PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/MyBookTally.rc")
endif()

# ---- Installing -----------------------------------------------------------
#
# "cmake --install" puts a runnable copy of the app in the install prefix.
# On Windows it also copies in the Qt DLLs and plugins the app needs (Qt's
# deployment script runs windeployqt), which is how the release zip on
# GitHub is made.
include(GNUInstallDirs)

install(TARGETS MyBookTally
	BUNDLE DESTINATION .
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
	qt_generate_deploy_app_script(
		TARGET MyBookTally
		OUTPUT_SCRIPT deploy_script
		NO_UNSUPPORTED_PLATFORM_ERROR
	)
	install(SCRIPT ${deploy_script})
	install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(UNIX AND NOT APPLE)
	# Linux desktop integration: a menu entry, and the icon at each size the
	# freedesktop icon theme looks for. Qt itself comes from the system's
	# packages on Linux, so nothing else is copied.
	install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/mybooktally.desktop"
		DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
	foreach(size 16 24 32 48 64 128 256 512)
		install(FILES "${icon_dir}/MyBookTally_Icon_${size}x${size}.png"
			DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
			RENAME mybooktally.png)
	endforeach()
	install(FILES "${icon_dir}/MyBookTally_Icon.svg"
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
		RENAME mybooktally.svg)
endif()
