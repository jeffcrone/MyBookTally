#include "BookDetailsDialog.h"

#include "BookEditorDialog.h"
#include "BookRepository.h"
#include "CoverImage.h"
#include "Database.h"
#include "ListRepository.h"
#include "LookupRepository.h"
#include "ReviewRepository.h"
#include "SmallDialogs.h"
#include "UiUtil.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace mbt {

namespace {

const QSize coverBox(200, 300);

// One "Label: value" row of the details table. Rows with no value are left
// out, so the page shows what is known rather than a column of blanks.
void addRow(QString& html, const QString& label, const QString& value)
{
	if (value.isEmpty())
		return;
	html += QStringLiteral("<tr><td style='padding-right:12px; vertical-align:top'><b>%1</b></td>"
		"<td>%2</td></tr>")
		.arg(label.toHtmlEscaped(), value.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
}

QString lookupName(const Database& database, LookupKind kind, const QString& id)
{
	if (id.isEmpty())
		return QString();
	const auto item = LookupRepository(database, kind).find(id);
	return item ? item->name : QString();
}

QString localTime(const QString& isoUtc)
{
	const QDateTime time = QDateTime::fromString(isoUtc, Qt::ISODateWithMs);
	if (!time.isValid())
		return isoUtc;
	return QLocale().toString(time.toLocalTime(), QLocale::ShortFormat);
}

QString byLine(const QString& reviewer)
{
	return reviewer.isEmpty() ? QObject::tr("You") : reviewer;
}

constexpr int IdRole = Qt::UserRole;

} // namespace

BookDetailsDialog::BookDetailsDialog(const Database& database, const QString& bookId, QWidget* parent)
	: QDialog(parent)
	, m_database(database)
	, m_bookId(bookId)
	, m_cover(new QLabel(this))
	, m_details(new QTextBrowser(this))
	, m_reviews(new QListWidget(this))
	, m_ratings(new QListWidget(this))
	, m_editReview(new QPushButton(tr("&Edit..."), this))
	, m_removeReview(new QPushButton(tr("&Delete"), this))
	, m_editRating(new QPushButton(tr("E&dit..."), this))
	, m_removeRating(new QPushButton(tr("De&lete"), this))
{
	m_cover->setFixedSize(coverBox);
	m_cover->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
	m_details->setOpenLinks(false);

	auto* detailsPage = new QWidget(this);
	auto* detailsLayout = new QHBoxLayout(detailsPage);
	detailsLayout->addWidget(m_details, 1);
	detailsLayout->addWidget(m_cover);

	// Reviews tab
	m_reviews->setWordWrap(true);
	m_reviews->setAlternatingRowColors(true);
	auto* addReviewButton = new QPushButton(tr("&Add Review..."), this);
	auto* reviewButtons = new QVBoxLayout;
	reviewButtons->addWidget(addReviewButton);
	reviewButtons->addWidget(m_editReview);
	reviewButtons->addWidget(m_removeReview);
	reviewButtons->addStretch();
	auto* reviewsPage = new QWidget(this);
	auto* reviewsLayout = new QHBoxLayout(reviewsPage);
	reviewsLayout->addWidget(m_reviews, 1);
	reviewsLayout->addLayout(reviewButtons);

	// Ratings tab
	auto* addRatingButton = new QPushButton(tr("Add &Rating..."), this);
	auto* ratingButtons = new QVBoxLayout;
	ratingButtons->addWidget(addRatingButton);
	ratingButtons->addWidget(m_editRating);
	ratingButtons->addWidget(m_removeRating);
	ratingButtons->addStretch();
	auto* ratingsPage = new QWidget(this);
	auto* ratingsLayout = new QHBoxLayout(ratingsPage);
	ratingsLayout->addWidget(m_ratings, 1);
	ratingsLayout->addLayout(ratingButtons);

	auto* tabs = new QTabWidget(this);
	tabs->addTab(detailsPage, tr("Details"));
	tabs->addTab(reviewsPage, tr("Reviews"));
	tabs->addTab(ratingsPage, tr("Ratings"));

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto* editBookButton = buttons->addButton(tr("Edit &Book..."), QDialogButtonBox::ActionRole);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(editBookButton, &QPushButton::clicked, this, &BookDetailsDialog::editBook);

	connect(addReviewButton, &QPushButton::clicked, this, &BookDetailsDialog::addReview);
	connect(m_editReview, &QPushButton::clicked, this, &BookDetailsDialog::editReview);
	connect(m_removeReview, &QPushButton::clicked, this, &BookDetailsDialog::removeReview);
	connect(m_reviews, &QListWidget::itemDoubleClicked, this, &BookDetailsDialog::editReview);
	connect(m_reviews, &QListWidget::currentRowChanged, this, &BookDetailsDialog::updateButtons);
	connect(addRatingButton, &QPushButton::clicked, this, &BookDetailsDialog::addRating);
	connect(m_editRating, &QPushButton::clicked, this, &BookDetailsDialog::editRating);
	connect(m_removeRating, &QPushButton::clicked, this, &BookDetailsDialog::removeRating);
	connect(m_ratings, &QListWidget::itemDoubleClicked, this, &BookDetailsDialog::editRating);
	connect(m_ratings, &QListWidget::currentRowChanged, this, &BookDetailsDialog::updateButtons);

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(tabs);
	layout->addWidget(buttons);

	reload();
	resize(720, 520);
}

QString BookDetailsDialog::detailsHtml() const
{
	return m_details->toHtml();
}

void BookDetailsDialog::reload()
{
	runReportingErrors(this, tr("Book Details"), [this] {
		const auto book = BookRepository(m_database).find(m_bookId);
		if (!book) {
			m_details->setHtml(tr("<p>This book no longer exists.</p>"));
			return;
		}
		setWindowTitle(book->title);

		const AuthorRepository authorRepo(m_database);
		QStringList authors;
		for (const QString& id : book->authorIds) {
			if (const auto author = authorRepo.find(id))
				authors.append(author->name);
		}

		QStringList series;
		for (const SeriesEntry& entry : book->series) {
			QString text = entry.seriesName;
			if (!entry.positionLabel.isEmpty())
				text += QStringLiteral(" #") + entry.positionLabel;
			else if (entry.position)
				text += QStringLiteral(" #") + QLocale().toString(*entry.position, 'g', 10);
			series.append(text);
		}

		const LookupRepository genreRepo(m_database, LookupKind::Genre);
		QStringList genres;
		for (const QString& id : book->genreIds) {
			if (const auto genre = genreRepo.find(id))
				genres.append(genre->name);
		}

		QStringList lists;
		for (const BookList& list : ListRepository(m_database).listsForBook(m_bookId))
			lists.append(list.name);

		const ReviewRepository reviewRepo(m_database);
		const QList<Rating> ratings = reviewRepo.ratings(m_bookId);
		QString ratingSummary;
		if (!ratings.isEmpty()) {
			double total = 0;
			for (const Rating& rating : ratings)
				total += rating.value * 5.0 / rating.scale;
			ratingSummary = tr("%1/5 average from %n rating(s)", nullptr, int(ratings.size()))
				.arg(QString::number(total / ratings.size(), 'f', 1));
		}

		QString html = QStringLiteral("<h2>%1</h2><table>").arg(book->title.toHtmlEscaped());
		addRow(html, tr("Authors"), authors.join(QStringLiteral(", ")));
		addRow(html, tr("Series"), series.join(QStringLiteral(", ")));
		addRow(html, tr("Genres"), genres.join(QStringLiteral(", ")));
		addRow(html, tr("Released"), book->releaseDate);
		addRow(html, tr("ISBN"), book->isbn);
		addRow(html, tr("Pages"), book->pageCount ? QString::number(*book->pageCount) : QString());
		addRow(html, tr("Type"), lookupName(m_database, LookupKind::BookType, book->bookTypeId));
		addRow(html, tr("Ownership"),
			lookupName(m_database, LookupKind::OwnershipStatus, book->ownershipStatusId));
		addRow(html, tr("Reading status"),
			lookupName(m_database, LookupKind::ReadingStatus, book->readingStatusId));
		addRow(html, tr("Location"), book->location);
		addRow(html, tr("Rating"), ratingSummary);
		addRow(html, tr("Lists"), lists.join(QStringLiteral(", ")));
		addRow(html, tr("Visibility"), book->isPrivate ? tr("Private (hidden from the library list)")
			: tr("Public"));
		addRow(html, tr("Added"), localTime(book->createdAt));
		addRow(html, tr("Last changed"), localTime(book->updatedAt));
		html += QStringLiteral("</table>");
		if (!book->description.isEmpty())
			html += QStringLiteral("<h3>%1</h3><p>%2</p>").arg(tr("Description").toHtmlEscaped(),
				book->description.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
		m_details->setHtml(html);

		const QPixmap cover = CoverImage::toPixmap(book->coverImage, coverBox);
		if (cover.isNull())
			m_cover->setText(tr("No cover"));
		else
			m_cover->setPixmap(cover);

		m_reviews->clear();
		for (const Review& review : reviewRepo.reviews(m_bookId)) {
			auto* item = new QListWidgetItem(QStringLiteral("%1\n— %2, %3")
				.arg(review.text, byLine(review.reviewer), localTime(review.createdAt)));
			item->setData(IdRole, review.id);
			m_reviews->addItem(item);
		}

		m_ratings->clear();
		for (const Rating& rating : ratings) {
			auto* item = new QListWidgetItem(QStringLiteral("%1 — %2, %3")
				.arg(formatRating(rating.value, rating.scale), byLine(rating.reviewer),
					localTime(rating.createdAt)));
			item->setData(IdRole, rating.id);
			m_ratings->addItem(item);
		}
	});
	updateButtons();
}

void BookDetailsDialog::updateButtons()
{
	const bool review = m_reviews->currentItem() != nullptr;
	m_editReview->setEnabled(review);
	m_removeReview->setEnabled(review);
	const bool rating = m_ratings->currentItem() != nullptr;
	m_editRating->setEnabled(rating);
	m_removeRating->setEnabled(rating);
}

void BookDetailsDialog::editBook()
{
	std::optional<Book> book;
	if (!runReportingErrors(this, tr("Edit Book"), [&] { book = BookRepository(m_database).find(m_bookId); })
		|| !book)
		return;
	BookEditorDialog editor(m_database, *book, this);
	if (editor.exec() == QDialog::Accepted) {
		m_changed = true;
		reload();
	}
}

void BookDetailsDialog::addReview()
{
	Review review;
	review.bookId = m_bookId;
	ReviewDialog dialog(review, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	if (runReportingErrors(this, dialog.windowTitle(),
			[&] { ReviewRepository(m_database).addReview(dialog.review()); })) {
		m_changed = true;
		reload();
	}
}

void BookDetailsDialog::editReview()
{
	const QListWidgetItem* item = m_reviews->currentItem();
	if (!item)
		return;
	std::optional<Review> review;
	runReportingErrors(this, tr("Edit Review"),
		[&] { review = ReviewRepository(m_database).findReview(item->data(IdRole).toString()); });
	if (!review)
		return;
	ReviewDialog dialog(*review, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	if (runReportingErrors(this, dialog.windowTitle(),
			[&] { ReviewRepository(m_database).updateReview(dialog.review()); })) {
		m_changed = true;
		reload();
	}
}

void BookDetailsDialog::removeReview()
{
	const QListWidgetItem* item = m_reviews->currentItem();
	if (!item)
		return;
	if (QMessageBox::question(this, tr("Delete Review"), tr("Delete this review?")) != QMessageBox::Yes)
		return;
	const QString id = item->data(IdRole).toString();
	if (runReportingErrors(this, tr("Delete Review"), [&] { ReviewRepository(m_database).removeReview(id); })) {
		m_changed = true;
		reload();
	}
}

void BookDetailsDialog::addRating()
{
	Rating rating;
	rating.bookId = m_bookId;
	RatingDialog dialog(rating, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	if (runReportingErrors(this, dialog.windowTitle(),
			[&] { ReviewRepository(m_database).addRating(dialog.rating()); })) {
		m_changed = true;
		reload();
	}
}

void BookDetailsDialog::editRating()
{
	const QListWidgetItem* item = m_ratings->currentItem();
	if (!item)
		return;
	std::optional<Rating> rating;
	runReportingErrors(this, tr("Edit Rating"),
		[&] { rating = ReviewRepository(m_database).findRating(item->data(IdRole).toString()); });
	if (!rating)
		return;
	RatingDialog dialog(*rating, this);
	if (dialog.exec() != QDialog::Accepted)
		return;
	if (runReportingErrors(this, dialog.windowTitle(),
			[&] { ReviewRepository(m_database).updateRating(dialog.rating()); })) {
		m_changed = true;
		reload();
	}
}

void BookDetailsDialog::removeRating()
{
	const QListWidgetItem* item = m_ratings->currentItem();
	if (!item)
		return;
	if (QMessageBox::question(this, tr("Delete Rating"), tr("Delete this rating?")) != QMessageBox::Yes)
		return;
	const QString id = item->data(IdRole).toString();
	if (runReportingErrors(this, tr("Delete Rating"), [&] { ReviewRepository(m_database).removeRating(id); })) {
		m_changed = true;
		reload();
	}
}

} // namespace mbt
