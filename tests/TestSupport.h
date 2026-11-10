#pragma once

// Helpers shared by the tests: a fresh database per test, and quick ways to
// make the records most tests need.

#include "BookRepository.h"
#include "Database.h"
#include "DatabaseError.h"
#include "Entities.h"
#include "LookupRepository.h"

#include <QSqlQuery>
#include <QTest>

#include <functional>
#include <memory>

namespace mbt::test {

// An empty, fully migrated in-memory database. Each call is a separate
// database, so tests cannot see each other's data.
inline std::unique_ptr<Database> freshDatabase()
{
	auto database = std::make_unique<Database>();
	database->open(QStringLiteral(":memory:"));
	return database;
}

// Inserts a book with the given title and authors (creating the authors by
// name) and returns its id.
inline QString addBook(const Database& database, const QString& title,
	const QStringList& authorNames = {}, const std::function<void(Book&)>& customize = {})
{
	Book book;
	book.title = title;
	const AuthorRepository authors(database);
	for (const QString& name : authorNames)
		book.authorIds.append(authors.findOrCreate(name));
	if (customize)
		customize(book);
	BookRepository(database).insert(book);
	return book.id;
}

// Counts the rows of a table, for checking cascades and rollbacks.
inline int rowCount(const Database& database, const QString& table)
{
	QSqlQuery query(database.connection());
	if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)) || !query.next())
		return -1;
	return query.value(0).toInt();
}

} // namespace mbt::test

// Fails the test unless statement throws DatabaseError.
#define MBT_VERIFY_THROWS(statement) \
	QVERIFY_THROWS_EXCEPTION_COMPAT(statement)

// QVERIFY_THROWS_EXCEPTION only exists from Qt 6.3, and QVERIFY_EXCEPTION_THROWN
// is deprecated there; this works the same on every Qt 6 this project supports.
#define QVERIFY_THROWS_EXCEPTION_COMPAT(statement) \
	do { \
		bool mbtThrew = false; \
		try { \
			statement; \
		} catch (const mbt::DatabaseError&) { \
			mbtThrew = true; \
		} \
		QVERIFY2(mbtThrew, "Expected DatabaseError from: " #statement); \
	} while (false)
