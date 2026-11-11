#pragma once

// Building Open Library (https://openlibrary.org) request URLs and reading
// its JSON replies. No networking here - OpenLibraryClient does that - so
// every rule about what is asked for and how the answer is read can be
// tested without a connection.
//
// How a lookup works:
//  1. search.json with whatever the user typed. Each result is a "work" (the
//     book in the abstract) together with its one edition that best matches
//     the search, so the list shows real ISBNs and covers of real printings.
//  2. Once the user picks one, that edition's record (/books/OL...M.json)
//     for the page count, format, and publish date, then the work's record
//     (/works/OL...W.json) for the description and subjects when the
//     edition has none, then the large cover image.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace mbt {

// What the user knows about the book, taken from the Add Book form.
struct OpenLibraryQuery
{
	QString title;
	QString author;
	QString isbn;

	bool isEmpty() const
	{
		return title.trimmed().isEmpty() && author.trimmed().isEmpty() && isbn.trimmed().isEmpty();
	}
	// "title "Dune", author "Herbert"" - for log entries and the dialog.
	QString describe() const;
};

// One row of the search results.
struct OpenLibraryMatch
{
	// "/works/OL27482W" and "/books/OL10236414M". The edition key is empty
	// when Open Library has a work with no editions recorded.
	QString workKey;
	QString editionKey;
	QString title;
	QStringList authors;
	// Digits only. The ISBN the user searched for if this edition has it,
	// otherwise its first ISBN-13, otherwise its first ISBN of any kind.
	QString isbn;
	// As Open Library has it ("1997 November 3"); see normalizeDate().
	QString publishDate;
	// Covers are fetched by their numeric id; 0 means none.
	int coverId = 0;
};

// Everything a lookup found that the Add Book form has a place for.
struct OpenLibraryBook
{
	QString title;
	QStringList authors;
	QString isbn;
	// Already in the app's format: "YYYY", "YYYY-MM" or "YYYY-MM-DD".
	QString releaseDate;
	std::optional<int> pageCount;
	// "Paperback", "Hardcover"... as Open Library has it; see
	// OpenLibrary::bookTypeNameFor().
	QString physicalFormat;
	QString description;
	// Open Library's subjects mix genres ("Fantasy") with every character
	// and place in the book, so the editor only uses the ones that match a
	// genre the user already has.
	QStringList subjects;
	int coverId = 0;
	// The downloaded cover file, before it is scaled for storage.
	QByteArray coverImage;
};

namespace OpenLibrary {

// Where requests go. OpenLibraryClient can be pointed elsewhere for tests.
QUrl defaultApiBase();
QUrl defaultCoversBase();

// How many results to ask for. Enough to find the right book among
// similarly named ones, while the covers (which load one per second) still
// all arrive in reasonable time.
constexpr int SearchLimit = 20;

// The search.json URL for query. A well-formed ISBN is searched on its own:
// it identifies the book, and a title typed slightly differently from Open
// Library's would otherwise rule the right answer out. Without one, the
// title and author are searched as separate fields, which is more precise
// than one free-text query.
QUrl searchUrl(const QUrl& apiBase, const OpenLibraryQuery& query);

// The JSON record of a work or edition, from its key ("/works/OL27482W").
// Returns an empty URL for anything that is not such a key, so a strange
// reply can never steer a request somewhere unexpected.
QUrl recordUrl(const QUrl& apiBase, const QString& key);

enum class CoverSize { Small, Medium, Large };
QUrl coverUrl(const QUrl& coversBase, int coverId, CoverSize size);

// Reads a search.json reply. Returns false and sets error if it is not the
// JSON Open Library sends. Results with no title are skipped.
bool parseSearchResults(const QByteArray& json, const QString& searchedIsbn,
	QList<OpenLibraryMatch>* matches, QString* error);

// The start of a lookup result, from the search row the user picked.
OpenLibraryBook bookFromMatch(const OpenLibraryMatch& match);

// Fill in book from an edition record or a work record. The edition is
// read first and wins; the work only fills what is still missing (a work
// has no page count or format, and its "first published" date is a
// fallback for an edition with no date).
bool applyEdition(const QByteArray& json, OpenLibraryBook* book, QString* error);
bool applyWork(const QByteArray& json, OpenLibraryBook* book, QString* error);

// Turns the free text Open Library uses for dates ("1997 November 3",
// "Sep 21, 2012", "March 1990", "c1985", "2011") into the app's "YYYY",
// "YYYY-MM" or "YYYY-MM-DD". Keeps as much as it can be sure of: an
// impossible day falls back to the month, and text with no recognizable
// year gives an empty string.
QString normalizeDate(const QString& text);

// The name of the app's seeded book type that a physical format means
// ("Hardback" -> "Hardcover", "Mass Market Paperback", "ebook" ->
// "E-book"), or the format itself, trimmed, when it is none of them, so it
// can still match a type the user made up.
QString bookTypeNameFor(const QString& physicalFormat);

} // namespace OpenLibrary

} // namespace mbt
