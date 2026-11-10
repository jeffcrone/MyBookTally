// Tests for ISBN normalization and validation (src/core/Isbn.cpp).

#include "Isbn.h"

#include <QTest>

using namespace mbt;

class TestIsbn : public QObject
{
	Q_OBJECT

private slots:
	void normalize_data()
	{
		QTest::addColumn<QString>("input");
		QTest::addColumn<QString>("expected");
		QTest::newRow("hyphens") << "978-0-306-40615-7" << "9780306406157";
		QTest::newRow("spaces") << "0 306 40615 2" << "0306406152";
		QTest::newRow("lowercase x") << "0-8044-2957-x" << "080442957X";
		QTest::newRow("already clean") << "9780306406157" << "9780306406157";
		QTest::newRow("empty") << "" << "";
		QTest::newRow("surrounding whitespace") << "  9780306406157\t" << "9780306406157";
	}

	void normalize()
	{
		QFETCH(QString, input);
		QFETCH(QString, expected);
		QCOMPARE(Isbn::normalize(input), expected);
	}

	void wellFormed_data()
	{
		QTest::addColumn<QString>("input");
		QTest::addColumn<bool>("expected");
		QTest::newRow("isbn13") << "9780306406157" << true;
		QTest::newRow("isbn13 with hyphens") << "978-0-306-40615-7" << true;
		QTest::newRow("isbn10") << "0306406152" << true;
		QTest::newRow("isbn10 ending in X") << "080442957X" << true;
		QTest::newRow("isbn13 bad checksum is still well formed") << "9780306406158" << true;
		QTest::newRow("empty") << "" << false;
		QTest::newRow("too short") << "030640615" << false;
		QTest::newRow("too long") << "97803064061570" << false;
		QTest::newRow("eleven digits") << "03064061521" << false;
		QTest::newRow("X not last") << "03064X6152" << false;
		QTest::newRow("X in isbn13") << "978030640615X" << false;
		QTest::newRow("letters") << "ABCDEFGHIJ" << false;
		// Arabic-Indic digits are "digits" to Unicode but not to an ISBN.
		QTest::newRow("non-ASCII digits") << QString::fromUtf8("٠٣٠٦٤٠٦١٥٢") << false;
	}

	void wellFormed()
	{
		QFETCH(QString, input);
		QFETCH(bool, expected);
		QCOMPARE(Isbn::isWellFormed(input), expected);
	}

	void checksum_data()
	{
		QTest::addColumn<QString>("input");
		QTest::addColumn<bool>("expected");
		QTest::newRow("valid isbn13") << "9780306406157" << true;
		QTest::newRow("valid isbn13 hyphenated") << "978-1-86197-876-9" << true;
		QTest::newRow("invalid isbn13") << "9780306406158" << false;
		QTest::newRow("valid isbn10") << "0306406152" << true;
		QTest::newRow("valid isbn10 with X") << "080442957X" << true;
		QTest::newRow("invalid isbn10") << "0306406153" << false;
		QTest::newRow("malformed") << "12345" << false;
		QTest::newRow("empty") << "" << false;
	}

	void checksum()
	{
		QFETCH(QString, input);
		QFETCH(bool, expected);
		QCOMPARE(Isbn::hasValidChecksum(input), expected);
	}
};

QTEST_GUILESS_MAIN(TestIsbn)
#include "tst_isbn.moc"
