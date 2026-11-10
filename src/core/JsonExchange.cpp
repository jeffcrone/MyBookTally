#include "JsonExchange.h"

#include "BookRepository.h"
#include "Database.h"
#include "DatabaseError.h"
#include "ListRepository.h"
#include "LookupRepository.h"
#include "ReviewRepository.h"
#include "SqlUtil.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace mbt {

namespace {

// JSON key for each single-link lookup on a book.
struct SingleLink
{
	LookupKind kind;
	const char* key;
	QString Book::*field;
};

const SingleLink singleLinks[] = {
	{LookupKind::BookType, "bookType", &Book::bookTypeId},
	{LookupKind::OwnershipStatus, "ownershipStatus", &Book::ownershipStatusId},
	{LookupKind::ReadingStatus, "readingStatus", &Book::readingStatusId},
};

QString key(const char* name)
{
	return QString::fromLatin1(name);
}

// Adds "key": text only when text is set, so exports do not fill up with
// empty strings and an absent key reads clearly as "not set".
void putText(QJsonObject& object, const char* name, const QString& text)
{
	if (!text.isEmpty())
		object.insert(key(name), text);
}

QJsonObject lookupToJson(const LookupItem& item, bool includeIds)
{
	QJsonObject object;
	if (includeIds)
		object.insert(QStringLiteral("id"), item.id);
	object.insert(QStringLiteral("name"), item.name);
	putText(object, "description", item.description);
	return object;
}

// ---- Import helpers -------------------------------------------------------

QString text(const QJsonObject& object, const char* name)
{
	return object.value(key(name)).toString().trimmed();
}

// Finds or creates the lookup row described by object; see the matching
// rules in JsonExchange.h. Returns an empty string for an object with no
// name (which is skipped).
QString resolveLookup(const LookupRepository& repository, const QJsonObject& object)
{
	const QString id = text(object, "id");
	const QString name = text(object, "name");
	if (!id.isEmpty() && repository.find(id))
		return id;
	if (name.isEmpty())
		return QString();
	if (const auto existing = repository.findByName(name))
		return existing->id;
	return repository.insert({id, name, object.value(QStringLiteral("description")).toString()});
}

QString resolveAuthor(const AuthorRepository& repository, const QJsonObject& object)
{
	const QString id = text(object, "id");
	const QString name = text(object, "name");
	if (!id.isEmpty() && repository.find(id))
		return id;
	if (name.isEmpty())
		return QString();
	if (const auto existing = repository.findByName(name))
		return existing->id;
	return repository.insert({id, name, object.value(QStringLiteral("bio")).toString(),
		text(object, "birthDate")});
}

QString resolveList(const ListRepository& repository, const QJsonObject& object)
{
	const QString id = text(object, "id");
	const QString name = text(object, "name");
	if (!id.isEmpty() && repository.find(id))
		return id;
	if (name.isEmpty())
		return QString();
	if (const auto existing = repository.findByName(name))
		return existing->id;
	BookList list;
	list.id = id;
	list.name = name;
	list.description = object.value(QStringLiteral("description")).toString();
	return repository.create(list);
}

QString normalizedNames(const QStringList& names)
{
	QStringList lowered;
	for (const QString& name : names)
		lowered.append(name.trimmed().toLower());
	return lowered.join(QLatin1Char('\n'));
}

} // namespace

JsonExchange::JsonExchange(const Database& database)
	: m_database(database)
{
}

QJsonDocument JsonExchange::exportBooks(const QList<QString>& bookIds, const ExportOptions& options) const
{
	const BookRepository books(m_database);
	const AuthorRepository authors(m_database);
	const ReviewRepository reviews(m_database);
	const ListRepository lists(m_database);
	const LookupRepository genres(m_database, LookupKind::Genre);
	const LookupRepository series(m_database, LookupKind::Series);

	QJsonArray bookArray;
	for (const QString& bookId : bookIds) {
		const auto book = books.find(bookId);
		if (!book)
			continue;

		QJsonObject object;
		if (options.includeIds)
			object.insert(QStringLiteral("id"), book->id);
		object.insert(QStringLiteral("title"), book->title);

		QJsonArray authorArray;
		for (const QString& authorId : book->authorIds) {
			const auto author = authors.find(authorId);
			if (!author)
				continue;
			QJsonObject a;
			if (options.includeIds)
				a.insert(QStringLiteral("id"), author->id);
			a.insert(QStringLiteral("name"), author->name);
			putText(a, "bio", author->bio);
			putText(a, "birthDate", author->birthDate);
			authorArray.append(a);
		}
		object.insert(QStringLiteral("authors"), authorArray);

		QJsonArray seriesArray;
		for (const SeriesEntry& entry : book->series) {
			const auto item = series.find(entry.seriesId);
			if (!item)
				continue;
			QJsonObject s = lookupToJson(*item, options.includeIds);
			if (entry.position)
				s.insert(QStringLiteral("position"), *entry.position);
			putText(s, "positionLabel", entry.positionLabel);
			seriesArray.append(s);
		}
		object.insert(QStringLiteral("series"), seriesArray);

		QJsonArray genreArray;
		for (const QString& genreId : book->genreIds) {
			if (const auto item = genres.find(genreId))
				genreArray.append(lookupToJson(*item, options.includeIds));
		}
		object.insert(QStringLiteral("genres"), genreArray);

		for (const SingleLink& link : singleLinks) {
			const QString& id = (*book).*(link.field);
			if (id.isEmpty())
				continue;
			if (const auto item = LookupRepository(m_database, link.kind).find(id))
				object.insert(key(link.key), lookupToJson(*item, options.includeIds));
		}

		putText(object, "releaseDate", book->releaseDate);
		putText(object, "isbn", book->isbn);
		if (book->pageCount)
			object.insert(QStringLiteral("pageCount"), *book->pageCount);
		putText(object, "location", book->location);
		putText(object, "description", book->description);
		object.insert(QStringLiteral("isPrivate"), book->isPrivate);
		if (options.includeCovers && !book->coverImage.isEmpty())
			object.insert(QStringLiteral("coverImage"),
				QString::fromLatin1(book->coverImage.toBase64()));
		object.insert(QStringLiteral("createdAt"), book->createdAt);
		object.insert(QStringLiteral("updatedAt"), book->updatedAt);

		QJsonArray reviewArray;
		for (const Review& review : reviews.reviews(book->id)) {
			QJsonObject r;
			if (options.includeIds)
				r.insert(QStringLiteral("id"), review.id);
			r.insert(QStringLiteral("text"), review.text);
			putText(r, "reviewer", review.reviewer);
			r.insert(QStringLiteral("createdAt"), review.createdAt);
			reviewArray.append(r);
		}
		object.insert(QStringLiteral("reviews"), reviewArray);

		QJsonArray ratingArray;
		for (const Rating& rating : reviews.ratings(book->id)) {
			QJsonObject r;
			if (options.includeIds)
				r.insert(QStringLiteral("id"), rating.id);
			r.insert(QStringLiteral("value"), rating.value);
			r.insert(QStringLiteral("scale"), rating.scale);
			putText(r, "reviewer", rating.reviewer);
			r.insert(QStringLiteral("createdAt"), rating.createdAt);
			ratingArray.append(r);
		}
		object.insert(QStringLiteral("ratings"), ratingArray);

		QJsonArray listArray;
		for (const BookList& list : lists.listsForBook(book->id)) {
			QJsonObject l;
			if (options.includeIds)
				l.insert(QStringLiteral("id"), list.id);
			l.insert(QStringLiteral("name"), list.name);
			putText(l, "description", list.description);
			listArray.append(l);
		}
		object.insert(QStringLiteral("lists"), listArray);

		bookArray.append(object);
	}

	QJsonObject root;
	root.insert(QStringLiteral("format"), QStringLiteral("MyBookTally"));
	root.insert(QStringLiteral("formatVersion"), FormatVersion);
	root.insert(QStringLiteral("appVersion"), QStringLiteral(MYBOOKTALLY_VERSION));
	root.insert(QStringLiteral("exportedAt"), nowUtc());
	root.insert(QStringLiteral("includesIds"), options.includeIds);
	root.insert(QStringLiteral("books"), bookArray);
	return QJsonDocument(root);
}

ImportResult JsonExchange::importBooks(const QJsonDocument& document) const
{
	const QJsonObject root = document.object();
	if (!document.isObject() || root.value(QStringLiteral("format")).toString() != QLatin1String("MyBookTally"))
		throw DatabaseError(QStringLiteral("This file is not a MyBookTally export."));
	const int version = root.value(QStringLiteral("formatVersion")).toInt(0);
	if (version < 1)
		throw DatabaseError(QStringLiteral("This file has no valid format version."));
	if (version > FormatVersion)
		throw DatabaseError(QStringLiteral(
			"This file was exported by a newer version of MyBookTally (format %1). "
			"Please update MyBookTally to import it.").arg(version));
	if (!root.value(QStringLiteral("books")).isArray())
		throw DatabaseError(QStringLiteral("This file has no list of books."));

	const BookRepository books(m_database);
	const AuthorRepository authors(m_database);
	const ReviewRepository reviews(m_database);
	const ListRepository lists(m_database);
	const LookupRepository genres(m_database, LookupKind::Genre);
	const LookupRepository series(m_database, LookupKind::Series);

	ImportResult result;
	const QJsonArray bookArray = root.value(QStringLiteral("books")).toArray();
	const QSqlDatabase db = m_database.connection();

	// The whole import is one transaction, so it is fast (SQLite would
	// otherwise sync the file to disk after every statement) and a crash
	// part-way leaves the library as it was. Each book also gets its own
	// nested savepoint, so one bad book is rolled back on its own.
	Transaction all(db);

	for (int index = 0; index < bookArray.size(); ++index) {
		const QJsonObject object = bookArray.at(index).toObject();
		const QString title = text(object, "title");
		const QString label = title.isEmpty()
			? QStringLiteral("Book %1").arg(index + 1)
			: QStringLiteral("\"%1\"").arg(title);

		try {
			Transaction one(db);

			// Links first, since matching an existing book by title needs
			// to compare authors.
			Book book;
			QStringList authorNames;
			for (const QJsonValue value : object.value(QStringLiteral("authors")).toArray()) {
				const QString id = resolveAuthor(authors, value.toObject());
				if (id.isEmpty())
					continue;
				book.authorIds.append(id);
				authorNames.append(text(value.toObject(), "name"));
			}
			for (const QJsonValue value : object.value(QStringLiteral("series")).toArray()) {
				const QJsonObject s = value.toObject();
				SeriesEntry entry;
				entry.seriesId = resolveLookup(series, s);
				if (entry.seriesId.isEmpty())
					continue;
				if (s.value(QStringLiteral("position")).isDouble())
					entry.position = s.value(QStringLiteral("position")).toDouble();
				entry.positionLabel = text(s, "positionLabel");
				book.series.append(entry);
			}
			for (const QJsonValue value : object.value(QStringLiteral("genres")).toArray()) {
				const QString id = resolveLookup(genres, value.toObject());
				if (!id.isEmpty())
					book.genreIds.append(id);
			}
			for (const SingleLink& link : singleLinks) {
				const QJsonValue value = object.value(key(link.key));
				if (value.isObject())
					book.*(link.field) = resolveLookup(LookupRepository(m_database, link.kind),
						value.toObject());
			}

			book.title = title;
			book.releaseDate = text(object, "releaseDate");
			book.isbn = text(object, "isbn");
			const QJsonValue pages = object.value(QStringLiteral("pageCount"));
			if (pages.isDouble() && pages.toInt() > 0)
				book.pageCount = pages.toInt();
			book.location = text(object, "location");
			book.description = object.value(QStringLiteral("description")).toString();
			book.isPrivate = object.value(QStringLiteral("isPrivate")).toBool(false);
			const bool hasCover = object.contains(QStringLiteral("coverImage"));
			if (hasCover)
				book.coverImage = QByteArray::fromBase64(
					object.value(QStringLiteral("coverImage")).toString().toLatin1());

			// Does this book already exist?
			QString existingId;
			const QString fileId = text(object, "id");
			if (!fileId.isEmpty() && books.find(fileId)) {
				existingId = fileId;
			} else if (fileId.isEmpty()) {
				// Only books without an id are matched by content. A book
				// with an unknown id is a different book that happens to
				// look alike, and is added with its own id.
				const QList<QString> byIsbn = books.findIdsByIsbn(book.isbn);
				if (!byIsbn.isEmpty()) {
					existingId = byIsbn.first();
				} else {
					const QString wanted = normalizedNames(authorNames);
					for (const QString& candidate : books.findIdsByTitle(book.title)) {
						// Held in a named variable: a range-for over
						// books.find(...)->authorIds would iterate a member of
						// a temporary that is destroyed before the loop runs.
						const std::optional<Book> candidateBook = books.find(candidate);
						if (!candidateBook)
							continue;
						QStringList names;
						for (const QString& authorId : candidateBook->authorIds) {
							if (const auto author = authors.find(authorId))
								names.append(author->name);
						}
						if (normalizedNames(names) == wanted) {
							existingId = candidate;
							break;
						}
					}
				}
			}

			if (!existingId.isEmpty()) {
				book.id = existingId;
				if (!hasCover)
					book.coverImage = books.find(existingId)->coverImage;
				books.update(book);
			} else {
				book.id = fileId;
				books.insert(book);
			}

			// Merge reviews and ratings: add the ones the library does not
			// already have. Without ids, "already has" means the same text
			// (or score) from the same reviewer, so importing the same file
			// twice does not double them up.
			const QList<Review> existingReviews = reviews.reviews(book.id);
			for (const QJsonValue value : object.value(QStringLiteral("reviews")).toArray()) {
				const QJsonObject r = value.toObject();
				Review review;
				review.id = text(r, "id");
				review.bookId = book.id;
				review.text = text(r, "text");
				review.reviewer = text(r, "reviewer");
				review.createdAt = text(r, "createdAt");
				if (review.text.isEmpty())
					continue;
				if (!review.id.isEmpty() && reviews.findReview(review.id))
					continue;
				const bool duplicate = std::any_of(existingReviews.begin(), existingReviews.end(),
					[&](const Review& e) {
						return e.text == review.text && e.reviewer == review.reviewer;
					});
				if (!duplicate)
					reviews.addReview(review);
			}

			const QList<Rating> existingRatings = reviews.ratings(book.id);
			for (const QJsonValue value : object.value(QStringLiteral("ratings")).toArray()) {
				const QJsonObject r = value.toObject();
				// A rating without a score carries no information; skip it
				// rather than failing the whole book over it.
				if (!r.value(QStringLiteral("value")).isDouble())
					continue;
				Rating rating;
				rating.id = text(r, "id");
				rating.bookId = book.id;
				rating.value = r.value(QStringLiteral("value")).toDouble();
				rating.scale = r.value(QStringLiteral("scale")).toDouble(5);
				rating.reviewer = text(r, "reviewer");
				rating.createdAt = text(r, "createdAt");
				if (!rating.id.isEmpty() && reviews.findRating(rating.id))
					continue;
				const bool duplicate = std::any_of(existingRatings.begin(), existingRatings.end(),
					[&](const Rating& e) {
						return qFuzzyCompare(e.value + 1, rating.value + 1)
							&& qFuzzyCompare(e.scale, rating.scale)
							&& e.reviewer == rating.reviewer;
					});
				if (!duplicate)
					reviews.addRating(rating);
			}

			for (const QJsonValue value : object.value(QStringLiteral("lists")).toArray()) {
				const QString listId = resolveList(lists, value.toObject());
				if (!listId.isEmpty())
					lists.addBook(listId, book.id);
			}

			one.commit();
			// Counted only now: a failure in the reviews, ratings, or lists
			// above rolls the whole book back, and it must not be reported
			// as imported.
			if (existingId.isEmpty())
				++result.booksAdded;
			else
				++result.booksUpdated;
		} catch (const DatabaseError& error) {
			// Keep only the first line: the rest is the SQL text, which
			// means nothing to the user.
			result.warnings.append(QStringLiteral("%1 was not imported: %2")
				.arg(label, error.message().section(QLatin1Char('\n'), 0, 0)));
		}
	}

	all.commit();
	return result;
}

} // namespace mbt
