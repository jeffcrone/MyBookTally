#include "Schema.h"

#include "DatabaseError.h"
#include "SqlUtil.h"

#include <QStringList>

namespace mbt::Schema {

namespace {

// Version 1: the original layout.
//
// Conventions used throughout:
// - id columns are TEXT GUIDs (see Entities.h for why).
// - Names that the user picks from a drop-down (genres, series, book types,
//   statuses, lists) are UNIQUE COLLATE NOCASE, so "Fantasy" and "fantasy"
//   cannot both exist and confuse the picker. Author names are deliberately
//   not unique: two different people can share a name.
// - Junction tables use ON DELETE CASCADE on both sides, so deleting a book
//   or an author removes the link without touching the other side.
// - Optional single links from books (type and statuses) use ON DELETE SET
//   NULL, so deleting "Audiobook" leaves the books but clears their type.
const QStringList version1Statements = {
	QStringLiteral(R"sql(
		CREATE TABLE authors (
			id          TEXT PRIMARY KEY NOT NULL,
			name        TEXT NOT NULL CHECK (length(trim(name)) > 0),
			bio         TEXT,
			birth_date  TEXT
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE series (
			id           TEXT PRIMARY KEY NOT NULL,
			name         TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK (length(trim(name)) > 0),
			description  TEXT
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE genres (
			id           TEXT PRIMARY KEY NOT NULL,
			name         TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK (length(trim(name)) > 0),
			description  TEXT
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE book_types (
			id    TEXT PRIMARY KEY NOT NULL,
			name  TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK (length(trim(name)) > 0)
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE ownership_statuses (
			id    TEXT PRIMARY KEY NOT NULL,
			name  TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK (length(trim(name)) > 0)
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE reading_statuses (
			id    TEXT PRIMARY KEY NOT NULL,
			name  TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK (length(trim(name)) > 0)
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE books (
			id                   TEXT PRIMARY KEY NOT NULL,
			title                TEXT NOT NULL CHECK (length(trim(title)) > 0),
			release_date         TEXT,
			isbn                 TEXT,
			page_count           INTEGER CHECK (page_count IS NULL OR page_count > 0),
			book_type_id         TEXT REFERENCES book_types(id) ON DELETE SET NULL,
			ownership_status_id  TEXT REFERENCES ownership_statuses(id) ON DELETE SET NULL,
			reading_status_id    TEXT REFERENCES reading_statuses(id) ON DELETE SET NULL,
			location             TEXT,
			description          TEXT,
			cover_image          BLOB,
			is_private           INTEGER NOT NULL DEFAULT 0 CHECK (is_private IN (0, 1)),
			created_at           TEXT NOT NULL,
			updated_at           TEXT NOT NULL
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE book_authors (
			book_id    TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
			author_id  TEXT NOT NULL REFERENCES authors(id) ON DELETE CASCADE,
			position   INTEGER NOT NULL,
			PRIMARY KEY (book_id, author_id)
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE book_series (
			book_id         TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
			series_id       TEXT NOT NULL REFERENCES series(id) ON DELETE CASCADE,
			position        REAL,
			position_label  TEXT,
			PRIMARY KEY (book_id, series_id)
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE book_genres (
			book_id   TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
			genre_id  TEXT NOT NULL REFERENCES genres(id) ON DELETE CASCADE,
			PRIMARY KEY (book_id, genre_id)
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE reviews (
			id           TEXT PRIMARY KEY NOT NULL,
			book_id      TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
			review_text  TEXT NOT NULL CHECK (length(trim(review_text)) > 0),
			reviewer     TEXT,
			created_at   TEXT NOT NULL
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE ratings (
			id            TEXT PRIMARY KEY NOT NULL,
			book_id       TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
			rating_value  REAL NOT NULL,
			rating_scale  REAL NOT NULL DEFAULT 5,
			reviewer      TEXT,
			created_at    TEXT NOT NULL,
			CHECK (rating_scale > 0 AND rating_value >= 0 AND rating_value <= rating_scale)
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE lists (
			id           TEXT PRIMARY KEY NOT NULL,
			name         TEXT NOT NULL UNIQUE COLLATE NOCASE CHECK (length(trim(name)) > 0),
			description  TEXT,
			created_at   TEXT NOT NULL
		)
	)sql"),
	QStringLiteral(R"sql(
		CREATE TABLE list_books (
			list_id   TEXT NOT NULL REFERENCES lists(id) ON DELETE CASCADE,
			book_id   TEXT NOT NULL REFERENCES books(id) ON DELETE CASCADE,
			position  INTEGER NOT NULL,
			added_at  TEXT NOT NULL,
			PRIMARY KEY (list_id, book_id)
		)
	)sql"),

	// The junction tables' primary keys already index lookups by book. These
	// cover the other direction ("which books use this genre?"), which the
	// usage counts and cascading deletes need, plus the columns the app
	// searches and sorts by.
	QStringLiteral("CREATE INDEX idx_book_authors_author ON book_authors(author_id)"),
	QStringLiteral("CREATE INDEX idx_book_series_series ON book_series(series_id)"),
	QStringLiteral("CREATE INDEX idx_book_genres_genre ON book_genres(genre_id)"),
	QStringLiteral("CREATE INDEX idx_reviews_book ON reviews(book_id)"),
	QStringLiteral("CREATE INDEX idx_ratings_book ON ratings(book_id)"),
	QStringLiteral("CREATE INDEX idx_list_books_book ON list_books(book_id)"),
	QStringLiteral("CREATE INDEX idx_books_title ON books(title COLLATE NOCASE)"),
	QStringLiteral("CREATE INDEX idx_books_isbn ON books(isbn)"),
	QStringLiteral("CREATE INDEX idx_authors_name ON authors(name COLLATE NOCASE)"),
};

// Starting values for the drop-down tables, so a new library is usable
// without visiting the Manage menu first. The user can rename or delete any
// of them.
void seedVersion1(const QSqlDatabase& db)
{
	struct Seed
	{
		const char* table;
		QStringList names;
	};
	const QList<Seed> seeds = {
		{"book_types", {QStringLiteral("Hardcover"), QStringLiteral("Paperback"),
			QStringLiteral("Mass Market Paperback"), QStringLiteral("E-book"),
			QStringLiteral("Audiobook")}},
		{"ownership_statuses", {QStringLiteral("Have"), QStringLiteral("Want"),
			QStringLiteral("Borrowed"), QStringLiteral("Lent Out")}},
		{"reading_statuses", {QStringLiteral("To Read"), QStringLiteral("Reading"),
			QStringLiteral("Read"), QStringLiteral("Did Not Finish")}},
	};
	for (const Seed& seed : seeds) {
		for (const QString& name : seed.names) {
			QSqlQuery query = prepareQuery(db,
				QStringLiteral("INSERT INTO %1 (id, name) VALUES (:id, :name)")
					.arg(QLatin1String(seed.table)));
			query.bindValue(QStringLiteral(":id"), newId());
			query.bindValue(QStringLiteral(":name"), name);
			execQuery(query);
		}
	}
}

void upgradeToVersion1(const QSqlDatabase& db)
{
	for (const QString& statement : version1Statements)
		execSql(db, statement);
	seedVersion1(db);
}

} // namespace

int version(const QSqlDatabase& db)
{
	QSqlQuery query = prepareQuery(db, QStringLiteral("PRAGMA user_version"));
	execQuery(query);
	return query.next() ? query.value(0).toInt() : 0;
}

void migrate(const QSqlDatabase& db)
{
	const int current = version(db);
	if (current > CurrentVersion)
		throw DatabaseError(QStringLiteral(
			"This library was saved by a newer version of MyBookTally "
			"(database schema %1; this version understands up to %2). "
			"Please update MyBookTally to open it.")
				.arg(current).arg(CurrentVersion));

	// One entry per schema version; entry N upgrades from N-1 to N. A
	// future version 2 adds upgradeToVersion2 here and bumps CurrentVersion.
	using Step = void (*)(const QSqlDatabase&);
	const Step steps[] = {upgradeToVersion1};

	for (int target = current + 1; target <= CurrentVersion; ++target) {
		Transaction transaction(db);
		steps[target - 1](db);
		// PRAGMA does not accept bound parameters, hence the formatted
		// string; target is an int from this code, never user input.
		execSql(db, QStringLiteral("PRAGMA user_version = %1").arg(target));
		transaction.commit();
	}
}

} // namespace mbt::Schema
