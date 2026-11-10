#pragma once

#include "Entities.h"

#include <QList>
#include <QString>

#include <optional>

namespace mbt {

class Database;

// Reads and writes books together with their links to authors, series, and
// genres (the book_authors, book_series, and book_genres junction tables).
//
// A book and its links are always saved together in one transaction: the
// editor shows them as one thing, and a half-saved book (fields updated,
// authors not) would be worse than either the old or the new version.
class BookRepository
{
public:
	explicit BookRepository(const Database& database);

	// Inserts book and its links. Gives it a new id if it has none, sets
	// createdAt/updatedAt, and writes those back into book. The ISBN is
	// stored normalized (see Isbn::normalize()).
	void insert(Book& book) const;

	// Replaces the stored book with book: every field, and the full set of
	// links (links not in book are removed). Updates book.updatedAt.
	// Throws DatabaseError if no book has book.id.
	void update(Book& book) const;

	// Deletes the book. Its links, reviews, ratings, and list memberships go
	// with it (ON DELETE CASCADE). Returns false if there was no such book.
	bool remove(const QString& id) const;

	// The book with all its links, or nothing.
	std::optional<Book> find(const QString& id) const;

	// Rows for the main table.
	// - With listId empty: every book in the library, leaving out private
	//   books unless includePrivate is true.
	// - With listId set: the books in that list. Private books are shown
	//   there regardless, since the user put them in the list on purpose.
	QList<BookSummary> summaries(bool includePrivate, const QString& listId = QString()) const;

	// Ids of books whose (normalized) ISBN matches. Used by import to spot a
	// book that already exists.
	QList<QString> findIdsByIsbn(const QString& isbn) const;

	// Ids of books with this exact title, ignoring case.
	QList<QString> findIdsByTitle(const QString& title) const;

	// Every book id, sorted by title. Used by export.
	QList<QString> allIds() const;

private:
	void writeLinks(const Book& book) const;

	const Database& m_database;
};

} // namespace mbt
