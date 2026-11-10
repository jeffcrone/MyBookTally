#include "BookRepository.h"

#include "Database.h"
#include "DatabaseError.h"
#include "Isbn.h"
#include "SqlUtil.h"

#include <QSet>

namespace mbt {

namespace {

void requireTitle(const Book& book)
{
	if (book.title.trimmed().isEmpty())
		throw DatabaseError(QStringLiteral("A book needs a title."));
	if (book.pageCount && *book.pageCount <= 0)
		throw DatabaseError(QStringLiteral("The number of pages must be more than zero."));
}

// Binds the columns shared by INSERT and UPDATE.
void bindBookFields(QSqlQuery& query, const Book& book)
{
	query.bindValue(QStringLiteral(":id"), book.id);
	query.bindValue(QStringLiteral(":title"), book.title.trimmed());
	query.bindValue(QStringLiteral(":release_date"), nullIfEmpty(book.releaseDate.trimmed()));
	query.bindValue(QStringLiteral(":isbn"), nullIfEmpty(Isbn::normalize(book.isbn)));
	query.bindValue(QStringLiteral(":page_count"), nullIfUnset(book.pageCount));
	query.bindValue(QStringLiteral(":book_type_id"), nullIfEmpty(book.bookTypeId));
	query.bindValue(QStringLiteral(":ownership_status_id"), nullIfEmpty(book.ownershipStatusId));
	query.bindValue(QStringLiteral(":reading_status_id"), nullIfEmpty(book.readingStatusId));
	query.bindValue(QStringLiteral(":location"), nullIfEmpty(book.location.trimmed()));
	query.bindValue(QStringLiteral(":description"), nullIfEmpty(book.description));
	query.bindValue(QStringLiteral(":cover_image"), nullIfEmpty(book.coverImage));
	query.bindValue(QStringLiteral(":is_private"), book.isPrivate ? 1 : 0);
	query.bindValue(QStringLiteral(":updated_at"), book.updatedAt);
}

QList<QString> readIds(QSqlQuery& query)
{
	execQuery(query);
	QList<QString> ids;
	while (query.next())
		ids.append(query.value(0).toString());
	return ids;
}

// The SELECT shared by both forms of summaries(). The scalar subqueries
// collapse each book's linked names into one comma-separated string. The
// inner ORDER BY decides the order of the names; SQLite's group_concat()
// keeps the order its input rows arrive in.
//
// The series label shows "#" plus the position label if there is one,
// otherwise the position number (as a whole number when it is one, so 2.0
// shows as "#2" but 1.5 stays "#1.5").
const char* const summarySelect = R"sql(
	SELECT
		b.id,
		b.title,
		(SELECT group_concat(name, ', ') FROM (
			SELECT a.name AS name
			FROM book_authors ba
			JOIN authors a ON a.id = ba.author_id
			WHERE ba.book_id = b.id
			ORDER BY ba.position)) AS authors,
		(SELECT group_concat(label, ', ') FROM (
			SELECT s.name ||
				CASE
					WHEN bs.position_label IS NOT NULL AND bs.position_label <> ''
						THEN ' #' || bs.position_label
					WHEN bs.position IS NOT NULL AND bs.position = CAST(bs.position AS INTEGER)
						THEN ' #' || CAST(bs.position AS INTEGER)
					WHEN bs.position IS NOT NULL
						THEN ' #' || bs.position
					ELSE ''
				END AS label
			FROM book_series bs
			JOIN series s ON s.id = bs.series_id
			WHERE bs.book_id = b.id
			ORDER BY s.name COLLATE NOCASE)) AS series,
		(SELECT group_concat(name, ', ') FROM (
			SELECT g.name AS name
			FROM book_genres bg
			JOIN genres g ON g.id = bg.genre_id
			WHERE bg.book_id = b.id
			ORDER BY g.name COLLATE NOCASE)) AS genres,
		b.release_date,
		b.isbn,
		b.page_count,
		bt.name AS book_type,
		os.name AS ownership_status,
		rs.name AS reading_status,
		b.location,
		b.description,
		(SELECT avg(r.rating_value * 5.0 / r.rating_scale)
			FROM ratings r WHERE r.book_id = b.id) AS average_rating,
		(SELECT count(*) FROM ratings r WHERE r.book_id = b.id) AS rating_count,
		b.is_private
	FROM books b
	LEFT JOIN book_types bt ON bt.id = b.book_type_id
	LEFT JOIN ownership_statuses os ON os.id = b.ownership_status_id
	LEFT JOIN reading_statuses rs ON rs.id = b.reading_status_id
)sql";

} // namespace

BookRepository::BookRepository(const Database& database)
	: m_database(database)
{
}

void BookRepository::insert(Book& book) const
{
	requireTitle(book);
	if (book.id.isEmpty())
		book.id = newId();
	book.createdAt = nowUtc();
	book.updatedAt = book.createdAt;

	const QSqlDatabase db = m_database.connection();
	Transaction transaction(db);

	QSqlQuery query = prepareQuery(db, QStringLiteral(R"sql(
		INSERT INTO books (
			id, title, release_date, isbn, page_count,
			book_type_id, ownership_status_id, reading_status_id,
			location, description, cover_image, is_private,
			created_at, updated_at)
		VALUES (
			:id, :title, :release_date, :isbn, :page_count,
			:book_type_id, :ownership_status_id, :reading_status_id,
			:location, :description, :cover_image, :is_private,
			:created_at, :updated_at)
	)sql"));
	bindBookFields(query, book);
	query.bindValue(QStringLiteral(":created_at"), book.createdAt);
	execQuery(query);

	writeLinks(book);
	transaction.commit();
}

void BookRepository::update(Book& book) const
{
	requireTitle(book);
	book.updatedAt = nowUtc();

	const QSqlDatabase db = m_database.connection();
	Transaction transaction(db);

	QSqlQuery query = prepareQuery(db, QStringLiteral(R"sql(
		UPDATE books SET
			title = :title,
			release_date = :release_date,
			isbn = :isbn,
			page_count = :page_count,
			book_type_id = :book_type_id,
			ownership_status_id = :ownership_status_id,
			reading_status_id = :reading_status_id,
			location = :location,
			description = :description,
			cover_image = :cover_image,
			is_private = :is_private,
			updated_at = :updated_at
		WHERE id = :id
	)sql"));
	bindBookFields(query, book);
	execQuery(query);
	if (query.numRowsAffected() == 0)
		throw DatabaseError(QStringLiteral("That book no longer exists."));

	// Replace the links wholesale. Working out which links were added and
	// which removed would save a few writes, but a book has a handful of
	// links at most, and "delete all, insert current" cannot get out of
	// step with what the editor showed.
	for (const char* table : {"book_authors", "book_series", "book_genres"}) {
		QSqlQuery remove = prepareQuery(db,
			QStringLiteral("DELETE FROM %1 WHERE book_id = :book_id").arg(QLatin1String(table)));
		remove.bindValue(QStringLiteral(":book_id"), book.id);
		execQuery(remove);
	}
	writeLinks(book);

	transaction.commit();
}

void BookRepository::writeLinks(const Book& book) const
{
	const QSqlDatabase db = m_database.connection();

	// Duplicates are skipped rather than left to fail on the primary key,
	// so an editor that lists the same author twice does not stop the save.
	QSet<QString> seen;
	int position = 0;
	for (const QString& authorId : book.authorIds) {
		if (seen.contains(authorId))
			continue;
		seen.insert(authorId);
		QSqlQuery query = prepareQuery(db, QStringLiteral(
			"INSERT INTO book_authors (book_id, author_id, position) "
			"VALUES (:book_id, :author_id, :position)"));
		query.bindValue(QStringLiteral(":book_id"), book.id);
		query.bindValue(QStringLiteral(":author_id"), authorId);
		query.bindValue(QStringLiteral(":position"), position++);
		execQuery(query);
	}

	seen.clear();
	for (const SeriesEntry& entry : book.series) {
		if (seen.contains(entry.seriesId))
			continue;
		seen.insert(entry.seriesId);
		QSqlQuery query = prepareQuery(db, QStringLiteral(
			"INSERT INTO book_series (book_id, series_id, position, position_label) "
			"VALUES (:book_id, :series_id, :position, :position_label)"));
		query.bindValue(QStringLiteral(":book_id"), book.id);
		query.bindValue(QStringLiteral(":series_id"), entry.seriesId);
		query.bindValue(QStringLiteral(":position"), nullIfUnset(entry.position));
		query.bindValue(QStringLiteral(":position_label"), nullIfEmpty(entry.positionLabel.trimmed()));
		execQuery(query);
	}

	seen.clear();
	for (const QString& genreId : book.genreIds) {
		if (seen.contains(genreId))
			continue;
		seen.insert(genreId);
		QSqlQuery query = prepareQuery(db, QStringLiteral(
			"INSERT INTO book_genres (book_id, genre_id) VALUES (:book_id, :genre_id)"));
		query.bindValue(QStringLiteral(":book_id"), book.id);
		query.bindValue(QStringLiteral(":genre_id"), genreId);
		execQuery(query);
	}
}

bool BookRepository::remove(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("DELETE FROM books WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	return query.numRowsAffected() > 0;
}

std::optional<Book> BookRepository::find(const QString& id) const
{
	const QSqlDatabase db = m_database.connection();

	QSqlQuery query = prepareQuery(db, QStringLiteral(R"sql(
		SELECT id, title, release_date, isbn, page_count,
			book_type_id, ownership_status_id, reading_status_id,
			location, description, cover_image, is_private,
			created_at, updated_at
		FROM books
		WHERE id = :id
	)sql"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	if (!query.next())
		return std::nullopt;

	Book book;
	book.id = query.value(0).toString();
	book.title = query.value(1).toString();
	book.releaseDate = query.value(2).toString();
	book.isbn = query.value(3).toString();
	book.pageCount = optionalInt(query.value(4));
	book.bookTypeId = query.value(5).toString();
	book.ownershipStatusId = query.value(6).toString();
	book.readingStatusId = query.value(7).toString();
	book.location = query.value(8).toString();
	book.description = query.value(9).toString();
	book.coverImage = query.value(10).toByteArray();
	book.isPrivate = query.value(11).toInt() != 0;
	book.createdAt = query.value(12).toString();
	book.updatedAt = query.value(13).toString();

	QSqlQuery authors = prepareQuery(db, QStringLiteral(
		"SELECT author_id FROM book_authors WHERE book_id = :book_id ORDER BY position"));
	authors.bindValue(QStringLiteral(":book_id"), id);
	book.authorIds = readIds(authors);

	QSqlQuery series = prepareQuery(db, QStringLiteral(R"sql(
		SELECT bs.series_id, s.name, bs.position, bs.position_label
		FROM book_series bs
		JOIN series s ON s.id = bs.series_id
		WHERE bs.book_id = :book_id
		ORDER BY s.name COLLATE NOCASE
	)sql"));
	series.bindValue(QStringLiteral(":book_id"), id);
	execQuery(series);
	while (series.next()) {
		SeriesEntry entry;
		entry.seriesId = series.value(0).toString();
		entry.seriesName = series.value(1).toString();
		entry.position = optionalDouble(series.value(2));
		entry.positionLabel = series.value(3).toString();
		book.series.append(entry);
	}

	QSqlQuery genres = prepareQuery(db, QStringLiteral(R"sql(
		SELECT bg.genre_id
		FROM book_genres bg
		JOIN genres g ON g.id = bg.genre_id
		WHERE bg.book_id = :book_id
		ORDER BY g.name COLLATE NOCASE
	)sql"));
	genres.bindValue(QStringLiteral(":book_id"), id);
	book.genreIds = readIds(genres);

	return book;
}

QList<BookSummary> BookRepository::summaries(bool includePrivate, const QString& listId) const
{
	QString sql = QLatin1String(summarySelect);
	if (listId.isEmpty()) {
		sql += QStringLiteral(
			"WHERE (:include_private = 1 OR b.is_private = 0) "
			"ORDER BY b.title COLLATE NOCASE");
	} else {
		// In a list, books appear in the order they were added.
		sql += QStringLiteral(
			"JOIN list_books lb ON lb.book_id = b.id "
			"WHERE lb.list_id = :list_id "
			"ORDER BY lb.position");
	}

	QSqlQuery query = prepareQuery(m_database.connection(), sql);
	if (listId.isEmpty())
		query.bindValue(QStringLiteral(":include_private"), includePrivate ? 1 : 0);
	else
		query.bindValue(QStringLiteral(":list_id"), listId);
	execQuery(query);

	QList<BookSummary> rows;
	while (query.next()) {
		BookSummary row;
		row.id = query.value(0).toString();
		row.title = query.value(1).toString();
		row.authors = query.value(2).toString();
		row.series = query.value(3).toString();
		row.genres = query.value(4).toString();
		row.releaseDate = query.value(5).toString();
		row.isbn = query.value(6).toString();
		row.pageCount = optionalInt(query.value(7));
		row.bookType = query.value(8).toString();
		row.ownershipStatus = query.value(9).toString();
		row.readingStatus = query.value(10).toString();
		row.location = query.value(11).toString();
		row.description = query.value(12).toString();
		row.averageRating = optionalDouble(query.value(13));
		row.ratingCount = query.value(14).toInt();
		row.isPrivate = query.value(15).toInt() != 0;
		rows.append(row);
	}
	return rows;
}

QList<QString> BookRepository::findIdsByIsbn(const QString& isbn) const
{
	const QString normalized = Isbn::normalize(isbn);
	if (normalized.isEmpty())
		return {};
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("SELECT id FROM books WHERE isbn = :isbn ORDER BY id"));
	query.bindValue(QStringLiteral(":isbn"), normalized);
	return readIds(query);
}

QList<QString> BookRepository::findIdsByTitle(const QString& title) const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT id FROM books WHERE title = :title COLLATE NOCASE ORDER BY id"));
	query.bindValue(QStringLiteral(":title"), title.trimmed());
	return readIds(query);
}

QList<QString> BookRepository::allIds() const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("SELECT id FROM books ORDER BY title COLLATE NOCASE, id"));
	return readIds(query);
}

} // namespace mbt
