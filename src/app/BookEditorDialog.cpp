#include "BookEditorDialog.h"

#include "BookRepository.h"
#include "CoverImage.h"
#include "Database.h"
#include "DatabaseError.h"
#include "ErrorLog.h"
#include "Isbn.h"
#include "LinkListEditor.h"
#include "LookupRepository.h"
#include "OpenLibrary.h"
#include "OpenLibraryLookupDialog.h"
#include "SqlUtil.h"
#include "UiUtil.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mbt {

namespace {

const QSize coverBox(180, 270);

// Columns of the series table.
enum SeriesColumn { SeriesName, SeriesPosition, SeriesLabel };

// Release dates may be a year, a year and month, or a full date, because
// often only the year is known.
const QRegularExpression releaseDatePattern(QStringLiteral(R"(^\d{4}(-\d{2}(-\d{2})?)?$)"));

// Parses a series position typed with either a "." or the user's own
// decimal separator ("1,5" in much of Europe).
std::optional<double> parsePosition(const QString& text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty())
		return std::nullopt;
	bool ok = false;
	double value = QLocale().toDouble(trimmed, &ok);
	if (!ok)
		value = QLocale::c().toDouble(trimmed, &ok);
	if (!ok)
		return std::nullopt;
	return value;
}

QString formatPosition(const std::optional<double>& position)
{
	return position ? QLocale::c().toString(*position, 'g', 10) : QString();
}

} // namespace

BookEditorDialog::BookEditorDialog(const Database& database, const Book& book, QWidget* parent,
	OpenLibraryClient* openLibrary)
	: QDialog(parent)
	, m_database(database)
	, m_openLibrary(openLibrary)
	, m_book(book)
	, m_cover(book.coverImage)
{
	setWindowTitle(book.id.isEmpty() ? tr("Add Book") : tr("Edit Book"));

	auto* tabs = new QTabWidget(this);
	tabs->addTab(buildDetailsPage(), tr("&Details"));
	tabs->addTab(buildSeriesGenresPage(), tr("&Series && Genres"));
	tabs->addTab(buildDescriptionCoverPage(), tr("Description && &Cover"));

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	// Only when adding: the lookup replaces most of the form, which is what
	// is wanted for a new book but would quietly overwrite a book the user
	// has already cataloged and maybe corrected by hand.
	//
	// It sits on its own at the left of the button row rather than inside
	// the button box, where some styles would place it between OK and
	// Cancel and make it easy to press by mistake.
	auto* buttonRow = new QHBoxLayout;
	if (book.id.isEmpty() && m_openLibrary) {
		m_lookup = new QPushButton(tr("Look &Up on Open Library..."), this);
		m_lookup->setToolTip(tr("Type what you know (the title, an author, or the ISBN), then press "
			"this to choose the book from Open Library and fill in the rest."));
		// Enter in a field must still mean OK, not start a lookup.
		m_lookup->setAutoDefault(false);
		connect(m_lookup, &QPushButton::clicked, this, &BookEditorDialog::lookUpOnOpenLibrary);
		buttonRow->addWidget(m_lookup);
	}
	buttonRow->addStretch();
	buttonRow->addWidget(buttons);

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(tabs);
	layout->addLayout(buttonRow);

	loadBook();
	resize(620, 560);
	m_title->setFocus();
}

QWidget* BookEditorDialog::buildDetailsPage()
{
	auto* page = new QWidget(this);

	m_title = new QLineEdit(page);
	m_authors = new LinkListEditor(true, tr("Pick or type an author, then press Add"), page);

	m_releaseDate = new QLineEdit(page);
	m_releaseDate->setPlaceholderText(tr("YYYY, YYYY-MM, or YYYY-MM-DD"));
	m_releaseDate->setValidator(new QRegularExpressionValidator(
		QRegularExpression(QStringLiteral(R"(\d{0,4}(-\d{0,2}(-\d{0,2})?)?)")), m_releaseDate));

	m_isbn = new QLineEdit(page);
	m_isbn->setPlaceholderText(tr("10 or 13 digits; hyphens are fine"));

	// 0 shows as "(none)", meaning the page count is not recorded.
	m_pages = new QSpinBox(page);
	m_pages->setRange(0, 100000);
	m_pages->setSpecialValueText(tr("(none)"));

	m_bookType = new QComboBox(page);
	m_ownership = new QComboBox(page);
	m_reading = new QComboBox(page);

	m_location = new QLineEdit(page);
	m_location->setPlaceholderText(tr("Where it is, or where to get it"));

	m_private = new QCheckBox(tr("&Private (hide from the main library list)"), page);

	auto* form = new QFormLayout(page);
	form->addRow(tr("&Title:"), m_title);
	form->addRow(tr("&Authors:"), m_authors);
	form->addRow(tr("&Released:"), m_releaseDate);
	form->addRow(tr("&ISBN:"), m_isbn);
	form->addRow(tr("&Pages:"), m_pages);
	form->addRow(tr("T&ype:"), m_bookType);
	form->addRow(tr("&Ownership:"), m_ownership);
	form->addRow(tr("Readin&g status:"), m_reading);
	form->addRow(tr("&Location:"), m_location);
	form->addRow(QString(), m_private);
	return page;
}

QWidget* BookEditorDialog::buildSeriesGenresPage()
{
	auto* page = new QWidget(this);

	m_seriesPicker = new QComboBox(page);
	m_seriesPicker->setEditable(true);
	m_seriesPicker->setInsertPolicy(QComboBox::NoInsert);
	m_seriesPicker->lineEdit()->setPlaceholderText(tr("Series name"));
	m_seriesPicker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	m_seriesPosition = new QLineEdit(page);
	m_seriesPosition->setPlaceholderText(tr("Number, e.g. 1.5"));
	m_seriesPosition->setMaximumWidth(120);
	m_seriesLabel = new QLineEdit(page);
	m_seriesLabel->setPlaceholderText(tr("Label (optional)"));
	m_seriesLabel->setMaximumWidth(140);
	auto* addSeries = new QPushButton(tr("Add"), page);
	auto* removeSeries = new QPushButton(tr("Remove"), page);

	m_series = new QTableWidget(0, 3, page);
	m_series->setHorizontalHeaderLabels({tr("Series"), tr("Number"), tr("Label")});
	m_series->horizontalHeader()->setSectionResizeMode(SeriesName, QHeaderView::Stretch);
	m_series->verticalHeader()->hide();
	m_series->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_series->setSelectionMode(QAbstractItemView::SingleSelection);
	m_series->setMaximumHeight(140);

	connect(addSeries, &QPushButton::clicked, this, [this] {
		addSeriesRow(m_seriesPicker->currentText(), m_seriesPosition->text(), m_seriesLabel->text());
	});
	connect(removeSeries, &QPushButton::clicked, this, [this] {
		if (m_series->currentRow() >= 0)
			m_series->removeRow(m_series->currentRow());
	});

	auto* seriesRow = new QHBoxLayout;
	seriesRow->addWidget(m_seriesPicker, 1);
	seriesRow->addWidget(m_seriesPosition);
	seriesRow->addWidget(m_seriesLabel);
	seriesRow->addWidget(addSeries);

	auto* seriesTableRow = new QHBoxLayout;
	seriesTableRow->addWidget(m_series, 1);
	auto* seriesButtons = new QVBoxLayout;
	seriesButtons->addWidget(removeSeries);
	seriesButtons->addStretch();
	seriesTableRow->addLayout(seriesButtons);

	auto* seriesHelp = new QLabel(tr(
		"The number orders books within the series and can be a decimal, so a book "
		"set between 1 and 2 can be 1.5. The label, if given, is shown instead of "
		"the number (for example \"Prequel\")."), page);
	seriesHelp->setWordWrap(true);

	m_genres = new LinkListEditor(false, tr("Pick or type a genre, then press Add"), page);

	auto* layout = new QVBoxLayout(page);
	layout->addWidget(new QLabel(tr("<b>Series</b>"), page));
	layout->addLayout(seriesRow);
	layout->addLayout(seriesTableRow);
	layout->addWidget(seriesHelp);
	layout->addSpacing(12);
	layout->addWidget(new QLabel(tr("<b>Genres</b>"), page));
	layout->addWidget(m_genres);
	layout->addStretch();
	return page;
}

QWidget* BookEditorDialog::buildDescriptionCoverPage()
{
	auto* page = new QWidget(this);
	m_description = new QPlainTextEdit(page);

	m_coverPreview = new QLabel(page);
	m_coverPreview->setFixedSize(coverBox);
	m_coverPreview->setAlignment(Qt::AlignCenter);
	m_coverPreview->setFrameShape(QFrame::StyledPanel);

	auto* choose = new QPushButton(tr("C&hoose Image..."), page);
	auto* clear = new QPushButton(tr("Remo&ve Image"), page);
	connect(choose, &QPushButton::clicked, this, &BookEditorDialog::chooseCover);
	connect(clear, &QPushButton::clicked, this, [this] {
		m_cover.clear();
		showCover();
	});

	auto* coverColumn = new QVBoxLayout;
	coverColumn->addWidget(new QLabel(tr("Cover:"), page));
	coverColumn->addWidget(m_coverPreview);
	coverColumn->addWidget(choose);
	coverColumn->addWidget(clear);
	coverColumn->addStretch();

	auto* textColumn = new QVBoxLayout;
	textColumn->addWidget(new QLabel(tr("Description:"), page));
	textColumn->addWidget(m_description);

	auto* layout = new QHBoxLayout(page);
	layout->addLayout(textColumn, 1);
	layout->addLayout(coverColumn);
	return page;
}

void BookEditorDialog::loadBook()
{
	const AuthorRepository authors(m_database);
	const LookupRepository genres(m_database, LookupKind::Genre);
	const LookupRepository series(m_database, LookupKind::Series);

	m_title->setText(m_book.title);

	QList<LinkChoice> authorChoices;
	for (const Author& author : authors.all())
		authorChoices.append({author.id, author.name});
	m_authors->setChoices(authorChoices);
	QList<LinkChoice> bookAuthors;
	for (const QString& id : m_book.authorIds) {
		if (const auto author = authors.find(id))
			bookAuthors.append({author->id, author->name});
	}
	m_authors->setSelected(bookAuthors);

	m_releaseDate->setText(m_book.releaseDate);
	m_isbn->setText(m_book.isbn);
	m_pages->setValue(m_book.pageCount.value_or(0));
	fillLookupCombo(m_bookType, m_database, LookupKind::BookType, m_book.bookTypeId);
	fillLookupCombo(m_ownership, m_database, LookupKind::OwnershipStatus, m_book.ownershipStatusId);
	fillLookupCombo(m_reading, m_database, LookupKind::ReadingStatus, m_book.readingStatusId);
	m_location->setText(m_book.location);
	m_private->setChecked(m_book.isPrivate);

	m_seriesPicker->clear();
	for (const LookupItem& item : series.all())
		m_seriesPicker->addItem(item.name, item.id);
	m_seriesPicker->setCurrentIndex(-1);
	m_series->setRowCount(0);
	for (const SeriesEntry& entry : m_book.series)
		addSeriesRow(entry.seriesName, formatPosition(entry.position), entry.positionLabel);

	QList<LinkChoice> genreChoices;
	for (const LookupItem& item : genres.all())
		genreChoices.append({item.id, item.name});
	m_genres->setChoices(genreChoices);
	QList<LinkChoice> bookGenres;
	for (const QString& id : m_book.genreIds) {
		if (const auto genre = genres.find(id))
			bookGenres.append({genre->id, genre->name});
	}
	m_genres->setSelected(bookGenres);

	m_description->setPlainText(m_book.description);
	showCover();
}

void BookEditorDialog::addSeriesRow(const QString& seriesName, const QString& position,
	const QString& label)
{
	const QString name = seriesName.trimmed();
	if (name.isEmpty())
		return;

	// A name matching an existing series means that series.
	QString id;
	QString displayName = name;
	for (int i = 0; i < m_seriesPicker->count(); ++i) {
		if (m_seriesPicker->itemText(i).compare(name, Qt::CaseInsensitive) == 0) {
			id = m_seriesPicker->itemData(i).toString();
			displayName = m_seriesPicker->itemText(i);
			break;
		}
	}
	// A book is in each series at most once; adding it again replaces the
	// earlier row, which is how the user changes a number.
	for (int row = 0; row < m_series->rowCount(); ++row) {
		if (m_series->item(row, SeriesName)->text().compare(displayName, Qt::CaseInsensitive) == 0) {
			m_series->removeRow(row);
			break;
		}
	}

	const int row = m_series->rowCount();
	m_series->insertRow(row);
	auto* nameItem = new QTableWidgetItem(displayName);
	nameItem->setData(Qt::UserRole, id);
	// The name identifies the series and is not edited in place; the number
	// and label are.
	nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
	m_series->setItem(row, SeriesName, nameItem);
	m_series->setItem(row, SeriesPosition, new QTableWidgetItem(position.trimmed()));
	m_series->setItem(row, SeriesLabel, new QTableWidgetItem(label.trimmed()));

	m_seriesPicker->setCurrentIndex(-1);
	m_seriesPicker->clearEditText();
	m_seriesPosition->clear();
	m_seriesLabel->clear();
}

void BookEditorDialog::showCover()
{
	const QPixmap pixmap = CoverImage::toPixmap(m_cover, coverBox);
	if (pixmap.isNull()) {
		m_coverPreview->setPixmap(QPixmap());
		m_coverPreview->setText(tr("No cover"));
	} else {
		m_coverPreview->setPixmap(pixmap);
	}
}

void BookEditorDialog::chooseCover()
{
	QStringList patterns;
	for (const QByteArray& format : QImageReader::supportedImageFormats())
		patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
	const QString path = QFileDialog::getOpenFileName(this, tr("Choose Cover Image"), QString(),
		tr("Images (%1)").arg(patterns.join(QLatin1Char(' '))));
	if (path.isEmpty())
		return;

	QString error;
	const QByteArray bytes = CoverImage::loadForStorage(path, &error);
	if (bytes.isEmpty()) {
		QMessageBox::warning(this, tr("Choose Cover Image"),
			tr("That file could not be read as an image: %1").arg(error));
		return;
	}
	m_cover = bytes;
	showCover();
}

OpenLibraryQuery BookEditorDialog::lookupQuery() const
{
	OpenLibraryQuery query;
	query.title = m_title->text().trimmed();
	// The first author is the one most likely to be credited the same way
	// on Open Library; the list is used if anything was added to it,
	// otherwise whatever is typed in the picker.
	const QList<LinkChoice> authors = m_authors->selected();
	query.author = authors.isEmpty() ? m_authors->picker()->currentText().trimmed()
									 : authors.first().name;
	query.isbn = m_isbn->text().trimmed();
	return query;
}

void BookEditorDialog::lookUpOnOpenLibrary()
{
	if (!m_openLibrary)
		return;
	const QString title = tr("Look Up on Open Library");
	const OpenLibraryQuery query = lookupQuery();
	if (query.isEmpty()) {
		QMessageBox::information(this, title,
			tr("Type what you know about the book first (its title, an author, or its ISBN), "
				"then press Look Up on Open Library again."));
		return;
	}
	// A mistyped ISBN on its own would search for nothing at all; with a
	// title or author as well, the search simply uses those instead.
	if (!query.isbn.isEmpty() && !Isbn::isWellFormed(query.isbn)
		&& query.title.isEmpty() && query.author.isEmpty()) {
		QMessageBox::warning(this, title,
			tr("An ISBN has 10 or 13 digits (an ISBN-10 may end in X). Please check it, "
				"or type the title or an author instead."));
		return;
	}

	OpenLibraryLookupDialog dialog(*m_openLibrary, query, this);
	if (dialog.exec() == QDialog::Accepted)
		applyLookup(dialog.chosenBook());
}

void BookEditorDialog::applyLookup(const OpenLibraryBook& found)
{
	if (!found.title.isEmpty())
		m_title->setText(found.title);

	// Open Library's author list replaces the typed one: it is in credit
	// order and spelled consistently. Names that match an author already in
	// the library link to that author rather than creating a duplicate.
	if (!found.authors.isEmpty()) {
		m_authors->setSelected({});
		for (const QString& name : found.authors)
			m_authors->addChoice(m_authors->choiceFor(name));
	}

	if (!found.releaseDate.isEmpty())
		m_releaseDate->setText(found.releaseDate);
	if (!found.isbn.isEmpty())
		m_isbn->setText(found.isbn);
	if (found.pageCount && *found.pageCount > 0 && *found.pageCount <= m_pages->maximum())
		m_pages->setValue(*found.pageCount);

	// The format only fills in the type when it names one the user has;
	// a new book type is not invented from Open Library's free text.
	if (!found.physicalFormat.isEmpty()) {
		const QString typeName = OpenLibrary::bookTypeNameFor(found.physicalFormat);
		for (int i = 0; i < m_bookType->count(); ++i) {
			if (!m_bookType->itemData(i).toString().isEmpty()
				&& m_bookType->itemText(i).compare(typeName, Qt::CaseInsensitive) == 0) {
				m_bookType->setCurrentIndex(i);
				break;
			}
		}
	}

	if (!found.description.isEmpty())
		m_description->setPlainText(found.description);

	// Subjects become genres only when the user already has a genre of
	// that name. Open Library lists dozens per book, most of them people
	// and places in the story, which would swamp the genre list.
	for (const QString& subject : found.subjects) {
		for (const LinkChoice& genre : m_genres->choices()) {
			if (genre.name.compare(subject, Qt::CaseInsensitive) == 0) {
				m_genres->addChoice(genre);
				break;
			}
		}
	}

	if (!found.coverImage.isEmpty()) {
		QString error;
		const QByteArray cover = CoverImage::prepareForStorage(found.coverImage, &error);
		if (cover.isEmpty()) {
			const QString message = tr("The cover image from Open Library could not be read: %1").arg(error);
			ErrorLog::instance().record(QStringLiteral("Reading the cover image of \"%1\" downloaded from "
				"Open Library").arg(found.title),
				QStringLiteral("%1\nCover id: %2\nDownloaded bytes: %3")
					.arg(message).arg(found.coverId).arg(found.coverImage.size()));
			QMessageBox::warning(this, tr("Look Up on Open Library"),
				message + QStringLiteral("\n\n") + tr("The rest of the details have been filled in."));
		} else {
			m_cover = cover;
			showCover();
		}
	}
}

QString BookEditorDialog::validate()
{
	if (m_title->text().trimmed().isEmpty())
		return tr("Please enter the book's title.");

	const QString date = m_releaseDate->text().trimmed();
	if (!date.isEmpty()) {
		if (!releaseDatePattern.match(date).hasMatch())
			return tr("The release date must be a year (2024), a year and month (2024-05), "
				"or a full date (2024-05-17).");
		if (date.size() == 10 && !QDate::fromString(date, Qt::ISODate).isValid())
			return tr("%1 is not a real date.").arg(date);
		if (date.size() == 7) {
			const int month = date.mid(5, 2).toInt();
			if (month < 1 || month > 12)
				return tr("%1 is not a real month.").arg(date);
		}
	}

	const QString isbn = m_isbn->text().trimmed();
	if (!isbn.isEmpty() && !Isbn::isWellFormed(isbn))
		return tr("An ISBN has 10 or 13 digits (an ISBN-10 may end in X). "
			"Hyphens and spaces are ignored.");

	for (int row = 0; row < m_series->rowCount(); ++row) {
		const QString position = m_series->item(row, SeriesPosition)->text();
		if (!position.trimmed().isEmpty() && !parsePosition(position))
			return tr("The number for the series \"%1\" is not a number.")
				.arg(m_series->item(row, SeriesName)->text());
	}
	return QString();
}

void BookEditorDialog::accept()
{
	// A name typed into a picker but not yet added is almost certainly
	// meant to be added; losing it on OK would be a nasty surprise.
	m_authors->addCurrentText();
	m_genres->addCurrentText();
	if (!m_seriesPicker->currentText().trimmed().isEmpty())
		addSeriesRow(m_seriesPicker->currentText(), m_seriesPosition->text(), m_seriesLabel->text());

	const QString problem = validate();
	if (!problem.isEmpty()) {
		QMessageBox::warning(this, windowTitle(), problem);
		return;
	}

	const QString isbn = m_isbn->text().trimmed();
	if (!isbn.isEmpty() && !Isbn::hasValidChecksum(isbn) && !m_confirmChecksum) {
		const auto answer = QMessageBox::question(this, windowTitle(),
			tr("The ISBN %1 has an incorrect check digit, so it may have been mistyped. "
				"Some older books were printed with a wrong check digit, though.\n\n"
				"Save it anyway?").arg(isbn));
		if (answer != QMessageBox::Yes)
			return;
	}

	Book book = m_book;
	book.title = m_title->text().trimmed();
	book.releaseDate = m_releaseDate->text().trimmed();
	book.isbn = Isbn::normalize(isbn);
	book.pageCount = m_pages->value() > 0 ? std::optional<int>(m_pages->value()) : std::nullopt;
	book.bookTypeId = comboId(m_bookType);
	book.ownershipStatusId = comboId(m_ownership);
	book.readingStatusId = comboId(m_reading);
	book.location = m_location->text().trimmed();
	book.isPrivate = m_private->isChecked();
	book.description = m_description->toPlainText().trimmed();
	book.coverImage = m_cover;

	const bool saved = runReportingErrors(this, windowTitle(), [&] {
		// New authors, genres, and series are created in the same
		// transaction as the book, so canceling on an error does not leave
		// them behind.
		Transaction transaction(m_database.connection());
		const AuthorRepository authors(m_database);
		const LookupRepository genres(m_database, LookupKind::Genre);
		const LookupRepository series(m_database, LookupKind::Series);

		book.authorIds.clear();
		for (const LinkChoice& choice : m_authors->selected())
			book.authorIds.append(choice.id.isEmpty() ? authors.findOrCreate(choice.name) : choice.id);

		book.genreIds.clear();
		for (const LinkChoice& choice : m_genres->selected())
			book.genreIds.append(choice.id.isEmpty() ? genres.findOrCreate(choice.name) : choice.id);

		book.series.clear();
		for (int row = 0; row < m_series->rowCount(); ++row) {
			SeriesEntry entry;
			const QTableWidgetItem* nameItem = m_series->item(row, SeriesName);
			entry.seriesId = nameItem->data(Qt::UserRole).toString();
			if (entry.seriesId.isEmpty())
				entry.seriesId = series.findOrCreate(nameItem->text());
			entry.seriesName = nameItem->text();
			entry.position = parsePosition(m_series->item(row, SeriesPosition)->text());
			entry.positionLabel = m_series->item(row, SeriesLabel)->text().trimmed();
			book.series.append(entry);
		}

		const BookRepository books(m_database);
		if (book.id.isEmpty())
			books.insert(book);
		else
			books.update(book);
		transaction.commit();
	});
	if (!saved)
		return;

	m_book = book;
	QDialog::accept();
}

} // namespace mbt
