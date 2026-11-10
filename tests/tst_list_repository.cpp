// Tests for user-created book lists (src/core/ListRepository.cpp).

#include "ListRepository.h"
#include "TestSupport.h"

using namespace mbt;

namespace {

QString makeList(const ListRepository& lists, const QString& name, const QString& description = {})
{
	BookList list;
	list.name = name;
	list.description = description;
	return lists.create(list);
}

} // namespace

class TestListRepository : public QObject
{
	Q_OBJECT

private slots:
	void createFindUpdateRemove()
	{
		const auto database = test::freshDatabase();
		const ListRepository lists(*database);
		const QString id = makeList(lists, " Favorites ", "The best ones");

		auto list = lists.find(id);
		QVERIFY(list);
		QCOMPARE(list->name, QString("Favorites"));
		QCOMPARE(list->description, QString("The best ones"));
		QVERIFY(!list->createdAt.isEmpty());
		QCOMPARE(list->bookCount, 0);

		list->name = "All-Time Favorites";
		list->description.clear();
		lists.update(*list);
		list = lists.find(id);
		QCOMPARE(list->name, QString("All-Time Favorites"));
		QVERIFY(list->description.isEmpty());
		QCOMPARE(lists.findByName("all-time favorites")->id, id);

		QVERIFY(lists.remove(id));
		QVERIFY(!lists.find(id));
		QVERIFY(!lists.remove(id));
	}

	void validation()
	{
		const auto database = test::freshDatabase();
		const ListRepository lists(*database);
		MBT_VERIFY_THROWS(makeList(lists, "  "));
		makeList(lists, "Taken");
		MBT_VERIFY_THROWS(makeList(lists, "TAKEN"));
		BookList missing;
		missing.id = "missing";
		missing.name = "Name";
		MBT_VERIFY_THROWS(lists.update(missing));
	}

	void addAndRemoveBooks()
	{
		const auto database = test::freshDatabase();
		const ListRepository lists(*database);
		const QString listId = makeList(lists, "To Buy");
		const QString a = test::addBook(*database, "A");
		const QString b = test::addBook(*database, "B");
		const QString c = test::addBook(*database, "C");

		QVERIFY(lists.addBook(listId, c));
		QVERIFY(lists.addBook(listId, a));
		QVERIFY(lists.addBook(listId, b));
		// Adding again is a no-op, not an error.
		QVERIFY(!lists.addBook(listId, a));

		QCOMPARE(lists.bookIds(listId), QList<QString>({c, a, b}));
		QCOMPARE(lists.find(listId)->bookCount, 3);

		QVERIFY(lists.removeBook(listId, a));
		QVERIFY(!lists.removeBook(listId, a));
		QCOMPARE(lists.bookIds(listId), QList<QString>({c, b}));

		// A re-added book goes to the end.
		QVERIFY(lists.addBook(listId, a));
		QCOMPARE(lists.bookIds(listId), QList<QString>({c, b, a}));

		// Removing from a list never deletes the book itself.
		QCOMPARE(test::rowCount(*database, "books"), 3);
	}

	void addingUnknownBookFails()
	{
		const auto database = test::freshDatabase();
		const ListRepository lists(*database);
		const QString listId = makeList(lists, "List");
		MBT_VERIFY_THROWS(lists.addBook(listId, "no-such-book"));
	}

	void deletingListKeepsBooks()
	{
		const auto database = test::freshDatabase();
		const ListRepository lists(*database);
		const QString listId = makeList(lists, "Temporary");
		lists.addBook(listId, test::addBook(*database, "Survivor"));
		QVERIFY(lists.remove(listId));
		QCOMPARE(test::rowCount(*database, "list_books"), 0);
		QCOMPARE(test::rowCount(*database, "books"), 1);
	}

	void allSortedWithCounts()
	{
		const auto database = test::freshDatabase();
		const ListRepository lists(*database);
		const QString wish = makeList(lists, "wishlist");
		const QString fav = makeList(lists, "Favorites");
		lists.addBook(fav, test::addBook(*database, "One"));
		lists.addBook(fav, test::addBook(*database, "Two"));

		const QList<BookList> all = lists.all();
		QCOMPARE(all.size(), 2);
		QCOMPARE(all.at(0).id, fav);
		QCOMPARE(all.at(0).bookCount, 2);
		QCOMPARE(all.at(1).id, wish);
		QCOMPARE(all.at(1).bookCount, 0);
	}

	void listsForBook()
	{
		const auto database = test::freshDatabase();
		const ListRepository lists(*database);
		const QString bookId = test::addBook(*database, "Popular");
		const QString z = makeList(lists, "Zebra");
		const QString a = makeList(lists, "Antelope");
		makeList(lists, "Unrelated");
		lists.addBook(z, bookId);
		lists.addBook(a, bookId);
		const QList<BookList> result = lists.listsForBook(bookId);
		QCOMPARE(result.size(), 2);
		QCOMPARE(result.at(0).name, QString("Antelope"));
		QCOMPARE(result.at(1).name, QString("Zebra"));
	}

	void findOrCreate()
	{
		const auto database = test::freshDatabase();
		const ListRepository lists(*database);
		QCOMPARE(lists.findOrCreate("Reading Group"), lists.findOrCreate("reading group"));
		QCOMPARE(lists.all().size(), 1);
	}
};

QTEST_GUILESS_MAIN(TestListRepository)
#include "tst_list_repository.moc"
