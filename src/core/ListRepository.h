#pragma once

#include "Entities.h"

#include <QList>
#include <QString>

#include <optional>

namespace mbt {

class Database;

// User-created lists of books, and which books are in each.
//
// A list holds references to books in the library; it never owns them.
// Removing a book from a list leaves the book in the library, and deleting a
// list deletes only the list and its memberships.
class ListRepository
{
public:
	explicit ListRepository(const Database& database);

	// All lists sorted by name, each with its bookCount filled in.
	QList<BookList> all() const;
	std::optional<BookList> find(const QString& id) const;
	std::optional<BookList> findByName(const QString& name) const;

	// Throws DatabaseError if the name is empty or another list has it.
	QString create(BookList list) const;
	// Changes the name and description.
	void update(const BookList& list) const;
	bool remove(const QString& id) const;
	QString findOrCreate(const QString& name) const;

	// Adds the book at the end of the list. Returns false if it was already
	// there (in which case nothing changes).
	bool addBook(const QString& listId, const QString& bookId) const;
	bool removeBook(const QString& listId, const QString& bookId) const;
	// In the order they were added.
	QList<QString> bookIds(const QString& listId) const;
	// The lists a book is in, sorted by name.
	QList<BookList> listsForBook(const QString& bookId) const;

private:
	const Database& m_database;
};

} // namespace mbt
