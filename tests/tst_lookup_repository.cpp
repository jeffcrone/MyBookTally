// Tests for authors, series, genres, book types, and statuses
// (src/core/LookupRepository.cpp).

#include "TestSupport.h"

using namespace mbt;

Q_DECLARE_METATYPE(mbt::LookupKind)

class TestLookupRepository : public QObject
{
	Q_OBJECT

private:
	// Every lookup kind, as test data, so each behavior is checked for all
	// five tables.
	void allKinds()
	{
		QTest::addColumn<LookupKind>("kind");
		QTest::newRow("series") << LookupKind::Series;
		QTest::newRow("genres") << LookupKind::Genre;
		QTest::newRow("book types") << LookupKind::BookType;
		QTest::newRow("ownership statuses") << LookupKind::OwnershipStatus;
		QTest::newRow("reading statuses") << LookupKind::ReadingStatus;
	}

private slots:
	void insertFindUpdateRemove_data() { allKinds(); }
	void insertFindUpdateRemove()
	{
		QFETCH(LookupKind, kind);
		const auto database = test::freshDatabase();
		const LookupRepository repository(*database, kind);
		const bool hasDescription = lookupKindHasDescription(kind);

		const QString id = repository.insert({QString(), "  Test Item  ", "About it"});
		QVERIFY(!id.isEmpty());

		auto item = repository.find(id);
		QVERIFY(item);
		QCOMPARE(item->name, QString("Test Item")); // trimmed
		QCOMPARE(item->description, hasDescription ? QString("About it") : QString());

		item->name = "Renamed";
		item->description = "Changed";
		repository.update(*item);
		item = repository.find(id);
		QCOMPARE(item->name, QString("Renamed"));
		QCOMPARE(item->description, hasDescription ? QString("Changed") : QString());

		QVERIFY(repository.remove(id));
		QVERIFY(!repository.find(id));
		QVERIFY(!repository.remove(id));
	}

	void insertKeepsGivenId_data() { allKinds(); }
	void insertKeepsGivenId()
	{
		QFETCH(LookupKind, kind);
		const auto database = test::freshDatabase();
		const LookupRepository repository(*database, kind);
		QCOMPARE(repository.insert({"fixed-id", "Fixed", QString()}), QString("fixed-id"));
		QVERIFY(repository.find("fixed-id"));
	}

	void namesAreUniqueIgnoringCase_data() { allKinds(); }
	void namesAreUniqueIgnoringCase()
	{
		QFETCH(LookupKind, kind);
		const auto database = test::freshDatabase();
		const LookupRepository repository(*database, kind);
		repository.insert({QString(), "Unique Name", QString()});
		MBT_VERIFY_THROWS(repository.insert({QString(), "unique name", QString()}));
	}

	void emptyNameRejected_data() { allKinds(); }
	void emptyNameRejected()
	{
		QFETCH(LookupKind, kind);
		const auto database = test::freshDatabase();
		const LookupRepository repository(*database, kind);
		MBT_VERIFY_THROWS(repository.insert({QString(), "   ", QString()}));
		const QString id = repository.insert({QString(), "Named", QString()});
		MBT_VERIFY_THROWS(repository.update({id, "", QString()}));
	}

	void updateMissingThrows_data() { allKinds(); }
	void updateMissingThrows()
	{
		QFETCH(LookupKind, kind);
		const auto database = test::freshDatabase();
		MBT_VERIFY_THROWS(LookupRepository(*database, kind).update({"missing", "Name", QString()}));
	}

	void findByNameIgnoresCase_data() { allKinds(); }
	void findByNameIgnoresCase()
	{
		QFETCH(LookupKind, kind);
		const auto database = test::freshDatabase();
		const LookupRepository repository(*database, kind);
		const QString id = repository.insert({QString(), "Mixed Case", QString()});
		QCOMPARE(repository.findByName("mixed case")->id, id);
		QCOMPARE(repository.findByName("  MIXED CASE ")->id, id);
		QVERIFY(!repository.findByName("Other"));
	}

	void findOrCreate_data() { allKinds(); }
	void findOrCreate()
	{
		QFETCH(LookupKind, kind);
		const auto database = test::freshDatabase();
		const LookupRepository repository(*database, kind);
		const int before = int(repository.all().size());
		const QString first = repository.findOrCreate("Brand New");
		const QString second = repository.findOrCreate("brand new");
		QCOMPARE(first, second);
		QCOMPARE(int(repository.all().size()), before + 1);
	}

	void allIsSortedByName()
	{
		const auto database = test::freshDatabase();
		const LookupRepository repository(*database, LookupKind::Genre);
		repository.insert({QString(), "mystery", QString()});
		repository.insert({QString(), "Fantasy", QString()});
		repository.insert({QString(), "biography", QString()});
		QStringList names;
		for (const LookupItem& item : repository.all())
			names.append(item.name);
		QCOMPARE(names, QStringList({"biography", "Fantasy", "mystery"}));
	}

	void usageCountAndDeleteEffects()
	{
		const auto database = test::freshDatabase();
		const LookupRepository genres(*database, LookupKind::Genre);
		const LookupRepository types(*database, LookupKind::BookType);
		const QString genreId = genres.insert({QString(), "Fantasy", QString()});
		const QString typeId = types.findByName("Paperback")->id;

		const QString bookId = test::addBook(*database, "A Book", {}, [&](Book& book) {
			book.genreIds = {genreId};
			book.bookTypeId = typeId;
		});
		test::addBook(*database, "Another", {}, [&](Book& book) { book.genreIds = {genreId}; });

		QCOMPARE(genres.usageCount(genreId), 2);
		QCOMPARE(types.usageCount(typeId), 1);

		// Deleting a genre removes the links (CASCADE) but not the books.
		QVERIFY(genres.remove(genreId));
		QCOMPARE(test::rowCount(*database, "book_genres"), 0);
		QCOMPARE(test::rowCount(*database, "books"), 2);

		// Deleting a book type clears the books' type (SET NULL).
		QVERIFY(types.remove(typeId));
		const auto book = BookRepository(*database).find(bookId);
		QVERIFY(book);
		QVERIFY(book->bookTypeId.isEmpty());
	}

	void kindNames()
	{
		QCOMPARE(allLookupKinds().size(), 5);
		QCOMPARE(lookupKindSingular(LookupKind::Genre), QString("Genre"));
		QCOMPARE(lookupKindPlural(LookupKind::ReadingStatus), QString("Reading Statuses"));
		QVERIFY(lookupKindHasDescription(LookupKind::Series));
		QVERIFY(!lookupKindHasDescription(LookupKind::BookType));
	}

	// ---- Authors ----

	void authorCrud()
	{
		const auto database = test::freshDatabase();
		const AuthorRepository authors(*database);
		const QString id = authors.insert({QString(), "Ursula K. Le Guin", "Wrote Earthsea.", "1929-10-21"});
		auto author = authors.find(id);
		QVERIFY(author);
		QCOMPARE(author->name, QString("Ursula K. Le Guin"));
		QCOMPARE(author->bio, QString("Wrote Earthsea."));
		QCOMPARE(author->birthDate, QString("1929-10-21"));

		author->bio.clear();
		author->birthDate = "1929";
		authors.update(*author);
		author = authors.find(id);
		QVERIFY(author->bio.isEmpty());
		QCOMPARE(author->birthDate, QString("1929"));

		QVERIFY(authors.remove(id));
		QVERIFY(!authors.find(id));
	}

	void authorNamesMayRepeat()
	{
		const auto database = test::freshDatabase();
		const AuthorRepository authors(*database);
		const QString first = authors.insert({QString(), "John Smith", QString(), QString()});
		const QString second = authors.insert({QString(), "John Smith", QString(), QString()});
		QVERIFY(first != second);
		QCOMPARE(authors.all().size(), 2);
		// findByName is stable when names repeat.
		QCOMPARE(authors.findByName("john smith")->id, authors.findByName("JOHN SMITH")->id);
	}

	void authorValidation()
	{
		const auto database = test::freshDatabase();
		const AuthorRepository authors(*database);
		MBT_VERIFY_THROWS(authors.insert({QString(), " ", QString(), QString()}));
		MBT_VERIFY_THROWS(authors.update({"missing", "Name", QString(), QString()}));
	}

	void deletingAuthorUnlinksBooks()
	{
		const auto database = test::freshDatabase();
		const AuthorRepository authors(*database);
		const QString bookId = test::addBook(*database, "Co-written", {"First", "Second"});
		const QString firstId = authors.findByName("First")->id;
		QCOMPARE(authors.usageCount(firstId), 1);
		QVERIFY(authors.remove(firstId));
		const auto book = BookRepository(*database).find(bookId);
		QCOMPARE(book->authorIds.size(), 1);
		QCOMPARE(book->authorIds.first(), authors.findByName("Second")->id);
	}

	void authorFindOrCreate()
	{
		const auto database = test::freshDatabase();
		const AuthorRepository authors(*database);
		QCOMPARE(authors.findOrCreate("Terry Pratchett"), authors.findOrCreate("terry pratchett"));
		QCOMPARE(authors.all().size(), 1);
	}
};

QTEST_GUILESS_MAIN(TestLookupRepository)
#include "tst_lookup_repository.moc"
