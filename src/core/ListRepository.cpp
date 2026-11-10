#include "ListRepository.h"

#include "Database.h"
#include "DatabaseError.h"
#include "SqlUtil.h"

namespace mbt {

namespace {

const char* const listSelect = R"sql(
	SELECT l.id, l.name, l.description, l.created_at,
		(SELECT count(*) FROM list_books lb WHERE lb.list_id = l.id) AS book_count
	FROM lists l
)sql";

BookList readList(const QSqlQuery& query)
{
	BookList list;
	list.id = query.value(0).toString();
	list.name = query.value(1).toString();
	list.description = query.value(2).toString();
	list.createdAt = query.value(3).toString();
	list.bookCount = query.value(4).toInt();
	return list;
}

void requireName(const BookList& list)
{
	if (list.name.trimmed().isEmpty())
		throw DatabaseError(QStringLiteral("A list needs a name."));
}

} // namespace

ListRepository::ListRepository(const Database& database)
	: m_database(database)
{
}

QList<BookList> ListRepository::all() const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QLatin1String(listSelect) + QStringLiteral(" ORDER BY l.name COLLATE NOCASE"));
	execQuery(query);
	QList<BookList> lists;
	while (query.next())
		lists.append(readList(query));
	return lists;
}

std::optional<BookList> ListRepository::find(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QLatin1String(listSelect) + QStringLiteral(" WHERE l.id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	if (!query.next())
		return std::nullopt;
	return readList(query);
}

std::optional<BookList> ListRepository::findByName(const QString& name) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QLatin1String(listSelect) + QStringLiteral(" WHERE l.name = :name COLLATE NOCASE"));
	query.bindValue(QStringLiteral(":name"), name.trimmed());
	execQuery(query);
	if (!query.next())
		return std::nullopt;
	return readList(query);
}

QString ListRepository::create(BookList list) const
{
	requireName(list);
	if (list.id.isEmpty())
		list.id = newId();
	if (list.createdAt.isEmpty())
		list.createdAt = nowUtc();
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"INSERT INTO lists (id, name, description, created_at) "
		"VALUES (:id, :name, :description, :created_at)"));
	query.bindValue(QStringLiteral(":id"), list.id);
	query.bindValue(QStringLiteral(":name"), list.name.trimmed());
	query.bindValue(QStringLiteral(":description"), nullIfEmpty(list.description));
	query.bindValue(QStringLiteral(":created_at"), list.createdAt);
	execQuery(query);
	return list.id;
}

void ListRepository::update(const BookList& list) const
{
	requireName(list);
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"UPDATE lists SET name = :name, description = :description WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), list.id);
	query.bindValue(QStringLiteral(":name"), list.name.trimmed());
	query.bindValue(QStringLiteral(":description"), nullIfEmpty(list.description));
	execQuery(query);
	if (query.numRowsAffected() == 0)
		throw DatabaseError(QStringLiteral("That list no longer exists."));
}

bool ListRepository::remove(const QString& id) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QStringLiteral("DELETE FROM lists WHERE id = :id"));
	query.bindValue(QStringLiteral(":id"), id);
	execQuery(query);
	return query.numRowsAffected() > 0;
}

QString ListRepository::findOrCreate(const QString& name) const
{
	if (const auto existing = findByName(name))
		return existing->id;
	BookList list;
	list.name = name.trimmed();
	return create(list);
}

bool ListRepository::addBook(const QString& listId, const QString& bookId) const
{
	// INSERT OR IGNORE: adding a book that is already in the list is not an
	// error, it is simply nothing to do. The position is one past the
	// current last book, so new books go to the end.
	//
	// The list id is bound twice under two names because reusing one named
	// placeholder within a statement is not reliable across Qt versions.
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(R"sql(
		INSERT OR IGNORE INTO list_books (list_id, book_id, position, added_at)
		VALUES (:list_id, :book_id,
			COALESCE((SELECT max(position) FROM list_books WHERE list_id = :list_id_for_position), 0) + 1,
			:added_at)
	)sql"));
	query.bindValue(QStringLiteral(":list_id"), listId);
	query.bindValue(QStringLiteral(":book_id"), bookId);
	query.bindValue(QStringLiteral(":list_id_for_position"), listId);
	query.bindValue(QStringLiteral(":added_at"), nowUtc());
	execQuery(query);
	return query.numRowsAffected() > 0;
}

bool ListRepository::removeBook(const QString& listId, const QString& bookId) const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"DELETE FROM list_books WHERE list_id = :list_id AND book_id = :book_id"));
	query.bindValue(QStringLiteral(":list_id"), listId);
	query.bindValue(QStringLiteral(":book_id"), bookId);
	execQuery(query);
	return query.numRowsAffected() > 0;
}

QList<QString> ListRepository::bookIds(const QString& listId) const
{
	QSqlQuery query = prepareQuery(m_database.connection(), QStringLiteral(
		"SELECT book_id FROM list_books WHERE list_id = :list_id ORDER BY position"));
	query.bindValue(QStringLiteral(":list_id"), listId);
	execQuery(query);
	QList<QString> ids;
	while (query.next())
		ids.append(query.value(0).toString());
	return ids;
}

QList<BookList> ListRepository::listsForBook(const QString& bookId) const
{
	QSqlQuery query = prepareQuery(m_database.connection(),
		QLatin1String(listSelect) + QStringLiteral(
			" JOIN list_books m ON m.list_id = l.id"
			" WHERE m.book_id = :book_id"
			" ORDER BY l.name COLLATE NOCASE"));
	query.bindValue(QStringLiteral(":book_id"), bookId);
	execQuery(query);
	QList<BookList> lists;
	while (query.next())
		lists.append(readList(query));
	return lists;
}

} // namespace mbt
