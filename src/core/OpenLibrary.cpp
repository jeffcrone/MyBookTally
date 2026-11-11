#include "OpenLibrary.h"

#include "Isbn.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QUrlQuery>

namespace mbt {

namespace {

// The fields search.json is asked to return. Naming them keeps replies
// small (a popular work otherwise comes back with hundreds of ISBNs and
// every subject), which is kinder to Open Library and quicker for the user.
// "editions" asks for the edition that best matches the search, nested
// inside each work.
const char* const searchFields =
	"key,title,subtitle,author_name,cover_i,"
	"editions,editions.key,editions.title,editions.subtitle,editions.isbn,"
	"editions.cover_i,editions.publish_date";

// Keys look like "/works/OL27482W" or "/books/OL10236414M".
const QRegularExpression recordKeyPattern(QStringLiteral(R"(^/(works/OL\d+W|books/OL\d+M)$)"));

bool parseObject(const QByteArray& json, QJsonObject* object, QString* error)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		if (error)
			*error = QStringLiteral("The reply is not valid JSON: %1 (at byte %2).")
				.arg(parseError.errorString()).arg(parseError.offset);
		return false;
	}
	if (!document.isObject()) {
		if (error)
			*error = QStringLiteral("The reply is JSON, but not the object Open Library sends.");
		return false;
	}
	*object = document.object();
	return true;
}

// Titles sometimes arrive with doubled or stray spaces ("The  Hobbit").
QString cleanText(const QString& text)
{
	return text.simplified();
}

// "Title: Subtitle", since the app has one title field and a subtitle is
// often what tells two books apart ("Sapiens: A Brief History...").
QString joinTitle(const QJsonObject& object)
{
	const QString title = cleanText(object.value(QStringLiteral("title")).toString());
	const QString subtitle = cleanText(object.value(QStringLiteral("subtitle")).toString());
	if (title.isEmpty() || subtitle.isEmpty())
		return title;
	return QStringLiteral("%1: %2").arg(title, subtitle);
}

QStringList stringList(const QJsonValue& value)
{
	QStringList result;
	if (value.isString()) {
		result.append(value.toString());
		return result;
	}
	for (const QJsonValue& item : value.toArray()) {
		if (item.isString())
			result.append(item.toString());
	}
	return result;
}

// Descriptions are either a plain string or {"type": "/type/text",
// "value": "..."}, depending on how the record was edited.
QString textValue(const QJsonValue& value)
{
	QString text = value.isObject() ? value.toObject().value(QStringLiteral("value")).toString()
									: value.toString();
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
	return text.trimmed();
}

// Picks the ISBN to show and save from an edition's list.
QString chooseIsbn(const QStringList& isbns, const QString& searchedIsbn)
{
	const QString wanted = Isbn::normalize(searchedIsbn);
	QString firstThirteen;
	QString first;
	for (const QString& raw : isbns) {
		const QString isbn = Isbn::normalize(raw);
		if (!Isbn::isWellFormed(isbn))
			continue;
		if (!wanted.isEmpty() && isbn == wanted)
			return isbn;
		if (firstThirteen.isEmpty() && isbn.size() == 13)
			firstThirteen = isbn;
		if (first.isEmpty())
			first = isbn;
	}
	return firstThirteen.isEmpty() ? first : firstThirteen;
}

// Open Library uses -1 in "covers" for a cover that was removed.
int firstCoverId(const QJsonValue& covers)
{
	for (const QJsonValue& value : covers.toArray()) {
		const int id = value.toInt();
		if (id > 0)
			return id;
	}
	return 0;
}

void appendSubjects(const QJsonValue& value, QStringList* subjects)
{
	for (const QString& subject : stringList(value)) {
		const QString clean = cleanText(subject);
		if (!clean.isEmpty() && !subjects->contains(clean, Qt::CaseInsensitive))
			subjects->append(clean);
	}
}

// The month a word names, accepting full names and abbreviations of at
// least three letters ("Sep", "Sept", "September"). 0 if it is not one.
int monthFromWord(const QString& word)
{
	static const QStringList months = {
		QStringLiteral("january"), QStringLiteral("february"), QStringLiteral("march"),
		QStringLiteral("april"), QStringLiteral("may"), QStringLiteral("june"),
		QStringLiteral("july"), QStringLiteral("august"), QStringLiteral("september"),
		QStringLiteral("october"), QStringLiteral("november"), QStringLiteral("december"),
	};
	const QString lower = word.toLower();
	if (lower.size() < 3)
		return 0;
	for (int i = 0; i < months.size(); ++i) {
		if (months.at(i).startsWith(lower))
			return i + 1;
	}
	return 0;
}

} // namespace

QString OpenLibraryQuery::describe() const
{
	QStringList parts;
	if (!title.trimmed().isEmpty())
		parts.append(QStringLiteral("title \"%1\"").arg(title.trimmed()));
	if (!author.trimmed().isEmpty())
		parts.append(QStringLiteral("author \"%1\"").arg(author.trimmed()));
	if (!isbn.trimmed().isEmpty())
		parts.append(QStringLiteral("ISBN %1").arg(isbn.trimmed()));
	return parts.join(QStringLiteral(", "));
}

namespace OpenLibrary {

QUrl defaultApiBase()
{
	return QUrl(QStringLiteral("https://openlibrary.org"));
}

QUrl defaultCoversBase()
{
	return QUrl(QStringLiteral("https://covers.openlibrary.org"));
}

QUrl searchUrl(const QUrl& apiBase, const OpenLibraryQuery& query)
{
	QUrlQuery items;
	const QString isbn = Isbn::normalize(query.isbn.trimmed());
	if (Isbn::isWellFormed(isbn)) {
		items.addQueryItem(QStringLiteral("isbn"), isbn);
	} else {
		if (!query.title.trimmed().isEmpty())
			items.addQueryItem(QStringLiteral("title"), query.title.trimmed());
		if (!query.author.trimmed().isEmpty())
			items.addQueryItem(QStringLiteral("author"), query.author.trimmed());
	}
	items.addQueryItem(QStringLiteral("fields"), QString::fromLatin1(searchFields));
	items.addQueryItem(QStringLiteral("limit"), QString::number(SearchLimit));

	QUrl url(apiBase);
	url.setPath(apiBase.path() + QStringLiteral("/search.json"));
	url.setQuery(items);
	return url;
}

QUrl recordUrl(const QUrl& apiBase, const QString& key)
{
	if (!recordKeyPattern.match(key).hasMatch())
		return QUrl();
	QUrl url(apiBase);
	url.setPath(apiBase.path() + key + QStringLiteral(".json"));
	return url;
}

QUrl coverUrl(const QUrl& coversBase, int coverId, CoverSize size)
{
	if (coverId <= 0)
		return QUrl();
	const QChar letter = size == CoverSize::Small ? QLatin1Char('S')
		: size == CoverSize::Medium ? QLatin1Char('M') : QLatin1Char('L');
	QUrl url(coversBase);
	url.setPath(coversBase.path() + QStringLiteral("/b/id/%1-%2.jpg").arg(coverId).arg(letter));
	// Without this a missing cover comes back as a blank 1x1 image, which
	// would be saved as the book's cover; with it, a "not found" error.
	url.setQuery(QStringLiteral("default=false"));
	return url;
}

bool parseSearchResults(const QByteArray& json, const QString& searchedIsbn,
	QList<OpenLibraryMatch>* matches, QString* error)
{
	matches->clear();
	QJsonObject root;
	if (!parseObject(json, &root, error))
		return false;
	const QJsonValue docs = root.value(QStringLiteral("docs"));
	if (!docs.isArray()) {
		if (error)
			*error = QStringLiteral("The reply has no \"docs\" list of results.");
		return false;
	}

	for (const QJsonValue& value : docs.toArray()) {
		const QJsonObject work = value.toObject();
		OpenLibraryMatch match;
		match.workKey = work.value(QStringLiteral("key")).toString();
		match.title = joinTitle(work);
		for (const QString& name : stringList(work.value(QStringLiteral("author_name")))) {
			const QString clean = cleanText(name);
			if (!clean.isEmpty())
				match.authors.append(clean);
		}
		match.coverId = work.value(QStringLiteral("cover_i")).toInt();

		// The best-matching edition, when there is one, describes the
		// printing the user is holding better than the work does.
		const QJsonArray editions = work.value(QStringLiteral("editions")).toObject()
			.value(QStringLiteral("docs")).toArray();
		if (!editions.isEmpty()) {
			const QJsonObject edition = editions.first().toObject();
			match.editionKey = edition.value(QStringLiteral("key")).toString();
			const QString editionTitle = joinTitle(edition);
			if (!editionTitle.isEmpty())
				match.title = editionTitle;
			match.isbn = chooseIsbn(stringList(edition.value(QStringLiteral("isbn"))), searchedIsbn);
			const QStringList dates = stringList(edition.value(QStringLiteral("publish_date")));
			if (!dates.isEmpty())
				match.publishDate = dates.first().trimmed();
			const int editionCover = edition.value(QStringLiteral("cover_i")).toInt();
			if (editionCover > 0)
				match.coverId = editionCover;
		}
		if (match.coverId < 0)
			match.coverId = 0;

		if (!match.title.isEmpty())
			matches->append(match);
	}
	return true;
}

OpenLibraryBook bookFromMatch(const OpenLibraryMatch& match)
{
	OpenLibraryBook book;
	book.title = match.title;
	book.authors = match.authors;
	book.isbn = match.isbn;
	book.releaseDate = normalizeDate(match.publishDate);
	book.coverId = match.coverId;
	return book;
}

bool applyEdition(const QByteArray& json, OpenLibraryBook* book, QString* error)
{
	QJsonObject edition;
	if (!parseObject(json, &edition, error))
		return false;

	const QString title = joinTitle(edition);
	if (!title.isEmpty())
		book->title = title;
	if (book->isbn.isEmpty()) {
		QStringList isbns = stringList(edition.value(QStringLiteral("isbn_13")));
		isbns += stringList(edition.value(QStringLiteral("isbn_10")));
		book->isbn = chooseIsbn(isbns, QString());
	}
	const QString date = normalizeDate(edition.value(QStringLiteral("publish_date")).toString());
	if (!date.isEmpty())
		book->releaseDate = date;
	const int pages = edition.value(QStringLiteral("number_of_pages")).toInt();
	if (pages > 0)
		book->pageCount = pages;
	book->physicalFormat = cleanText(edition.value(QStringLiteral("physical_format")).toString());
	const QString description = textValue(edition.value(QStringLiteral("description")));
	if (!description.isEmpty())
		book->description = description;
	appendSubjects(edition.value(QStringLiteral("subjects")), &book->subjects);
	const int cover = firstCoverId(edition.value(QStringLiteral("covers")));
	if (cover > 0)
		book->coverId = cover;
	return true;
}

bool applyWork(const QByteArray& json, OpenLibraryBook* book, QString* error)
{
	QJsonObject work;
	if (!parseObject(json, &work, error))
		return false;

	if (book->title.isEmpty())
		book->title = joinTitle(work);
	if (book->description.isEmpty())
		book->description = textValue(work.value(QStringLiteral("description")));
	if (book->releaseDate.isEmpty())
		book->releaseDate = normalizeDate(work.value(QStringLiteral("first_publish_date")).toString());
	appendSubjects(work.value(QStringLiteral("subjects")), &book->subjects);
	if (book->coverId <= 0)
		book->coverId = firstCoverId(work.value(QStringLiteral("covers")));
	return true;
}

QString normalizeDate(const QString& text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty())
		return QString();

	int year = 0;
	int month = 0;
	int day = 0;

	static const QRegularExpression isoPattern(QStringLiteral(R"(^(\d{4})-(\d{1,2})(?:-(\d{1,2}))?\b)"));
	const QRegularExpressionMatch iso = isoPattern.match(trimmed);
	if (iso.hasMatch()) {
		year = iso.captured(1).toInt();
		month = iso.captured(2).toInt();
		day = iso.captured(3).toInt();
	} else {
		// Word by word: a four-digit number is the year, a month name the
		// month, and a one- or two-digit number the day - but only once a
		// month name has been seen somewhere, since "12 2001" alone is
		// not a day.
		static const QRegularExpression separators(QStringLiteral(R"([^A-Za-z0-9]+)"));
		int dayCandidate = 0;
		for (const QString& word : trimmed.split(separators, Qt::SkipEmptyParts)) {
			bool isNumber = false;
			const int number = word.toInt(&isNumber);
			if (isNumber && word.size() == 4 && year == 0)
				year = number;
			else if (isNumber && word.size() <= 2 && dayCandidate == 0)
				dayCandidate = number;
			else if (!isNumber && month == 0)
				month = monthFromWord(word);
		}
		if (month != 0)
			day = dayCandidate;
	}

	// No clean year: take any four-digit year in the text, as in "c1985"
	// or "[1990?]".
	if (year == 0) {
		static const QRegularExpression anyYear(QStringLiteral(R"((?<!\d)(1\d{3}|20\d{2})(?!\d))"));
		const QRegularExpressionMatch found = anyYear.match(trimmed);
		if (!found.hasMatch())
			return QString();
		return found.captured(1);
	}
	if (year < 1000 || year > 2999)
		return QString();

	const QString yearText = QStringLiteral("%1").arg(year, 4, 10, QLatin1Char('0'));
	if (month < 1 || month > 12)
		return yearText;
	const QString monthText = QStringLiteral("%1-%2").arg(yearText).arg(month, 2, 10, QLatin1Char('0'));
	if (day < 1 || !QDate(year, month, day).isValid())
		return monthText;
	return QStringLiteral("%1-%2").arg(monthText).arg(day, 2, 10, QLatin1Char('0'));
}

QString bookTypeNameFor(const QString& physicalFormat)
{
	// Compare letters only, so "Mass-market paperback", "Mass Market
	// Paperback" and "mass market paperback" are all the same thing.
	QString letters;
	for (const QChar c : physicalFormat.toLower()) {
		if (c.isLetter())
			letters.append(c);
	}
	if (letters.contains(QLatin1String("massmarket")))
		return QStringLiteral("Mass Market Paperback");
	if (letters.contains(QLatin1String("hardcover")) || letters.contains(QLatin1String("hardback"))
		|| letters.contains(QLatin1String("hardbound")) || letters.contains(QLatin1String("librarybinding")))
		return QStringLiteral("Hardcover");
	if (letters.contains(QLatin1String("paperback")) || letters.contains(QLatin1String("softcover"))
		|| letters.contains(QLatin1String("softback")) || letters.contains(QLatin1String("tradepaper")))
		return QStringLiteral("Paperback");
	if (letters.contains(QLatin1String("ebook")) || letters.contains(QLatin1String("electronic"))
		|| letters.contains(QLatin1String("kindle")) || letters.contains(QLatin1String("epub")))
		return QStringLiteral("E-book");
	if (letters.contains(QLatin1String("audio")))
		return QStringLiteral("Audiobook");
	return physicalFormat.trimmed();
}

} // namespace OpenLibrary

} // namespace mbt
