// Tests for JSON export and import (src/core/JsonExchange.cpp).

#include "JsonExchange.h"
#include "ListRepository.h"
#include "ReviewRepository.h"
#include "TestSupport.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace mbt;

namespace {

// A book with something in every field and every kind of link.
QString addRichBook(const Database& database)
{
	const LookupRepository series(database, LookupKind::Series);
	const LookupRepository genres(database, LookupKind::Genre);
	const QString seriesId = series.insert({QString(), "Discworld", "Pratchett's long-running series"});
	const QString id = test::addBook(database, "Guards! Guards!", {"Terry Pratchett"}, [&](Book& b) {
		b.series = {{seriesId, {}, 8.0, {}}};
		b.genreIds = {genres.insert({QString(), "Fantasy", "Magic and such"}),
			genres.findOrCreate("Humor")};
		b.releaseDate = "1989";
		b.isbn = "9780552134637";
		b.pageCount = 416;
		b.bookTypeId = LookupRepository(database, LookupKind::BookType).findByName("Paperback")->id;
		b.ownershipStatusId = LookupRepository(database, LookupKind::OwnershipStatus).findByName("Have")->id;
		b.readingStatusId = LookupRepository(database, LookupKind::ReadingStatus).findByName("Read")->id;
		b.location = "Shelf 2";
		b.description = "The Night Watch versus a dragon.";
		b.coverImage = QByteArray("cover-bytes");
	});
	const AuthorRepository authors(database);
	Author author = *authors.findByName("Terry Pratchett");
	author.bio = "English author.";
	author.birthDate = "1948-04-28";
	authors.update(author);

	const ReviewRepository reviews(database);
	reviews.addReview({QString(), id, "Hilarious.", "Sam", QString()});
	Rating rating;
	rating.bookId = id;
	rating.value = 9;
	rating.scale = 10;
	reviews.addRating(rating);

	const ListRepository lists(database);
	BookList list;
	list.name = "Favorites";
	list.description = "Rereads";
	lists.addBook(lists.create(list), id);
	return id;
}

QJsonDocument exportAll(const Database& database, bool ids, bool covers = true)
{
	ExportOptions options;
	options.includeIds = ids;
	options.includeCovers = covers;
	return JsonExchange(database).exportBooks(BookRepository(database).allIds(), options);
}

// Does any object in the document (at any depth) have an "id" key?
bool containsIdKey(const QJsonValue& value)
{
	if (value.isObject()) {
		const QJsonObject object = value.toObject();
		if (object.contains("id"))
			return true;
		for (const QJsonValue child : object)
			if (containsIdKey(child))
				return true;
	} else if (value.isArray()) {
		for (const QJsonValue child : value.toArray())
			if (containsIdKey(child))
				return true;
	}
	return false;
}

void verifyRichBook(const Database& database, const QString& id)
{
	const auto book = BookRepository(database).find(id);
	QVERIFY(book);
	QCOMPARE(book->title, QString("Guards! Guards!"));
	QCOMPARE(book->releaseDate, QString("1989"));
	QCOMPARE(book->isbn, QString("9780552134637"));
	QCOMPARE(*book->pageCount, 416);
	QCOMPARE(book->location, QString("Shelf 2"));
	QCOMPARE(book->description, QString("The Night Watch versus a dragon."));
	QCOMPARE(book->coverImage, QByteArray("cover-bytes"));

	QCOMPARE(book->authorIds.size(), 1);
	const auto author = AuthorRepository(database).find(book->authorIds.first());
	QCOMPARE(author->name, QString("Terry Pratchett"));
	QCOMPARE(author->bio, QString("English author."));
	QCOMPARE(author->birthDate, QString("1948-04-28"));

	QCOMPARE(book->series.size(), 1);
	QCOMPARE(book->series.first().seriesName, QString("Discworld"));
	QCOMPARE(*book->series.first().position, 8.0);
	QCOMPARE(LookupRepository(database, LookupKind::Series).find(book->series.first().seriesId)->description,
		QString("Pratchett's long-running series"));

	QCOMPARE(book->genreIds.size(), 2);
	QCOMPARE(LookupRepository(database, LookupKind::Genre).findByName("Fantasy")->description,
		QString("Magic and such"));

	QCOMPARE(LookupRepository(database, LookupKind::BookType).find(book->bookTypeId)->name, QString("Paperback"));
	QCOMPARE(LookupRepository(database, LookupKind::OwnershipStatus).find(book->ownershipStatusId)->name,
		QString("Have"));
	QCOMPARE(LookupRepository(database, LookupKind::ReadingStatus).find(book->readingStatusId)->name,
		QString("Read"));

	const ReviewRepository reviews(database);
	QCOMPARE(reviews.reviews(id).size(), 1);
	QCOMPARE(reviews.reviews(id).first().text, QString("Hilarious."));
	QCOMPARE(reviews.reviews(id).first().reviewer, QString("Sam"));
	QCOMPARE(reviews.ratings(id).size(), 1);
	QCOMPARE(reviews.ratings(id).first().value, 9.0);
	QCOMPARE(reviews.ratings(id).first().scale, 10.0);

	const QList<BookList> lists = ListRepository(database).listsForBook(id);
	QCOMPARE(lists.size(), 1);
	QCOMPARE(lists.first().name, QString("Favorites"));
	QCOMPARE(lists.first().description, QString("Rereads"));
}

} // namespace

class TestJsonExchange : public QObject
{
	Q_OBJECT

private slots:
	void exportHeader()
	{
		const auto database = test::freshDatabase();
		addRichBook(*database);
		const QJsonObject root = exportAll(*database, true).object();
		QCOMPARE(root.value("format").toString(), QString("MyBookTally"));
		QCOMPARE(root.value("formatVersion").toInt(), JsonExchange::FormatVersion);
		QCOMPARE(root.value("appVersion").toString(), QString(MYBOOKTALLY_VERSION));
		QVERIFY(!root.value("exportedAt").toString().isEmpty());
		QCOMPARE(root.value("includesIds").toBool(), true);
		QCOMPARE(root.value("books").toArray().size(), 1);
	}

	void exportWithIdsHasIds()
	{
		const auto database = test::freshDatabase();
		const QString id = addRichBook(*database);
		const QJsonObject book = exportAll(*database, true).object().value("books").toArray().first().toObject();
		QCOMPARE(book.value("id").toString(), id);
		QVERIFY(book.value("authors").toArray().first().toObject().contains("id"));
		QVERIFY(book.value("bookType").toObject().contains("id"));
		QVERIFY(book.value("lists").toArray().first().toObject().contains("id"));
	}

	void exportWithNamesHasNoIds()
	{
		const auto database = test::freshDatabase();
		addRichBook(*database);
		const QJsonDocument document = exportAll(*database, false);
		QVERIFY(!containsIdKey(document.object().value("books")));
		const QJsonObject book = document.object().value("books").toArray().first().toObject();
		QCOMPARE(book.value("authors").toArray().first().toObject().value("name").toString(),
			QString("Terry Pratchett"));
		QCOMPARE(book.value("bookType").toObject().value("name").toString(), QString("Paperback"));
		QCOMPARE(book.value("series").toArray().first().toObject().value("position").toDouble(), 8.0);
	}

	void exportWithoutCovers()
	{
		const auto database = test::freshDatabase();
		addRichBook(*database);
		const QJsonObject book =
			exportAll(*database, true, false).object().value("books").toArray().first().toObject();
		QVERIFY(!book.contains("coverImage"));
	}

	void exportSkipsUnknownIdsAndKeepsOrder()
	{
		const auto database = test::freshDatabase();
		const QString a = test::addBook(*database, "A");
		const QString b = test::addBook(*database, "B");
		const QJsonArray books = JsonExchange(*database)
			.exportBooks({b, "missing", a}, ExportOptions()).object().value("books").toArray();
		QCOMPARE(books.size(), 2);
		QCOMPARE(books.at(0).toObject().value("title").toString(), QString("B"));
		QCOMPARE(books.at(1).toObject().value("title").toString(), QString("A"));
	}

	void roundTripWithIdsIntoEmptyLibrary()
	{
		const auto source = test::freshDatabase();
		const QString id = addRichBook(*source);
		const QJsonDocument document = exportAll(*source, true);

		const auto target = test::freshDatabase();
		const ImportResult result = JsonExchange(*target).importBooks(document);
		QCOMPARE(result.booksAdded, 1);
		QCOMPARE(result.booksUpdated, 0);
		QVERIFY(result.warnings.isEmpty());
		// The book keeps its id, so a later import updates the same record.
		verifyRichBook(*target, id);
		// The seeded statuses in the target were matched by name, not
		// duplicated, even though their ids differ from the source's.
		QCOMPARE(test::rowCount(*target, "book_types"), 5);
	}

	void roundTripWithNamesIntoEmptyLibrary()
	{
		const auto source = test::freshDatabase();
		addRichBook(*source);
		const QJsonDocument document = exportAll(*source, false);

		const auto target = test::freshDatabase();
		const ImportResult result = JsonExchange(*target).importBooks(document);
		QCOMPARE(result.booksAdded, 1);
		const QList<QString> ids = BookRepository(*target).allIds();
		QCOMPARE(ids.size(), 1);
		verifyRichBook(*target, ids.first());
	}

	void reimportUpdatesInsteadOfDuplicating_data()
	{
		QTest::addColumn<bool>("withIds");
		QTest::newRow("with ids") << true;
		QTest::newRow("names only") << false;
	}

	void reimportUpdatesInsteadOfDuplicating()
	{
		QFETCH(bool, withIds);
		const auto database = test::freshDatabase();
		addRichBook(*database);
		const QJsonDocument document = exportAll(*database, withIds);

		const ImportResult result = JsonExchange(*database).importBooks(document);
		QCOMPARE(result.booksAdded, 0);
		QCOMPARE(result.booksUpdated, 1);
		QCOMPARE(test::rowCount(*database, "books"), 1);
		QCOMPARE(test::rowCount(*database, "authors"), 1);
		QCOMPARE(test::rowCount(*database, "genres"), 2);
		QCOMPARE(test::rowCount(*database, "series"), 1);
		QCOMPARE(test::rowCount(*database, "reviews"), 1);
		QCOMPARE(test::rowCount(*database, "ratings"), 1);
		QCOMPARE(test::rowCount(*database, "lists"), 1);
		QCOMPARE(test::rowCount(*database, "list_books"), 1);
	}

	void importOverwritesFieldsOfMatchedBook()
	{
		const auto database = test::freshDatabase();
		const QString id = addRichBook(*database);
		QJsonObject root = exportAll(*database, true).object();
		QJsonArray books = root.value("books").toArray();
		QJsonObject book = books.first().toObject();
		book.insert("title", "Guards! Guards! (Revised)");
		book.insert("pageCount", 420);
		book.remove("location");
		books[0] = book;
		root.insert("books", books);

		JsonExchange(*database).importBooks(QJsonDocument(root));
		const auto stored = BookRepository(*database).find(id);
		QCOMPARE(stored->title, QString("Guards! Guards! (Revised)"));
		QCOMPARE(*stored->pageCount, 420);
		QVERIFY(stored->location.isEmpty());
	}

	void importWithoutCoverKeepsExistingCover()
	{
		const auto database = test::freshDatabase();
		const QString id = addRichBook(*database);
		JsonExchange(*database).importBooks(exportAll(*database, true, false));
		QCOMPARE(BookRepository(*database).find(id)->coverImage, QByteArray("cover-bytes"));
	}

	void namesOnlyMatchesByIsbn()
	{
		const auto database = test::freshDatabase();
		const QString id = test::addBook(*database, "Original Title", {}, [](Book& b) {
			b.isbn = "9780306406157";
		});
		const QJsonObject book{{"title", "Different Title"}, {"isbn", "978-0-306-40615-7"}};
		const QJsonObject root{{"format", "MyBookTally"}, {"formatVersion", 1},
			{"books", QJsonArray{book}}};
		const ImportResult result = JsonExchange(*database).importBooks(QJsonDocument(root));
		QCOMPARE(result.booksUpdated, 1);
		QCOMPARE(BookRepository(*database).find(id)->title, QString("Different Title"));
	}

	void namesOnlyMatchesByTitleAndAuthors()
	{
		const auto database = test::freshDatabase();
		const QString id = test::addBook(*database, "Same Title", {"Author One"});
		test::addBook(*database, "Same Title", {"Someone Else"});
		const QJsonObject book{{"title", "same title"},
			{"authors", QJsonArray{QJsonObject{{"name", "author one"}}}},
			{"location", "Found it"}};
		const QJsonObject root{{"format", "MyBookTally"}, {"formatVersion", 1},
			{"books", QJsonArray{book}}};
		const ImportResult result = JsonExchange(*database).importBooks(QJsonDocument(root));
		QCOMPARE(result.booksUpdated, 1);
		QCOMPARE(result.booksAdded, 0);
		QCOMPARE(BookRepository(*database).find(id)->location, QString("Found it"));
	}

	void sameTitleDifferentAuthorIsNewBook()
	{
		const auto database = test::freshDatabase();
		test::addBook(*database, "Common Title", {"Author One"});
		const QJsonObject book{{"title", "Common Title"},
			{"authors", QJsonArray{QJsonObject{{"name", "Author Two"}}}}};
		const QJsonObject root{{"format", "MyBookTally"}, {"formatVersion", 1},
			{"books", QJsonArray{book}}};
		const ImportResult result = JsonExchange(*database).importBooks(QJsonDocument(root));
		QCOMPARE(result.booksAdded, 1);
		QCOMPARE(test::rowCount(*database, "books"), 2);
	}

	void minimalHandWrittenFile()
	{
		const auto database = test::freshDatabase();
		const QByteArray json = R"({
			"format": "MyBookTally",
			"formatVersion": 1,
			"books": [
				{
					"title": "Dune",
					"authors": [{"name": "Frank Herbert"}],
					"genres": [{"name": "Science Fiction"}],
					"series": [{"name": "Dune", "position": 1}],
					"readingStatus": {"name": "To Read"},
					"ratings": [{"value": 5}, {"reviewer": "no score, skipped"}],
					"lists": [{"name": "Classics"}]
				}
			]
		})";
		const ImportResult result = JsonExchange(*database).importBooks(QJsonDocument::fromJson(json));
		QCOMPARE(result.booksAdded, 1);
		QVERIFY(result.warnings.isEmpty());
		const QString id = BookRepository(*database).allIds().first();
		const auto book = BookRepository(*database).find(id);
		QCOMPARE(book->title, QString("Dune"));
		QCOMPARE(AuthorRepository(*database).find(book->authorIds.first())->name, QString("Frank Herbert"));
		QCOMPARE(book->readingStatusId,
			LookupRepository(*database, LookupKind::ReadingStatus).findByName("To Read")->id);
		const QList<Rating> ratings = ReviewRepository(*database).ratings(id);
		QCOMPARE(ratings.size(), 1);
		QCOMPARE(ratings.first().scale, 5.0); // default scale
		QCOMPARE(ListRepository(*database).findByName("Classics")->bookCount, 1);
	}

	void badBookIsSkippedOthersImported()
	{
		const auto database = test::freshDatabase();
		const QJsonObject good{{"title", "Good Book"}};
		const QJsonObject noTitle{{"authors", QJsonArray{QJsonObject{{"name", "Orphan Author"}}}}};
		const QJsonObject badRating{{"title", "Bad Rating"},
			{"ratings", QJsonArray{QJsonObject{{"value", 7}, {"scale", 5}}}}};
		const QJsonObject root{{"format", "MyBookTally"}, {"formatVersion", 1},
			{"books", QJsonArray{good, noTitle, badRating}}};

		const ImportResult result = JsonExchange(*database).importBooks(QJsonDocument(root));
		QCOMPARE(result.booksAdded, 1);
		QCOMPARE(result.warnings.size(), 2);
		QVERIFY(result.warnings.at(0).startsWith("Book 2"));
		QVERIFY(result.warnings.at(1).contains("Bad Rating"));
		// Each failed book was rolled back entirely, including the author
		// it created and the book row written before the rating failed.
		QCOMPARE(test::rowCount(*database, "books"), 1);
		QCOMPARE(test::rowCount(*database, "authors"), 0);
	}

	void rejectsWrongFiles_data()
	{
		QTest::addColumn<QByteArray>("json");
		QTest::newRow("array") << QByteArray("[]");
		QTest::newRow("other format") << QByteArray(R"({"format": "Other", "formatVersion": 1, "books": []})");
		QTest::newRow("no version") << QByteArray(R"({"format": "MyBookTally", "books": []})");
		QTest::newRow("newer version") << QByteArray(R"({"format": "MyBookTally", "formatVersion": 99, "books": []})");
		QTest::newRow("no books") << QByteArray(R"({"format": "MyBookTally", "formatVersion": 1})");
	}

	void rejectsWrongFiles()
	{
		QFETCH(QByteArray, json);
		const auto database = test::freshDatabase();
		MBT_VERIFY_THROWS(JsonExchange(*database).importBooks(QJsonDocument::fromJson(json)));
	}

	void emptyBookListIsFine()
	{
		const auto database = test::freshDatabase();
		const QJsonObject root{{"format", "MyBookTally"}, {"formatVersion", 1}, {"books", QJsonArray()}};
		const ImportResult result = JsonExchange(*database).importBooks(QJsonDocument(root));
		QCOMPARE(result.booksAdded, 0);
		QCOMPARE(result.booksUpdated, 0);
	}
};

QTEST_GUILESS_MAIN(TestJsonExchange)
#include "tst_json_exchange.moc"
