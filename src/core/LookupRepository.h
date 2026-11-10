#pragma once

#include "Entities.h"

#include <QList>
#include <QString>

#include <optional>

namespace mbt {

class Database;

// The five small tables whose rows a book links to by picking from a list.
// They share one shape (id, name, and for some a description), so one
// repository serves them all, parameterized by kind, instead of five nearly
// identical copies of the same queries.
enum class LookupKind
{
	Series,
	Genre,
	BookType,
	OwnershipStatus,
	ReadingStatus,
};

// Every kind, in the order the Manage menu lists them.
const QList<LookupKind>& allLookupKinds();

// Human-readable names for menus and dialogs: "Genre" / "Genres".
QString lookupKindSingular(LookupKind kind);
QString lookupKindPlural(LookupKind kind);

// Whether rows of this kind have a description column.
bool lookupKindHasDescription(LookupKind kind);

class LookupRepository
{
public:
	LookupRepository(const Database& database, LookupKind kind);

	LookupKind kind() const { return m_kind; }

	// All rows, sorted by name ignoring case.
	QList<LookupItem> all() const;
	std::optional<LookupItem> find(const QString& id) const;
	// Case-insensitive exact match.
	std::optional<LookupItem> findByName(const QString& name) const;

	// Inserts item, giving it a new id first if it has none, and returns the
	// id. Throws DatabaseError if the name is empty or already used.
	QString insert(LookupItem item) const;
	// Throws DatabaseError if no row has item.id.
	void update(const LookupItem& item) const;
	// Returns false if there was no such row. Books that used the row lose
	// the link (see the ON DELETE rules in Schema.cpp); the books remain.
	bool remove(const QString& id) const;

	// How many books link to this row. The Manage dialog shows this before
	// deleting, so the user knows what the delete will affect.
	int usageCount(const QString& id) const;

	// The id of the row called name, creating it if there is none. Used when
	// the user types a new genre straight into the book editor, and by
	// import.
	QString findOrCreate(const QString& name) const;

private:
	const Database& m_database;
	LookupKind m_kind;
};

class AuthorRepository
{
public:
	explicit AuthorRepository(const Database& database);

	QList<Author> all() const;
	std::optional<Author> find(const QString& id) const;
	// The first author with this name, ignoring case. Author names are not
	// unique, so this is a best guess, used only where nothing better is
	// available (typing a name in the editor, importing by name).
	std::optional<Author> findByName(const QString& name) const;

	QString insert(Author author) const;
	void update(const Author& author) const;
	bool remove(const QString& id) const;
	int usageCount(const QString& id) const;
	QString findOrCreate(const QString& name) const;

private:
	const Database& m_database;
};

} // namespace mbt
