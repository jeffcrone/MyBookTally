// Tests for OpenLibrary: the URLs the app asks Open Library for, and how it
// reads the answers. No network is used; the replies are samples in the
// shape the real service sends (see OpenLibrarySamples.h).

#include "OpenLibrary.h"
#include "OpenLibrarySamples.h"

#include <QTest>
#include <QUrlQuery>

using namespace mbt;

class TestOpenLibrary : public QObject
{
	Q_OBJECT

private:
	static QUrlQuery queryOf(const QUrl& url) { return QUrlQuery(url); }

private slots:
	// ---- Building URLs ----

	void searchUsesTitleAndAuthorFields()
	{
		const QUrl url = OpenLibrary::searchUrl(OpenLibrary::defaultApiBase(),
			OpenLibraryQuery{QStringLiteral(" The Hobbit "), QStringLiteral("Tolkien"), QString()});
		QCOMPARE(url.scheme(), QStringLiteral("https"));
		QCOMPARE(url.host(), QStringLiteral("openlibrary.org"));
		QCOMPARE(url.path(), QStringLiteral("/search.json"));
		const QUrlQuery query = queryOf(url);
		QCOMPARE(query.queryItemValue(QStringLiteral("title")), QStringLiteral("The Hobbit"));
		QCOMPARE(query.queryItemValue(QStringLiteral("author")), QStringLiteral("Tolkien"));
		QVERIFY(!query.hasQueryItem(QStringLiteral("isbn")));
		QVERIFY(!query.hasQueryItem(QStringLiteral("q")));
	}

	void searchByIsbnIgnoresTheRest()
	{
		// A title spelled differently from Open Library's would otherwise
		// rule out the exact book the ISBN names.
		const QUrl url = OpenLibrary::searchUrl(OpenLibrary::defaultApiBase(),
			OpenLibraryQuery{QStringLiteral("Hobit"), QStringLiteral("Tolkien"), QStringLiteral("978-0-261-10334-4")});
		const QUrlQuery query = queryOf(url);
		QCOMPARE(query.queryItemValue(QStringLiteral("isbn")), QStringLiteral("9780261103344"));
		QVERIFY(!query.hasQueryItem(QStringLiteral("title")));
		QVERIFY(!query.hasQueryItem(QStringLiteral("author")));
	}

	void searchIgnoresMalformedIsbn()
	{
		const QUrlQuery query = queryOf(OpenLibrary::searchUrl(OpenLibrary::defaultApiBase(),
			OpenLibraryQuery{QStringLiteral("Dune"), QString(), QStringLiteral("12345")}));
		QVERIFY(!query.hasQueryItem(QStringLiteral("isbn")));
		QCOMPARE(query.queryItemValue(QStringLiteral("title")), QStringLiteral("Dune"));
	}

	void searchAsksForFewFieldsAndALimit()
	{
		// Asking only for what is shown keeps replies small, as Open
		// Library asks of API clients.
		const QUrlQuery query = queryOf(OpenLibrary::searchUrl(OpenLibrary::defaultApiBase(),
			OpenLibraryQuery{QStringLiteral("Dune"), QString(), QString()}));
		const QStringList fields = query.queryItemValue(QStringLiteral("fields")).split(QLatin1Char(','));
		for (const char* field : {"key", "title", "author_name", "cover_i", "editions", "editions.key",
				"editions.isbn", "editions.cover_i", "editions.publish_date"})
			QVERIFY2(fields.contains(QLatin1String(field)), field);
		QVERIFY(!fields.contains(QStringLiteral("*")));
		QCOMPARE(query.queryItemValue(QStringLiteral("limit")).toInt(), OpenLibrary::SearchLimit);
	}

	void searchEncodesSpecialCharacters()
	{
		const QUrl url = OpenLibrary::searchUrl(OpenLibrary::defaultApiBase(),
			OpenLibraryQuery{QStringLiteral("Tom & Jerry #1?"), QStringLiteral("Brontë"), QString()});
		QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded),
			QStringLiteral("Tom & Jerry #1?"));
		QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("author"), QUrl::FullyDecoded),
			QStringLiteral("Brontë"));
		// Still one query, not split at the "&" or cut at the "#".
		QCOMPARE(url.fragment(), QString());
	}

	void searchKeepsTheBasePath()
	{
		const QUrl url = OpenLibrary::searchUrl(QUrl(QStringLiteral("http://127.0.0.1:8080/mirror")),
			OpenLibraryQuery{QStringLiteral("Dune"), QString(), QString()});
		QCOMPARE(url.path(), QStringLiteral("/mirror/search.json"));
		QCOMPARE(url.port(), 8080);
	}

	void recordUrlOnlyForWorkAndEditionKeys()
	{
		const QUrl base = OpenLibrary::defaultApiBase();
		QCOMPARE(OpenLibrary::recordUrl(base, QStringLiteral("/works/OL27482W")).toString(),
			QStringLiteral("https://openlibrary.org/works/OL27482W.json"));
		QCOMPARE(OpenLibrary::recordUrl(base, QStringLiteral("/books/OL10236414M")).toString(),
			QStringLiteral("https://openlibrary.org/books/OL10236414M.json"));
		// A strange key in a reply must not become a request elsewhere.
		QVERIFY(OpenLibrary::recordUrl(base, QString()).isEmpty());
		QVERIFY(OpenLibrary::recordUrl(base, QStringLiteral("/authors/OL26320A")).isEmpty());
		QVERIFY(OpenLibrary::recordUrl(base, QStringLiteral("/works/OL1W/../../x")).isEmpty());
		QVERIFY(OpenLibrary::recordUrl(base, QStringLiteral("//evil.example/works/OL1W")).isEmpty());
		QVERIFY(OpenLibrary::recordUrl(base, QStringLiteral("/books/OL1W")).isEmpty());
	}

	void coverUrlBySize()
	{
		const QUrl base = OpenLibrary::defaultCoversBase();
		QCOMPARE(OpenLibrary::coverUrl(base, 10522108, OpenLibrary::CoverSize::Small).toString(),
			QStringLiteral("https://covers.openlibrary.org/b/id/10522108-S.jpg?default=false"));
		QCOMPARE(OpenLibrary::coverUrl(base, 1, OpenLibrary::CoverSize::Medium).path(),
			QStringLiteral("/b/id/1-M.jpg"));
		QCOMPARE(OpenLibrary::coverUrl(base, 1, OpenLibrary::CoverSize::Large).path(),
			QStringLiteral("/b/id/1-L.jpg"));
		QVERIFY(OpenLibrary::coverUrl(base, 0, OpenLibrary::CoverSize::Large).isEmpty());
		QVERIFY(OpenLibrary::coverUrl(base, -1, OpenLibrary::CoverSize::Large).isEmpty());
	}

	void queryDescription()
	{
		QCOMPARE((OpenLibraryQuery{QStringLiteral("Dune"), QStringLiteral("Herbert"), QStringLiteral("123")}).describe(),
			QStringLiteral("title \"Dune\", author \"Herbert\", ISBN 123"));
		QCOMPARE((OpenLibraryQuery{QString(), QStringLiteral("Herbert"), QString()}).describe(),
			QStringLiteral("author \"Herbert\""));
		QVERIFY((OpenLibraryQuery{QStringLiteral("  "), QString(), QString()}).isEmpty());
		QVERIFY(!(OpenLibraryQuery{QString(), QString(), QStringLiteral("1")}).isEmpty());
	}

	// ---- Reading search results ----

	void parsesSearchResults()
	{
		QList<OpenLibraryMatch> matches;
		QString error;
		QVERIFY(OpenLibrary::parseSearchResults(test::samples::searchHobbit(), QString(), &matches, &error));
		QCOMPARE(matches.size(), 2);

		const OpenLibraryMatch& first = matches.at(0);
		QCOMPARE(first.workKey, QStringLiteral("/works/OL27482W"));
		QCOMPARE(first.editionKey, QStringLiteral("/books/OL10236414M"));
		// The edition's own title and subtitle, as printed on that book.
		QCOMPARE(first.title, QStringLiteral("The Hobbit: or There and Back Again"));
		QCOMPARE(first.authors, QStringList{QStringLiteral("J.R.R. Tolkien")});
		// ISBN-13 preferred when the user did not search for a specific one.
		QCOMPARE(first.isbn, QStringLiteral("9780261103344"));
		QCOMPARE(first.publishDate, QStringLiteral("2011"));
		// The edition's cover rather than the work's.
		QCOMPARE(first.coverId, 10522108);

		const OpenLibraryMatch& second = matches.at(1);
		QCOMPARE(second.title, QStringLiteral("The Hobbit"));
		QCOMPARE(second.authors.at(1), QStringLiteral("Sean Deming"));
		// No edition cover, so the work's.
		QCOMPARE(second.coverId, 8406766);
	}

	void searchedIsbnIsTheOneShown()
	{
		QList<OpenLibraryMatch> matches;
		QVERIFY(OpenLibrary::parseSearchResults(test::samples::searchHobbit(),
			QStringLiteral("0-261-10334-2"), &matches, nullptr));
		QCOMPARE(matches.at(0).isbn, QStringLiteral("0261103342"));
	}

	void workWithoutEditionsOrFields()
	{
		QList<OpenLibraryMatch> matches;
		QVERIFY(OpenLibrary::parseSearchResults(QByteArrayLiteral(
			R"({"docs": [{"key": "/works/OL1W", "title": "Bare"}, {"key": "/works/OL2W"},
				{"key": "/works/OL3W", "title": "Odd", "cover_i": -1, "editions": {"docs": []}}]})"),
			QString(), &matches, nullptr));
		// The result with no title is skipped.
		QCOMPARE(matches.size(), 2);
		QCOMPARE(matches.at(0).title, QStringLiteral("Bare"));
		QVERIFY(matches.at(0).editionKey.isEmpty());
		QVERIFY(matches.at(0).isbn.isEmpty());
		QVERIFY(matches.at(0).authors.isEmpty());
		QCOMPARE(matches.at(0).coverId, 0);
		QCOMPARE(matches.at(1).coverId, 0);
	}

	void malformedIsbnsAreSkipped()
	{
		QList<OpenLibraryMatch> matches;
		QVERIFY(OpenLibrary::parseSearchResults(QByteArrayLiteral(
			R"({"docs": [{"title": "X", "editions": {"docs": [{"key": "/books/OL1M", "isbn": ["abc", "123", "0306406152"]}]}}]})"),
			QString(), &matches, nullptr));
		QCOMPARE(matches.at(0).isbn, QStringLiteral("0306406152"));
	}

	void emptySearchIsNotAnError()
	{
		QList<OpenLibraryMatch> matches;
		QString error;
		QVERIFY(OpenLibrary::parseSearchResults(test::samples::searchNothing(), QString(), &matches, &error));
		QVERIFY(matches.isEmpty());
		QVERIFY(error.isEmpty());
	}

	void badSearchRepliesAreErrors_data()
	{
		QTest::addColumn<QByteArray>("json");
		QTest::newRow("not JSON") << QByteArrayLiteral("<html>Service Unavailable</html>");
		QTest::newRow("truncated") << QByteArrayLiteral(R"({"docs": [{"title": )");
		QTest::newRow("array") << QByteArrayLiteral("[1, 2]");
		QTest::newRow("no docs") << QByteArrayLiteral(R"({"error": "bad query"})");
		QTest::newRow("empty") << QByteArray();
	}

	void badSearchRepliesAreErrors()
	{
		QFETCH(QByteArray, json);
		QList<OpenLibraryMatch> matches{OpenLibraryMatch{}};
		QString error;
		QVERIFY(!OpenLibrary::parseSearchResults(json, QString(), &matches, &error));
		QVERIFY(!error.isEmpty());
		QVERIFY(matches.isEmpty());
	}

	// ---- Reading edition and work records ----

	void editionAndWorkFillTheBook()
	{
		QList<OpenLibraryMatch> matches;
		QVERIFY(OpenLibrary::parseSearchResults(test::samples::searchHobbit(), QString(), &matches, nullptr));
		OpenLibraryBook book = OpenLibrary::bookFromMatch(matches.at(0));
		QCOMPARE(book.releaseDate, QStringLiteral("2011"));

		QString error;
		QVERIFY(OpenLibrary::applyEdition(test::samples::editionHobbit(), &book, &error));
		QVERIFY(OpenLibrary::applyWork(test::samples::workHobbit(), &book, &error));

		QCOMPARE(book.title, QStringLiteral("The Hobbit: or There and Back Again"));
		QCOMPARE(book.authors, QStringList{QStringLiteral("J.R.R. Tolkien")});
		QCOMPARE(book.isbn, QStringLiteral("9780261103344"));
		QCOMPARE(book.releaseDate, QStringLiteral("2011"));
		QCOMPARE(book.pageCount, std::optional<int>(310));
		QCOMPARE(book.physicalFormat, QStringLiteral("Paperback"));
		// The edition's description wins over the work's, with Windows line
		// endings turned into plain ones.
		QCOMPARE(book.description, QStringLiteral("Bilbo Baggins is a hobbit.\n\n--back cover"));
		QCOMPARE(book.coverId, 10522108);
		// Edition and work subjects together, without repeats.
		QCOMPARE(book.subjects, (QStringList{QStringLiteral("Fantasy"), QStringLiteral("Dragons"),
			QStringLiteral("Fiction"), QStringLiteral("Middle Earth (Imaginary place)")}));
	}

	void workFillsOnlyWhatIsMissing()
	{
		OpenLibraryBook book;
		QVERIFY(OpenLibrary::applyEdition(QByteArrayLiteral(R"({"title": "Bare", "covers": [-1]})"), &book, nullptr));
		QVERIFY(OpenLibrary::applyWork(test::samples::workHobbit(), &book, nullptr));
		QCOMPARE(book.title, QStringLiteral("Bare"));
		QCOMPARE(book.description, QStringLiteral("The Hobbit is a tale of high adventure."));
		QCOMPARE(book.releaseDate, QStringLiteral("1938-01"));
		QCOMPARE(book.coverId, 14627509);
		QVERIFY(!book.pageCount);
	}

	void editionPlainStringDescriptionAndIsbn10()
	{
		OpenLibraryBook book;
		QVERIFY(OpenLibrary::applyEdition(QByteArrayLiteral(
			R"({"description": "  Plain text.  ", "isbn_10": ["0306406152"], "number_of_pages": 0})"), &book, nullptr));
		QCOMPARE(book.description, QStringLiteral("Plain text."));
		QCOMPARE(book.isbn, QStringLiteral("0306406152"));
		QVERIFY(!book.pageCount);
	}

	void badRecordsAreErrors()
	{
		OpenLibraryBook book;
		QString error;
		QVERIFY(!OpenLibrary::applyEdition(QByteArrayLiteral("nope"), &book, &error));
		QVERIFY(!error.isEmpty());
		error.clear();
		QVERIFY(!OpenLibrary::applyWork(QByteArrayLiteral("[]"), &book, &error));
		QVERIFY(!error.isEmpty());
	}

	// ---- Dates ----

	void normalizeDate_data()
	{
		QTest::addColumn<QString>("text");
		QTest::addColumn<QString>("expected");
		QTest::newRow("year") << "2011" << "2011";
		QTest::newRow("ISO day") << "2012-09-21" << "2012-09-21";
		QTest::newRow("ISO month") << "2012-09" << "2012-09";
		QTest::newRow("ISO short parts") << "2012-9-1" << "2012-09-01";
		QTest::newRow("year month day") << "1997 November 3" << "1997-11-03";
		QTest::newRow("US style") << "Sep 21, 2012" << "2012-09-21";
		QTest::newRow("Sept.") << "Sept. 5, 1990" << "1990-09-05";
		QTest::newRow("day month year") << "3 November 1997" << "1997-11-03";
		QTest::newRow("month year") << "March 1990" << "1990-03";
		QTest::newRow("year month") << "1990 March" << "1990-03";
		QTest::newRow("copyright c") << "c1985" << "1985";
		QTest::newRow("bracketed") << "[1990?]" << "1990";
		QTest::newRow("numeric slashes") << "12/05/2001" << "2001";
		QTest::newRow("impossible day") << "February 30, 2001" << "2001-02";
		QTest::newRow("leap day") << "February 29, 2000" << "2000-02-29";
		QTest::newRow("bad ISO month") << "2001-13-01" << "2001";
		QTest::newRow("no year") << "Spring" << "";
		QTest::newRow("empty") << "" << "";
		QTest::newRow("spaces") << "   " << "";
	}

	void normalizeDate()
	{
		QFETCH(QString, text);
		QFETCH(QString, expected);
		QCOMPARE(OpenLibrary::normalizeDate(text), expected);
	}

	// ---- Book types ----

	void bookTypeNameFor_data()
	{
		QTest::addColumn<QString>("format");
		QTest::addColumn<QString>("expected");
		QTest::newRow("paperback") << "Paperback" << "Paperback";
		QTest::newRow("trade paperback") << "Trade Paperback" << "Paperback";
		QTest::newRow("softcover") << "softcover" << "Paperback";
		QTest::newRow("mass market") << "Mass Market Paperback" << "Mass Market Paperback";
		QTest::newRow("mass-market") << "mass-market paperback" << "Mass Market Paperback";
		QTest::newRow("hardcover") << "Hardcover" << "Hardcover";
		QTest::newRow("hardback") << "Hardback" << "Hardcover";
		QTest::newRow("library binding") << "Library Binding" << "Hardcover";
		QTest::newRow("ebook") << "ebook" << "E-book";
		QTest::newRow("E-Book") << "E-Book" << "E-book";
		QTest::newRow("kindle") << "Kindle Edition" << "E-book";
		QTest::newRow("audio cd") << "Audio CD" << "Audiobook";
		QTest::newRow("unknown kept") << "  Board book " << "Board book";
		QTest::newRow("empty") << "" << "";
	}

	void bookTypeNameFor()
	{
		QFETCH(QString, format);
		QFETCH(QString, expected);
		QCOMPARE(OpenLibrary::bookTypeNameFor(format), expected);
	}
};

QTEST_GUILESS_MAIN(TestOpenLibrary)
#include "tst_open_library.moc"
