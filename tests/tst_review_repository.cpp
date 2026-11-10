// Tests for reviews and ratings (src/core/ReviewRepository.cpp).

#include "ReviewRepository.h"
#include "TestSupport.h"

using namespace mbt;

class TestReviewRepository : public QObject
{
	Q_OBJECT

private slots:
	void reviewCrud()
	{
		const auto database = test::freshDatabase();
		const QString bookId = test::addBook(*database, "Reviewed");
		const ReviewRepository reviews(*database);

		const QString id = reviews.addReview({QString(), bookId, "  Loved it.  ", "Sam", QString()});
		auto review = reviews.findReview(id);
		QVERIFY(review);
		QCOMPARE(review->text, QString("Loved it."));
		QCOMPARE(review->reviewer, QString("Sam"));
		QCOMPARE(review->bookId, bookId);
		QVERIFY(!review->createdAt.isEmpty());

		review->text = "Liked it.";
		review->reviewer.clear();
		reviews.updateReview(*review);
		review = reviews.findReview(id);
		QCOMPARE(review->text, QString("Liked it."));
		QVERIFY(review->reviewer.isEmpty());

		QVERIFY(reviews.removeReview(id));
		QVERIFY(!reviews.findReview(id));
		QVERIFY(!reviews.removeReview(id));
	}

	void reviewsNewestFirst()
	{
		const auto database = test::freshDatabase();
		const QString bookId = test::addBook(*database, "Reviewed");
		const ReviewRepository reviews(*database);
		reviews.addReview({QString(), bookId, "Old", QString(), "2020-01-01T00:00:00.000Z"});
		reviews.addReview({QString(), bookId, "New", QString(), "2025-01-01T00:00:00.000Z"});
		const QList<Review> list = reviews.reviews(bookId);
		QCOMPARE(list.size(), 2);
		QCOMPARE(list.at(0).text, QString("New"));
		QCOMPARE(list.at(1).text, QString("Old"));
	}

	void reviewValidation()
	{
		const auto database = test::freshDatabase();
		const QString bookId = test::addBook(*database, "Reviewed");
		const ReviewRepository reviews(*database);
		MBT_VERIFY_THROWS(reviews.addReview({QString(), bookId, "   ", QString(), QString()}));
		MBT_VERIFY_THROWS(reviews.addReview({QString(), "no-such-book", "Text", QString(), QString()}));
		MBT_VERIFY_THROWS(reviews.updateReview({"missing", bookId, "Text", QString(), QString()}));
	}

	void ratingCrud()
	{
		const auto database = test::freshDatabase();
		const QString bookId = test::addBook(*database, "Rated");
		const ReviewRepository reviews(*database);

		Rating rating;
		rating.bookId = bookId;
		rating.value = 3.5;
		rating.scale = 5;
		rating.reviewer = "Pat";
		const QString id = reviews.addRating(rating);

		auto stored = reviews.findRating(id);
		QVERIFY(stored);
		QCOMPARE(stored->value, 3.5);
		QCOMPARE(stored->scale, 5.0);
		QCOMPARE(stored->reviewer, QString("Pat"));

		stored->value = 8;
		stored->scale = 10;
		reviews.updateRating(*stored);
		stored = reviews.findRating(id);
		QCOMPARE(stored->value, 8.0);
		QCOMPARE(stored->scale, 10.0);

		QCOMPARE(reviews.ratings(bookId).size(), 1);
		QVERIFY(reviews.removeRating(id));
		QVERIFY(reviews.ratings(bookId).isEmpty());
	}

	void ratingValidation_data()
	{
		QTest::addColumn<double>("value");
		QTest::addColumn<double>("scale");
		QTest::addColumn<bool>("valid");
		QTest::newRow("zero") << 0.0 << 5.0 << true;
		QTest::newRow("top of scale") << 5.0 << 5.0 << true;
		QTest::newRow("half") << 2.5 << 5.0 << true;
		QTest::newRow("negative") << -1.0 << 5.0 << false;
		QTest::newRow("above scale") << 6.0 << 5.0 << false;
		QTest::newRow("zero scale") << 0.0 << 0.0 << false;
		QTest::newRow("negative scale") << 0.0 << -5.0 << false;
	}

	void ratingValidation()
	{
		QFETCH(double, value);
		QFETCH(double, scale);
		QFETCH(bool, valid);
		const auto database = test::freshDatabase();
		const QString bookId = test::addBook(*database, "Rated");
		Rating rating;
		rating.bookId = bookId;
		rating.value = value;
		rating.scale = scale;
		if (valid) {
			ReviewRepository(*database).addRating(rating);
			QCOMPARE(test::rowCount(*database, "ratings"), 1);
		} else {
			MBT_VERIFY_THROWS(ReviewRepository(*database).addRating(rating));
			QCOMPARE(test::rowCount(*database, "ratings"), 0);
		}
	}

	void ratingCheckConstraintBackstopsValidation()
	{
		// Even SQL that bypasses the repository cannot store a bad rating.
		const auto database = test::freshDatabase();
		const QString bookId = test::addBook(*database, "Rated");
		QSqlQuery query(database->connection());
		QVERIFY(!query.exec(QStringLiteral(
			"INSERT INTO ratings (id, book_id, rating_value, rating_scale, created_at) "
			"VALUES ('x', '%1', 9, 5, 'now')").arg(bookId)));
	}

	void formatRatingText()
	{
		QCOMPARE(formatRating(4, 5), QString("4/5"));
		QCOMPARE(formatRating(3.5, 5), QString("3.5/5"));
		QCOMPARE(formatRating(8, 10), QString("8/10"));
	}
};

QTEST_GUILESS_MAIN(TestReviewRepository)
#include "tst_review_repository.moc"
