#include "LookupRepository.h"

#include "Database.h"
#include "DatabaseError.h"
#include "SqlUtil.h"

namespace mbt {

namespace {

// Where each kind lives, and where books link to it. The table and column
// names are spliced into the SQL text (SQL parameters cannot stand in for
// identifiers); they come only from this fixed table, never from user input.
struct KindInfo
{
	const char* table;
	bool hasDescription;
	// The table and column that point at this kind's id, for usage counts.
	const char* usageTable;
	const char* usageColumn;
	const char* singular;
	const char* plural;
};

KindInfo info(LookupKind kind)
{
	switch (kind) {
	case LookupKind::Series:
		return {"series", true, "book_series", "series_id", "Series", "Series"};
	case LookupKind::Genre:
		return {"genres", true, "book_genres", "genre_id", "Genre", "Genres"};
	case LookupKind::BookType:
		return {"book_types", false, "books", "book_type_id", "Book Type", "Book Types"};
	case LookupKind::OwnershipStatus:
		return {"ownership_statuses", false, "books", "ownership_status_id",
			"Ownership Status", "Ownership Statuses"};
	case LookupKind::ReadingStatus:
		return {"reading_statuses", false, "books", "reading_status_id",
			"Reading Status", "Reading Statuses"};
	}
	Q_UNREACHABLE();
}

// SELECT list for a kind: tables without a description still return a
// (NULL) description column so one row reader works for every kind.
QString selectColumns(const KindInfo& k)
{
	return k.hasDescription ? QStringLiteral("id, name, description")
							: QStringLiteral("id, name, NULL AS description");
}

LookupItem readItem(const QSqlQuery& query)
{
	return {query.value(0).toString(), query.value(1).toString(), query.value(2).toString()};
}

Author readAuthor(const QSqlQuery& query)
{
	return {query.value(0).toString(), query.value(1).toString(),
		query.value(2).toString(), query.value(3).toString()};
}

void requireName(const QString& name, const char* what)
{
	if (name.trimmed().isEmpty())
		throw DatabaseError(QStringLiteral("A %1 needs a name.").arg(QString::fromLatin1(what).toLower()));
}

} // namespace

const QList<LookupKind>& allLookupKinds()
{
	static const QList<LookupKind> kinds = {
		LookupKind::Series,
		LookupKind::Genre,
		LookupKind::BookType,
		LookupKind::OwnershipStatus,
		LookupKind::ReadingStatus,
	};
	return kinds;
}

QString lookupKindSingular(LookupKind kind)
{
	return QString::fromLatin1(info(kind).singular);
}

QString lookupKindPlural(LookupKind kind)
{
	return QString::fromLatin1(info(kind).plural);
}

bool lookupKindHasDescription(LookupKind kind)
{
	return info(kind).hasDescription;
}

LookupRepository::LookupRepository(const Database& database, LookupKind kind)
	: m_database(database)
	, m_kind(kind)
{
}

QList<LookupItem> LookupRepository::all() const
{
	const KindInfo k = info(m_kind);
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("SELECT %1 FROM %2 ORDER BY name COLLATE NOCASE")
			.arg(selectColumns(k), QLatin1String(k.table)));
	execQuery(query);
	QList<LookupItem> items;
	while (query.next())
		items.append(readItem(query));
	return items;
}

std::optional<LookupItem> LookupRepository::find(const QString& id) const
{
	const KindInfo k = info(m_kind);
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("SELECT %1 FROM %2 WHERE id = :id")
			.arg(selectColumns(k), QLatin1String(k.table)));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	if (!query.next())
		return std::nullopt;
	return readItem(query);
}

std::optional<LookupItem> LookupRepository::findByName(const QString& name) const
{
	const KindInfo k = info(m_kind);
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("SELECT %1 FROM %2 WHERE name = :name COLLATE NOCASE LIMIT 1")
			.arg(selectColumns(k), QLatin1String(k.table)));
	query.bindValue(QStringLiteral(":name"), name.trimmed());
	execQuery(query);
	if (!query.next())
		return std::nullopt;
	return readItem(query);
}

QString LookupRepository::insert(LookupItem item) const
{
	const KindInfo k = info(m_kind);
	requireName(item.name, k.singular);
	if (item.id.isEmpty())
		item.id = newId();

	QSqlQuery query = prepareQuery(m_database.connection(), k.hasDescription
		? QStringLiteral("INSERT INTO %1 (id, name, description) VALUES (:id, :name, :description)")
			.arg(QLatin1String(k.table))
		: QStringLiteral("INSERT INTO %1 (id, name) VALUES (:id, :name)")
			.arg(QLatin1String(k.table)));
	query.bindValue(QStringLiteral(":id"), item.id);
	query.bindValue(QStringLiteral(":name"), item.name.trimmed());
	if (k.hasDescription)
		query.bindValue(QStringLiteral(":description"), nullIfEmpty(item.description));
	execQuery(query);
	return item.id;
}

void LookupRepository::update(const LookupItem& item) const
{
	const KindInfo k = info(m_kind);
	requireName(item.name, k.singular);

	QSqlQuery query = prepareQuery(m_database.connection(), k.hasDescription
		? QStringLiteral("UPDATE %1 SET name = :name, description = :description WHERE id = :id")
			.arg(QLatin1String(k.table))
		: QStringLiteral("UPDATE %1 SET name = :name WHERE id = :id")
			.arg(QLatin1String(k.table)));
	query.bindValue(QStringLiteral(":id"), item.id);
	query.bindValue(QStringLiteral(":name"), item.name.trimmed());
	if (k.hasDescription)
		query.bindValue(QStringLiteral(":description"), nullIfEmpty(item.description));
	execQuery(query);
	if (query.numRowsAffected() == 0)
		throw DatabaseError(QStringLiteral("That %1 no longer exists.")
			.arg(QString::fromLatin1(k.singular).toLower()));
}

bool LookupRepository::remove(const QString& id) const
{
	const KindInfo k = info(m_kind);
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("DELETE FROM %1 WHERE id = :id").arg(QLatin1String(k.table)));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	return query.numRowsAffected() > 0;
}

int LookupRepository::usageCount(const QString& id) const
{
	const KindInfo k = info(m_kind);
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 = :id")
			.arg(QLatin1String(k.usageTable), QLatin1String(k.usageColumn)));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	return query.next() ? query.value(0).toInt() : 0;
}

QString LookupRepository::findOrCreate(const QString& name) const
{
	if (const auto existing = findByName(name))
		return existing->id;
	return insert({QString(), name.trimmed(), QString()});
}

AuthorRepository::AuthorRepository(const Database& database)
	: m_database(database)
{
}

QList<Author> AuthorRepository::all() const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT id, name, bio, birth_date FROM authors ORDER BY name COLLATE NOCASE"));
	execQuery(query);
	QList<Author> authors;
	while (query.next())
		authors.append(readAuthor(query));
	return authors;
}

std::optional<Author> AuthorRepository::find(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT id, name, bio, birth_date FROM authors WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	if (!query.next())
		return std::nullopt;
	return readAuthor(query);
}

std::optional<Author> AuthorRepository::findByName(const QString& name) const
{
	// ORDER BY id makes "the first one" stable between calls when several
	// authors share a name.
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT id, name, bio, birth_date FROM authors "
		"WHERE name = :name COLLATE NOCASE ORDER BY id LIMIT 1"));
	query.bindValue(QStringLiteral(":name"), name.trimmed());
	execQuery(query);
	if (!query.next())
		return std::nullopt;
	return readAuthor(query);
}

QString AuthorRepository::insert(Author author) const
{
	requireName(author.name, "Author");
	if (author.id.isEmpty())
		author.id = newId();
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"INSERT INTO authors (id, name, bio, birth_date) "
		"VALUES (:id, :name, :bio, :birth_date)"));
	query.bindValue(QStringLiteral(":id"), author.id);
	query.bindValue(QStringLiteral(":name"), author.name.trimmed());
	query.bindValue(QStringLiteral(":bio"), nullIfEmpty(author.bio));
	query.bindValue(QStringLiteral(":birth_date"), nullIfEmpty(author.birthDate.trimmed()));
	execQuery(query);
	return author.id;
}

void AuthorRepository::update(const Author& author) const
{
	requireName(author.name, "Author");
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"UPDATE authors SET name = :name, bio = :bio, birth_date = :birth_date WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), author.id);
	query.bindValue(QStringLiteral(":name"), author.name.trimmed());
	query.bindValue(QStringLiteral(":bio"), nullIfEmpty(author.bio));
	query.bindValue(QStringLiteral(":birth_date"), nullIfEmpty(author.birthDate.trimmed()));
	execQuery(query);
	if (query.numRowsAffected() == 0)
		throw DatabaseError(QStringLiteral("That author no longer exists."));
}

bool AuthorRepository::remove(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("DELETE FROM authors WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	return query.numRowsAffected() > 0;
}

int AuthorRepository::usageCount(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("SELECT COUNT(*) FROM book_authors WHERE author_id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	return query.next() ? query.value(0).toInt() : 0;
}

QString AuthorRepository::findOrCreate(const QString& name) const
{
	if (const auto existing = findByName(name))
		return existing->id;
	return insert({QString(), name.trimmed(), QString(), QString()});
}

} // namespace mbt
