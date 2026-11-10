#pragma once

#include <QJsonDocument>
#include <QList>
#include <QString>
#include <QStringList>

namespace mbt {

class Database;

struct ExportOptions
{
	// true: every book, author, series, genre, status, review, rating, and
	// list carries its GUID, so importing the file back into this or another
	// MyBookTally library updates the same records instead of duplicating
	// them. false: GUIDs are left out and links are by name only, which is
	// easier to read and to produce by hand or from another program.
	bool includeIds = true;
	// Covers are stored as base64 text, which can make the file many times
	// larger. Leaving them out keeps an export small and readable.
	bool includeCovers = true;
};

struct ImportResult
{
	int booksAdded = 0;
	int booksUpdated = 0;
	// Books that could not be imported, and other things the user should
	// know about. One line each.
	QStringList warnings;
};

// Export to and import from MyBookTally's JSON file format.
//
// The format is a single object:
//
//   {
//     "format": "MyBookTally",
//     "formatVersion": 1,
//     "appVersion": "0.0.2",
//     "exportedAt": "2026-11-10T14:00:00.000Z",
//     "includesIds": true,
//     "books": [ { ...one object per book... } ]
//   }
//
// Each book object holds the book's fields, and its linked records embedded
// as objects ("authors": [{"id": ..., "name": ..., "bio": ...}], and so on)
// rather than as bare foreign keys. That way one file is self-contained: it
// can be imported into an empty library and recreate everything the books
// need. README.md describes every field.
//
// How import decides whether a record already exists:
// - A record with an "id" that exists in the library is that record.
// - Otherwise a linked record (author, genre, ...) is matched by name,
//   ignoring case, and created if there is no match (keeping the file's id
//   if it has one).
// - Otherwise a book is matched by ISBN, then by title plus the same
//   authors, and added as new if neither matches.
// A matched book is overwritten with the file's version of its fields and
// links; its reviews, ratings, and list memberships are merged (added to,
// never removed). A book's cover is kept when the file has no "coverImage".
class JsonExchange
{
public:
	static constexpr int FormatVersion = 1;

	explicit JsonExchange(const Database& database);

	// Exports the given books in the given order.
	QJsonDocument exportBooks(const QList<QString>& bookIds, const ExportOptions& options) const;

	// Imports every book in document. Each book is imported on its own: one
	// that fails is reported in ImportResult::warnings and skipped, and the
	// rest still go in. Throws DatabaseError if the document is not a
	// MyBookTally export at all, or is from a newer format version.
	ImportResult importBooks(const QJsonDocument& document) const;

private:
	const Database& m_database;
};

} // namespace mbt
