#include "OpenLibraryLookupDialog.h"

#include "ErrorLog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mbt {

namespace {

// Small covers from Open Library are about this size already, so they are
// shown at it without blurring.
const QSize thumbnailSize(40, 60);

// Tells the user where the full text of an error went, so they know where
// to look (or how to keep it next time).
QString whereFullErrorWent()
{
	const ErrorLog& log = ErrorLog::instance();
	QStringList places;
	if (log.fileEnabled())
		places.append(QObject::tr("added to the log file %1")
			.arg(QDir::toNativeSeparators(log.filePath())));
	if (log.consoleEnabled())
		places.append(QObject::tr("printed to the console"));
	if (places.isEmpty())
		return QObject::tr("To keep the full error, turn on logging in File > Settings.");
	return QObject::tr("The full error has been %1.").arg(places.join(QObject::tr(" and ")));
}

} // namespace

OpenLibraryLookupDialog::OpenLibraryLookupDialog(OpenLibraryClient& client,
	const OpenLibraryQuery& query, QWidget* parent)
	: QDialog(parent)
	, m_client(&client)
	, m_query(query)
	, m_thumbnailRequests(new QObject(this))
{
	setWindowTitle(tr("Look Up on Open Library"));

	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	// Titles from Open Library are shown as typed, never read as HTML.
	m_status->setTextFormat(Qt::PlainText);

	m_results = new QTableWidget(0, ColumnCount, this);
	m_results->setHorizontalHeaderLabels({tr("Cover"), tr("Title"), tr("Authors"), tr("ISBN"), tr("Published")});
	m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_results->setSelectionMode(QAbstractItemView::SingleSelection);
	m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_results->setIconSize(thumbnailSize);
	m_results->verticalHeader()->hide();
	m_results->verticalHeader()->setDefaultSectionSize(thumbnailSize.height() + 6);
	m_results->setWordWrap(true);
	QHeaderView* header = m_results->horizontalHeader();
	header->setSectionResizeMode(CoverColumn, QHeaderView::Fixed);
	m_results->setColumnWidth(CoverColumn, thumbnailSize.width() + 40);
	header->setSectionResizeMode(TitleColumn, QHeaderView::Stretch);
	m_results->setColumnWidth(AuthorsColumn, 180);
	m_results->setColumnWidth(IsbnColumn, 120);
	m_results->setColumnWidth(PublishedColumn, 110);

	auto* buttons = new QDialogButtonBox(this);
	m_choose = buttons->addButton(tr("&Use This Book"), QDialogButtonBox::AcceptRole);
	buttons->addButton(QDialogButtonBox::Cancel);
	// The choose button starts a fetch rather than closing the dialog, so it
	// is connected to that instead of to accept().
	connect(m_choose, &QPushButton::clicked, this, &OpenLibraryLookupDialog::chooseSelected);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_results, &QTableWidget::itemSelectionChanged, this, &OpenLibraryLookupDialog::updateChooseButton);
	connect(m_results, &QTableWidget::cellDoubleClicked, this, [this] { chooseSelected(); });

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_status);
	layout->addWidget(m_results, 1);
	layout->addWidget(buttons);

	m_reportError = [this](const QString& text, const QString& details) {
		QMessageBox box(QMessageBox::Warning, windowTitle(), text, QMessageBox::Ok, this);
		box.setInformativeText(whereFullErrorWent());
		if (!details.isEmpty())
			box.setDetailedText(details);
		box.exec();
	};

	resize(820, 520);
	updateChooseButton();
	startSearch();
}

OpenLibraryLookupDialog::~OpenLibraryLookupDialog()
{
	if (m_client) {
		m_client->cancel(m_thumbnailRequests);
		m_client->cancel(this);
	}
}

void OpenLibraryLookupDialog::reject()
{
	// Nothing more is wanted from Open Library once the user gives up.
	if (m_client) {
		m_client->cancel(m_thumbnailRequests);
		m_client->cancel(this);
	}
	QDialog::reject();
}

void OpenLibraryLookupDialog::startSearch()
{
	m_status->setText(tr("Searching Open Library for %1...").arg(m_query.describe()));
	const QUrl url = OpenLibrary::searchUrl(m_client->apiBase(), m_query);
	m_client->get(url, QStringLiteral("Searching Open Library for %1").arg(m_query.describe()), this,
		[this](const OpenLibraryReply& reply) { searchFinished(reply); });
}

void OpenLibraryLookupDialog::searchFinished(const OpenLibraryReply& reply)
{
	if (!reply.ok) {
		// Nothing to choose from, so after explaining, go back to the form.
		showError(tr("The search on Open Library failed.\n\n%1").arg(reply.error), reply.details);
		QDialog::reject();
		return;
	}

	QString parseError;
	if (!OpenLibrary::parseSearchResults(reply.body, m_query.isbn, &m_matches, &parseError)) {
		const QString details = QStringLiteral("Request: GET %1\n%2\nResponse body (first 2000 bytes):\n%3")
			.arg(reply.url.toString(), parseError, QString::fromUtf8(reply.body.left(2000)));
		ErrorLog::instance().record(QStringLiteral("Reading Open Library's search results for %1")
			.arg(m_query.describe()), details);
		showError(tr("Open Library's answer to the search could not be read. (%1)").arg(parseError), details);
		QDialog::reject();
		return;
	}

	if (m_matches.isEmpty()) {
		m_status->setText(tr("Open Library found no books matching %1. Try fewer details, or check "
			"the spelling, then search again.").arg(m_query.describe()));
		return;
	}

	m_results->setRowCount(int(m_matches.size()));
	for (int row = 0; row < m_matches.size(); ++row) {
		const OpenLibraryMatch& match = m_matches.at(row);
		auto* cover = new QTableWidgetItem(match.coverId > 0 ? tr("Loading...") : tr("No cover"));
		cover->setTextAlignment(Qt::AlignCenter);
		m_results->setItem(row, CoverColumn, cover);
		m_results->setItem(row, TitleColumn, new QTableWidgetItem(match.title));
		m_results->setItem(row, AuthorsColumn, new QTableWidgetItem(match.authors.join(QStringLiteral(", "))));
		m_results->setItem(row, IsbnColumn, new QTableWidgetItem(match.isbn));
		m_results->setItem(row, PublishedColumn, new QTableWidgetItem(match.publishDate));
	}
	m_results->selectRow(0);
	const QString found = m_matches.size() == 1 ? tr("Open Library found 1 book.")
		: tr("Open Library found %1 books.").arg(m_matches.size());
	m_status->setText(found + QLatin1Char(' ')
		+ tr("Pick yours and press Use This Book, or Cancel if it is not listed."));
	requestThumbnails();
}

void OpenLibraryLookupDialog::requestThumbnails()
{
	for (int row = 0; row < m_matches.size(); ++row) {
		const OpenLibraryMatch& match = m_matches.at(row);
		if (match.coverId <= 0)
			continue;
		const QUrl url = OpenLibrary::coverUrl(m_client->coversBase(), match.coverId,
			OpenLibrary::CoverSize::Small);
		// A missing thumbnail is not worth a popup per row while the user is
		// choosing - the row says so and the log has the details - but a
		// failure of anything they asked for directly does get one.
		m_client->get(url, QStringLiteral("Getting the cover of \"%1\" from Open Library for the "
			"search results").arg(match.title), m_thumbnailRequests,
			[this, row](const OpenLibraryReply& reply) {
				QTableWidgetItem* item = m_results->item(row, CoverColumn);
				if (!item)
					return;
				QPixmap pixmap;
				if (reply.ok && pixmap.loadFromData(reply.body)) {
					item->setText(QString());
					item->setData(Qt::DecorationRole,
						pixmap.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
				} else {
					item->setText(tr("Not available"));
					item->setToolTip(reply.ok ? tr("The cover image could not be read.") : reply.error);
				}
			});
	}
}

void OpenLibraryLookupDialog::chooseSelected()
{
	const int row = m_results->currentRow();
	if (m_fetchingDetails || row < 0 || row >= m_matches.size())
		return;

	m_fetchingDetails = true;
	m_chosenRow = row;
	m_results->setEnabled(false);
	updateChooseButton();

	// Covers still waiting for the list are no longer needed; dropping them
	// lets this book's requests go next instead of after them.
	m_client->cancel(m_thumbnailRequests);
	delete m_thumbnailRequests;
	m_thumbnailRequests = new QObject(this);

	const OpenLibraryMatch& match = m_matches.at(row);
	m_book = OpenLibrary::bookFromMatch(match);
	m_status->setText(tr("Getting the details of \"%1\" from Open Library...").arg(match.title));
	fetchEdition();
}

void OpenLibraryLookupDialog::fetchEdition()
{
	const OpenLibraryMatch& match = m_matches.at(m_chosenRow);
	const QUrl url = OpenLibrary::recordUrl(m_client->apiBase(), match.editionKey);
	if (url.isEmpty()) {
		fetchWork();
		return;
	}
	const QString activity = QStringLiteral("Getting the edition details of \"%1\" (%2) from Open Library")
		.arg(match.title, match.editionKey);
	m_client->get(url, activity, this, [this, activity](const OpenLibraryReply& reply) {
		QString parseError;
		if (!reply.ok)
			detailFailed(tr("the edition details"), reply.error, reply.details);
		else if (!OpenLibrary::applyEdition(reply.body, &m_book, &parseError)) {
			const QString details = QStringLiteral("Request: GET %1\n%2").arg(reply.url.toString(), parseError);
			ErrorLog::instance().record(activity, details);
			detailFailed(tr("the edition details"), parseError, details);
		}
		fetchWork();
	});
}

void OpenLibraryLookupDialog::fetchWork()
{
	const OpenLibraryMatch& match = m_matches.at(m_chosenRow);
	const QUrl url = OpenLibrary::recordUrl(m_client->apiBase(), match.workKey);
	if (url.isEmpty()) {
		fetchCover();
		return;
	}
	const QString activity = QStringLiteral("Getting the description and subjects of \"%1\" (%2) from Open Library")
		.arg(match.title, match.workKey);
	m_client->get(url, activity, this, [this, activity](const OpenLibraryReply& reply) {
		QString parseError;
		if (!reply.ok)
			detailFailed(tr("the description and subjects"), reply.error, reply.details);
		else if (!OpenLibrary::applyWork(reply.body, &m_book, &parseError)) {
			const QString details = QStringLiteral("Request: GET %1\n%2").arg(reply.url.toString(), parseError);
			ErrorLog::instance().record(activity, details);
			detailFailed(tr("the description and subjects"), parseError, details);
		}
		fetchCover();
	});
}

void OpenLibraryLookupDialog::fetchCover()
{
	if (m_book.coverId <= 0) {
		finishDetails();
		return;
	}
	const QUrl url = OpenLibrary::coverUrl(m_client->coversBase(), m_book.coverId, OpenLibrary::CoverSize::Large);
	const QString activity = QStringLiteral("Getting the cover image of \"%1\" from Open Library").arg(m_book.title);
	m_client->get(url, activity, this, [this](const OpenLibraryReply& reply) {
		if (reply.ok)
			m_book.coverImage = reply.body;
		else
			detailFailed(tr("the cover image"), reply.error, reply.details);
		finishDetails();
	});
}

void OpenLibraryLookupDialog::detailFailed(const QString& what, const QString& message, const QString& details)
{
	m_detailProblems.append(tr("Could not get %1: %2").arg(what, message));
	m_detailErrors.append(details);
}

void OpenLibraryLookupDialog::finishDetails()
{
	if (!m_detailProblems.isEmpty()) {
		showError(tr("Some details of this book could not be fetched from Open Library. The form "
			"will be filled in with everything that was found.\n\n%1")
				.arg(m_detailProblems.join(QLatin1Char('\n'))),
			m_detailErrors.join(QStringLiteral("\n\n")));
	}
	accept();
}

void OpenLibraryLookupDialog::showError(const QString& text, const QString& details)
{
	if (m_reportError)
		m_reportError(text, details);
}

void OpenLibraryLookupDialog::updateChooseButton()
{
	m_choose->setEnabled(!m_fetchingDetails && m_results->currentRow() >= 0
		&& !m_results->selectedItems().isEmpty());
}

} // namespace mbt
