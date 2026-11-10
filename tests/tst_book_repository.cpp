// Tests for saving and reading books and their links
// (src/core/BookRepository.cpp).

#include "ListRepository.h"
#include "ReviewRepository.h"
#include "TestSupport.h"

using namespace mbt;

class TestBookRepository : public QObject
{
	Q_OBJECT

private slots:
	void insertAndFindEveryField()
	{
		const auto database = test::freshDatabase();
		const LookupRepository types(*database, LookupKind::BookType);
		const LookupRepository owned(*database, LookupKind::OwnershipStatus);
		const LookupRepository reading(*database, LookupKind::ReadingStatus);
		const LookupRepository genres(*database, LookupKind::Genre);
		const LookupRepository series(*database, LookupKind::Series);
		const AuthorRepository authors(*database);

		Book book;
		book.title = "  A Wizard of Earthsea  ";
		book.authorIds = {authors.findOrCreate("Ursula K. Le Guin")};
		book.series = {{series.findOrCreate("Earthsea"), QString(), 1.0, QString()}};
		book.genreIds = {genres.findOrCreate("Fantasy")};
		book.releaseDate = "1968";
		book.isbn = "978-0-547-77374-2";
		book.pageCount = 183;
		book.bookTypeId = types.findByName("Paperback")->id;
		book.ownershipStatusId = owned.findByName("Have")->id;
		book.readingStatusId = reading.findByName("Read")->id;
		book.location = "Living room shelf";
		book.description = "A young wizard.\nSecond line.";
		book.coverImage = QByteArray("\x89PNG fake bytes", 15);
		book.isPrivate = true;

		const BookRepository books(*database);
		books.insert(book);
		QVERIFY(!book.id.isEmpty());
		QVERIFY(!book.createdAt.isEmpty());
		QCOMPARE(book.createdAt, book.updatedAt);

		const auto stored = books.find(book.id);
		QVERIFY(stored);
		QCOMPARE(stored->title, QString("A Wizard of Earthsea"));
		QCOMPARE(stored->authorIds, book.authorIds);
		QCOMPARE(stored->series.size(), 1);
		QCOMPARE(stored->series.first().seriesId, book.series.first().seriesId);
		QCOMPARE(stored->series.first().seriesName, QString("Earthsea"));
		QCOMPARE(*stored->series.first().position, 1.0);
		QCOMPARE(stored->genreIds, book.genreIds);
		QCOMPARE(stored->releaseDate, QString("1968"));
		QCOMPARE(stored->isbn, QString("9780547773742")); // normalized
		QCOMPARE(*stored->pageCount, 183);
		QCOMPARE(stored->bookTypeId, book.bookTypeId);
		QCOMPARE(stored->ownershipStatusId, book.ownershipStatusId);
		QCOMPARE(stored->readingStatusId, book.readingStatusId);
		QCOMPARE(stored->location, QString("Living room shelf"));
		QCOMPARE(stored->description, book.description);
		QCOMPARE(stored->coverImage, book.coverImage);
		QVERIFY(stored->isPrivate);
		QCOMPARE(stored->createdAt, book.createdAt);
	}

	void optionalFieldsStoredAsNull()
	{
		const auto database = test::freshDatabase();
		const QString id = test::addBook(*database, "Bare");
		QSqlQuery query(database->connection());
		QVERIFY(query.exec(QStringLiteral(
			"SELECT release_date, isbn, page_count, book_type_id, location, description, cover_image "
			"FROM books WHERE id = '%1'").arg(id)));
		QVERIFY(query.next());
		for (int column = 0; column < 7; ++column)
			QVERIFY2(query.value(column).isNull(), qPrintable(QString::number(column)));

		const auto book = BookRepository(*database).find(id);
		QVERIFY(!book->pageCount);
		QVERIFY(book->authorIds.isEmpty());
		QVERIFY(!book->isPrivate);
	}

	void validation()
	{
		const auto database = test::freshDatabase();
		const BookRepository books(*database);
		Book noTitle;
		noTitle.title = "   ";
		MBT_VERIFY_THROWS(books.insert(noTitle));

		Book zeroPages;
		zeroPages.title = "Zero";
		zeroPages.pageCount = 0;
		MBT_VERIFY_THROWS(books.insert(zeroPages));

		Book missing;
		missing.id = "no-such-book";
		missing.title = "Missing";
		MBT_VERIFY_THROWS(books.update(missing));
		QCOMPARE(test::rowCount(*database, "books"), 0);
	}

	void failedInsertLeavesNothingBehind()
	{
		// A link to an author that does not exist violates a foreign key
		// after the book row is written; the whole insert must roll back.
		const auto database = test::freshDatabase();
		Book book;
		book.title = "Half Saved";
		book.authorIds = {"no-such-author"};
		MBT_VERIFY_THROWS(BookRepository(*database).insert(book));
		QCOMPARE(test::rowCount(*database, "books"), 0);
		QCOMPARE(test::rowCount(*database, "book_authors"), 0);
	}

	void updateReplacesFieldsAndLinks()
	{
		const auto database = test::freshDatabase();
		const BookRepository books(*database);
		const AuthorRepository authors(*database);
		const LookupRepository genres(*database, LookupKind::Genre);
		const QString id = test::addBook(*database, "Original", {"Alpha", "Beta"},
			[&](Book& b) { b.genreIds = {genres.findOrCreate("Old Genre")}; });

		auto book = *books.find(id);
		const QString created = book.createdAt;
		book.title = "Changed";
		book.authorIds = {authors.findOrCreate("Gamma"), authors.findByName("Alpha")->id};
		book.genreIds = {genres.findOrCreate("New Genre")};
		book.isbn = "0306406152";
		QTest::qWait(5); // so updatedAt visibly moves on
		books.update(book);

		const auto stored = *books.find(id);
		QCOMPARE(stored.title, QString("Changed"));
		QCOMPARE(stored.authorIds, book.authorIds); // new order kept, Beta gone
		QCOMPARE(stored.genreIds, QList<QString>({genres.findByName("New Genre")->id}));
		QCOMPARE(stored.isbn, QString("0306406152"));
		QCOMPARE(stored.createdAt, created);
		QVERIFY(stored.updatedAt > created);
		// The unlinked author and genre still exist; only the links changed.
		QVERIFY(authors.findByName("Beta"));
		QVERIFY(genres.findByName("Old Genre"));
	}

	void duplicateLinksAreIgnored()
	{
		const auto database = test::freshDatabase();
		const AuthorRepository authors(*database);
		const QString authorId = authors.findOrCreate("Repeat");
		const QString id = test::addBook(*database, "Twice", {}, [&](Book& b) {
			b.authorIds = {authorId, authorId};
		});
		QCOMPARE(BookRepository(*database).find(id)->authorIds.size(), 1);
	}

	void removeCascades()
	{
		const auto database = test::freshDatabase();
		const QString id = test::addBook(*database, "Doomed", {"Author"}, [&](Book& b) {
			b.genreIds = {LookupRepository(*database, LookupKind::Genre).findOrCreate("G")};
			b.series = {{LookupRepository(*database, LookupKind::Series).findOrCreate("S"), {}, 1.0, {}}};
		});
		const ReviewRepository reviews(*database);
		reviews.addReview({QString(), id, "Great", QString(), QString()});
		Rating rating;
		rating.bookId = id;
		rating.value = 4;
		reviews.addRating(rating);
		const ListRepository lists(*database);
		BookList list;
		list.name = "Favorites";
		lists.addBook(lists.create(list), id);

		QVERIFY(BookRepository(*database).remove(id));
		for (const char* table : {"books", "book_authors", "book_genres", "book_series", "reviews",
				 "ratings", "list_books"})
			QVERIFY2(test::rowCount(*database, table) == 0, table);
		// The things the book linked to are kept.
		QCOMPARE(test::rowCount(*database, "authors"), 1);
		QCOMPARE(test::rowCount(*database, "genres"), 1);
		QCOMPARE(test::rowCount(*database, "lists"), 1);
		QVERIFY(!BookRepository(*database).remove(id));
	}

	void summariesResolveNames()
	{
		const auto database = test::freshDatabase();
		const LookupRepository series(*database, LookupKind::Series);
		const LookupRepository genres(*database, LookupKind::Genre);
		const QString id = test::addBook(*database, "Good Omens", {"Terry Pratchett", "Neil Gaiman"},
			[&](Book& b) {
				b.genreIds = {genres.findOrCreate("Humor"), genres.findOrCreate("Fantasy")};
				b.series = {
					{series.findOrCreate("Zeta"), {}, 2.0, {}},
					{series.findOrCreate("Alpha"), {}, 1.5, {}},
					{series.findOrCreate("Middle"), {}, 3.0, "Prequel"},
					{series.findOrCreate("No Number"), {}, std::nullopt, {}},
				};
				b.pageCount = 412;
				b.bookTypeId = LookupRepository(*database, LookupKind::BookType).findByName("Hardcover")->id;
				b.location = "Shelf";
			});
		const ReviewRepository reviews(*database);
		Rating a;
		a.bookId = id;
		a.value = 4;
		a.scale = 5;
		reviews.addRating(a);
		Rating b;
		b.bookId = id;
		b.value = 6;
		b.scale = 10; // 3 out of 5
		reviews.addRating(b);

		const QList<BookSummary> rows = BookRepository(*database).summaries(false);
		QCOMPARE(rows.size(), 1);
		const BookSummary& row = rows.first();
		QCOMPARE(row.id, id);
		QCOMPARE(row.authors, QString("Terry Pratchett, Neil Gaiman")); // credit order
		QCOMPARE(row.genres, QString("Fantasy, Humor"));                // alphabetical
		QCOMPARE(row.series, QString("Alpha #1.5, Middle #Prequel, No Number, Zeta #2"));
		QCOMPARE(*row.pageCount, 412);
		QCOMPARE(row.bookType, QString("Hardcover"));
		QCOMPARE(row.location, QString("Shelf"));
		QVERIFY(row.ownershipStatus.isEmpty());
		QCOMPARE(row.ratingCount, 2);
		QCOMPARE(*row.averageRating, 3.5);
	}

	void summariesHidePrivateBooks()
	{
		const auto database = test::freshDatabase();
		test::addBook(*database, "Public");
		test::addBook(*database, "Secret", {}, [](Book& b) { b.isPrivate = true; });
		const BookRepository books(*database);
		QCOMPARE(books.summaries(false).size(), 1);
		QCOMPARE(books.summaries(false).first().title, QString("Public"));
		QCOMPARE(books.summaries(true).size(), 2);
	}

	void summariesForAList()
	{
		const auto database = test::freshDatabase();
		const QString first = test::addBook(*database, "B First Added");
		const QString second = test::addBook(*database, "A Second Added", {}, [](Book& b) {
			b.isPrivate = true;
		});
		test::addBook(*database, "Not In List");
		const ListRepository lists(*database);
		BookList list;
		list.name = "Picks";
		const QString listId = lists.create(list);
		lists.addBook(listId, first);
		lists.addBook(listId, second);

		const QList<BookSummary> rows = BookRepository(*database).summaries(false, listId);
		QCOMPARE(rows.size(), 2); // private books show in lists regardless
		QCOMPARE(rows.at(0).id, first); // in the order added, not by title
		QCOMPARE(rows.at(1).id, second);
	}

	void summariesSortedByTitleIgnoringCase()
	{
		const auto database = test::freshDatabase();
		test::addBook(*database, "beta");
		test::addBook(*database, "Alpha");
		test::addBook(*database, "Gamma");
		QStringList titles;
		for (const BookSummary& row : BookRepository(*database).summaries(true))
			titles.append(row.title);
		QCOMPARE(titles, QStringList({"Alpha", "beta", "Gamma"}));
	}

	void findByIsbnAndTitle()
	{
		const auto database = test::freshDatabase();
		const QString id = test::addBook(*database, "Findable", {}, [](Book& b) {
			b.isbn = "978-0-306-40615-7";
		});
		const BookRepository books(*database);
		QCOMPARE(books.findIdsByIsbn("9780306406157"), QList<QString>({id}));
		QCOMPARE(books.findIdsByIsbn("978 0 306 40615 7"), QList<QString>({id}));
		QVERIFY(books.findIdsByIsbn("").isEmpty());
		QVERIFY(books.findIdsByIsbn("0306406152").isEmpty());
		QCOMPARE(books.findIdsByTitle("findable"), QList<QString>({id}));
		QVERIFY(books.findIdsByTitle("Find").isEmpty());
		QCOMPARE(books.allIds(), QList<QString>({id}));
	}

	void findMissingReturnsNothing()
	{
		const auto database = test::freshDatabase();
		QVERIFY(!BookRepository(*database).find("missing"));
	}
};

QTEST_GUILESS_MAIN(TestBookRepository)
#include "tst_book_repository.moc"
