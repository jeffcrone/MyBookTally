// Tests for opening the database, creating the schema, and transactions
// (src/core/Database.cpp, Schema.cpp, SqlUtil.cpp).

#include "Schema.h"
#include "SqlUtil.h"
#include "TestSupport.h"

#include <QSqlRecord>
#include <QTemporaryDir>

using namespace mbt;

namespace {

QStringList tableNames(const Database& database)
{
	QSqlQuery query(database.connection());
	query.exec(QStringLiteral(
		"SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name"));
	QStringList names;
	while (query.next())
		names.append(query.value(0).toString());
	return names;
}

} // namespace

class TestDatabase : public QObject
{
	Q_OBJECT

private slots:
	void createsEveryTable()
	{
		const auto database = test::freshDatabase();
		const QStringList expected = {
			"authors", "book_authors", "book_genres", "book_series", "book_types", "books",
			"genres", "list_books", "lists", "ownership_statuses", "ratings", "reading_statuses",
			"reviews", "series",
		};
		QCOMPARE(tableNames(*database), expected);
	}

	void setsSchemaVersion()
	{
		const auto database = test::freshDatabase();
		QCOMPARE(Schema::version(database->connection()), Schema::CurrentVersion);
	}

	void seedsLookupTables()
	{
		const auto database = test::freshDatabase();
		QCOMPARE(test::rowCount(*database, "book_types"), 5);
		QCOMPARE(test::rowCount(*database, "ownership_statuses"), 4);
		QCOMPARE(test::rowCount(*database, "reading_statuses"), 4);
		QVERIFY(LookupRepository(*database, LookupKind::OwnershipStatus).findByName("Want"));
		QVERIFY(LookupRepository(*database, LookupKind::ReadingStatus).findByName("To Read"));
		QVERIFY(LookupRepository(*database, LookupKind::BookType).findByName("Hardcover"));
		// Genres and series start empty: they are personal.
		QCOMPARE(test::rowCount(*database, "genres"), 0);
		QCOMPARE(test::rowCount(*database, "series"), 0);
	}

	void enforcesForeignKeys()
	{
		const auto database = test::freshDatabase();
		QSqlQuery query(database->connection());
		QVERIFY(query.exec("PRAGMA foreign_keys"));
		QVERIFY(query.next());
		QCOMPARE(query.value(0).toInt(), 1);

		// A link to a book that does not exist must be refused.
		QSqlQuery insert(database->connection());
		QVERIFY(!insert.exec(
			"INSERT INTO book_genres (book_id, genre_id) VALUES ('no-such-book', 'no-such-genre')"));
	}

	void keepsDataBetweenOpens()
	{
		QTemporaryDir folder;
		QVERIFY(folder.isValid());
		const QString path = folder.filePath("sub/folder/library.sqlite");
		QString id;
		{
			Database database;
			// Also checks that missing parent folders are created.
			database.open(path);
			id = test::addBook(database, "Persistent", {"Someone"});
		}
		Database again;
		again.open(path);
		const auto book = BookRepository(again).find(id);
		QVERIFY(book);
		QCOMPARE(book->title, QString("Persistent"));
		// Reopening does not re-run the seeding.
		QCOMPARE(test::rowCount(again, "book_types"), 5);
	}

	void refusesNewerSchema()
	{
		QTemporaryDir folder;
		const QString path = folder.filePath("future.sqlite");
		{
			Database database;
			database.open(path);
			execSql(database.connection(),
				QStringLiteral("PRAGMA user_version = %1").arg(Schema::CurrentVersion + 1));
		}
		Database again;
		MBT_VERIFY_THROWS(again.open(path));
		QVERIFY(!again.isOpen());
	}

	void reportsUnopenableFile()
	{
		QTemporaryDir folder;
		// A folder cannot be opened as a database file.
		Database database;
		MBT_VERIFY_THROWS(database.open(folder.path()));
		QVERIFY(!database.isOpen());
	}

	void connectionThrowsWhenClosed()
	{
		Database database;
		MBT_VERIFY_THROWS(database.connection());
	}

	void independentDatabases()
	{
		const auto first = test::freshDatabase();
		const auto second = test::freshDatabase();
		test::addBook(*first, "Only in the first");
		QCOMPARE(test::rowCount(*first, "books"), 1);
		QCOMPARE(test::rowCount(*second, "books"), 0);
	}

	void transactionCommits()
	{
		const auto database = test::freshDatabase();
		{
			Transaction transaction(database->connection());
			test::addBook(*database, "Kept");
			transaction.commit();
		}
		QCOMPARE(test::rowCount(*database, "books"), 1);
	}

	void transactionRollsBackWithoutCommit()
	{
		const auto database = test::freshDatabase();
		{
			Transaction transaction(database->connection());
			test::addBook(*database, "Discarded");
		}
		QCOMPARE(test::rowCount(*database, "books"), 0);
	}

	void nestedTransactionRollsBackOnlyInner()
	{
		const auto database = test::freshDatabase();
		{
			Transaction outer(database->connection());
			test::addBook(*database, "Outer");
			{
				Transaction inner(database->connection());
				test::addBook(*database, "Inner");
				// No commit: only "Inner" is undone.
			}
			outer.commit();
		}
		QCOMPARE(test::rowCount(*database, "books"), 1);
		QCOMPARE(BookRepository(*database).findIdsByTitle("Outer").size(), 1);
	}

	void transactionRollsBackOnException()
	{
		const auto database = test::freshDatabase();
		try {
			Transaction transaction(database->connection());
			test::addBook(*database, "Doomed");
			throw DatabaseError("simulated failure");
		} catch (const DatabaseError&) {
		}
		QCOMPARE(test::rowCount(*database, "books"), 0);
	}

	void badSqlThrowsWithMessage()
	{
		const auto database = test::freshDatabase();
		try {
			execSql(database->connection(), "SELECT * FROM no_such_table");
			QFAIL("expected an exception");
		} catch (const DatabaseError& error) {
			QVERIFY(error.message().contains("no_such_table"));
		}
	}

	void helperConversions()
	{
		QVERIFY(nullIfEmpty(QString()).isNull());
		QCOMPARE(nullIfEmpty(QString("x")).toString(), QString("x"));
		QVERIFY(nullIfUnset(std::optional<int>()).isNull());
		QCOMPARE(nullIfUnset(std::optional<int>(7)).toInt(), 7);
		QVERIFY(!optionalInt(QVariant()));
		QCOMPARE(*optionalInt(QVariant(3)), 3);
		QVERIFY(!optionalDouble(QVariant()));
		QCOMPARE(*optionalDouble(QVariant(1.5)), 1.5);
		QVERIFY(newId() != newId());
		QCOMPARE(newId().size(), 36);
	}
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
