#include "Isbn.h"

namespace mbt::Isbn {

QString normalize(const QString& isbn)
{
	QString result;
	result.reserve(isbn.size());
	for (const QChar c : isbn) {
		if (c == QLatin1Char('-') || c.isSpace())
			continue;
		result.append(c == QLatin1Char('x') ? QLatin1Char('X') : c);
	}
	return result;
}

bool isWellFormed(const QString& isbn)
{
	const QString value = normalize(isbn);
	auto allDigits = [&value](int count) {
		for (int i = 0; i < count; ++i) {
			// isDigit() alone would also accept digits from other scripts
			// (Arabic-Indic and so on), which an ISBN never contains.
			const QChar c = value.at(i);
			if (c < QLatin1Char('0') || c > QLatin1Char('9'))
				return false;
		}
		return true;
	};

	if (value.size() == 13)
		return allDigits(13);
	if (value.size() == 10) {
		const QChar last = value.at(9);
		const bool lastOk = last == QLatin1Char('X')
			|| (last >= QLatin1Char('0') && last <= QLatin1Char('9'));
		return allDigits(9) && lastOk;
	}
	return false;
}

bool hasValidChecksum(const QString& isbn)
{
	if (!isWellFormed(isbn))
		return false;
	const QString value = normalize(isbn);

	if (value.size() == 10) {
		// ISBN-10: weights 10 down to 1; the sum must be a multiple of 11.
		// X stands for 10 and is only allowed as the check digit.
		int sum = 0;
		for (int i = 0; i < 10; ++i) {
			const QChar c = value.at(i);
			const int digit = c == QLatin1Char('X') ? 10 : c.digitValue();
			sum += digit * (10 - i);
		}
		return sum % 11 == 0;
	}

	// ISBN-13: alternating weights 1 and 3; the sum must be a multiple of 10.
	int sum = 0;
	for (int i = 0; i < 13; ++i)
		sum += value.at(i).digitValue() * (i % 2 == 0 ? 1 : 3);
	return sum % 10 == 0;
}

} // namespace mbt::Isbn
