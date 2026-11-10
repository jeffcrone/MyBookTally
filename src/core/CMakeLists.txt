# mybooktally_core: everything that touches the database, with no widgets.
#
# Keeping this separate from the user interface means the automated tests can
# exercise every SQL query directly, without opening a window.
add_library(mybooktally_core STATIC
	BookRepository.cpp
	BookRepository.h
	BookTableModel.cpp
	BookTableModel.h
	Database.cpp
	Database.h
	DatabaseError.h
	Entities.h
	Isbn.cpp
	Isbn.h
	JsonExchange.cpp
	JsonExchange.h
	ListRepository.cpp
	ListRepository.h
	LookupRepository.cpp
	LookupRepository.h
	ReviewRepository.cpp
	ReviewRepository.h
	Schema.cpp
	Schema.h
	SqlUtil.cpp
	SqlUtil.h
)

target_include_directories(mybooktally_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(mybooktally_core PUBLIC Qt6::Core Qt6::Sql)

# Exports record which app version wrote them, so the core needs to know it.
target_compile_definitions(mybooktally_core PUBLIC
	MYBOOKTALLY_VERSION="${PROJECT_VERSION}"
)
