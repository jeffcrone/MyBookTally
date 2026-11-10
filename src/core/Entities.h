#pragma once

// Plain data structures that mirror the rows of the SQLite tables. They hold
// no behavior; the repositories read and write them.
//
// Every ID is a GUID stored as text (for example
// "3f2b8c1e-5a7d-4e0b-9c1a-2d4e6f8a0b1c"). GUIDs rather than auto-increment
// integers so that books exported from one database and imported into
// another - or, later, synced with the website - keep their identity without
// colliding with rows that already exist there.
//
// Optional text fields use an empty QString for "not set"; the repositories
// store that as NULL. Optional numbers use std::optional, because 0 can be a
// meaningful value where an empty string cannot.

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace mbt {

// A row of one of the simple "name (+ description)" tables: series, genres,
// book_types, ownership_statuses, reading_statuses. Description is always
// empty for the tables that have no description column.
struct LookupItem
{
	QString id;
	QString name;
	QString description;
};

struct Author
{
	QString id;
	QString name;
	QString bio;
	// Free text, normally "YYYY-MM-DD", but a bare year is accepted because
	// that is often all that is known.
	QString birthDate;
};

// One book's membership in one series (a row of book_series).
struct SeriesEntry
{
	QString seriesId;
	// Filled in when a book is read back, for display. Ignored when saving.
	QString seriesName;
	// The number used to sort books within the series. A real number so a
	// book written later can be slotted in between, as 1.5.
	std::optional<double> position;
	// What to show for the position, when that is not simply the number:
	// "Prequel", "1.5", "Book Two".
	QString positionLabel;
};

struct Book
{
	QString id;
	QString title;
	// In credit order; the first is the primary author.
	QList<QString> authorIds;
	QList<SeriesEntry> series;
	QList<QString> genreIds;
	// "YYYY" or "YYYY-MM-DD".
	QString releaseDate;
	// Digits only (plus a trailing X for some ISBN-10s); see Isbn::normalize().
	QString isbn;
	std::optional<int> pageCount;
	QString bookTypeId;
	QString ownershipStatusId;
	QString readingStatusId;
	// Where the book is: "Living room shelf", "Storage box 3", or - for a
	// book the user wants - "Downtown bookstore".
	QString location;
	QString description;
	// Raw image file bytes (PNG/JPEG), or empty for no cover.
	QByteArray coverImage;
	// Private books are hidden from the main library list.
	bool isPrivate = false;
	// ISO 8601 UTC timestamps, set by the repository.
	QString createdAt;
	QString updatedAt;
};

struct Review
{
	QString id;
	QString bookId;
	QString text;
	// Who wrote the review; empty means the user themselves.
	QString reviewer;
	QString createdAt;
};

struct Rating
{
	QString id;
	QString bookId;
	// The score and what it is out of: 4 and 5 mean "4/5". Stored as two
	// numbers rather than the text "4/5" so ratings on different scales can
	// still be averaged and sorted.
	double value = 0.0;
	double scale = 5.0;
	QString reviewer;
	QString createdAt;
};

// A user-created list of books ("Favorites", "Summer reading").
struct BookList
{
	QString id;
	QString name;
	QString description;
	QString createdAt;
	// Filled in by ListRepository::all() for display; ignored when saving.
	int bookCount = 0;
};

// One row of the main book table: a book with the names of everything it
// links to already resolved, ready to display and sort.
struct BookSummary
{
	QString id;
	QString title;
	QString authors;
	QString series;
	QString genres;
	QString releaseDate;
	QString isbn;
	std::optional<int> pageCount;
	QString bookType;
	QString ownershipStatus;
	QString readingStatus;
	QString location;
	QString description;
	// The mean of all the book's ratings, each converted to a scale of 5.
	std::optional<double> averageRating;
	int ratingCount = 0;
	bool isPrivate = false;
};

} // namespace mbt
