#include "ManageDialog.h"

#include "Database.h"
#include "LookupRepository.h"
#include "UiUtil.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace mbt {

namespace {

constexpr int IdRole = Qt::UserRole;

} // namespace

ManageDialog::ManageDialog(const Database& database, QWidget* parent)
	: QDialog(parent)
	, m_database(database)
{
	build();
}

ManageDialog::ManageDialog(const Database& database, LookupKind kind, QWidget* parent)
	: QDialog(parent)
	, m_database(database)
	, m_kind(kind)
{
	build();
}

QString ManageDialog::singular() const
{
	return isAuthors() ? tr("Author") : lookupKindSingular(*m_kind);
}

void ManageDialog::build()
{
	setWindowTitle(tr("Manage %1").arg(isAuthors() ? tr("Authors") : lookupKindPlural(*m_kind)));

	m_list = new QListWidget(this);
	auto* newButton = new QPushButton(tr("&New"), this);
	m_delete = new QPushButton(tr("&Delete"), this);
	auto* listButtons = new QHBoxLayout;
	listButtons->addWidget(newButton);
	listButtons->addWidget(m_delete);
	auto* left = new QVBoxLayout;
	left->addWidget(m_list);
	left->addLayout(listButtons);

	m_name = new QLineEdit(this);
	m_usage = new QLabel(this);
	m_usage->setWordWrap(true);
	m_save = new QPushButton(tr("&Save"), this);

	auto* form = new QFormLayout;
	form->addRow(tr("&Name:"), m_name);
	const bool hasDescription = isAuthors() || lookupKindHasDescription(*m_kind);
	if (isAuthors()) {
		m_birthDate = new QLineEdit(this);
		m_birthDate->setPlaceholderText(tr("YYYY-MM-DD or YYYY"));
		form->addRow(tr("&Born:"), m_birthDate);
	}
	if (hasDescription) {
		m_description = new QPlainTextEdit(this);
		form->addRow(isAuthors() ? tr("Bi&o:") : tr("Descri&ption:"), m_description);
	}

	auto* saveRow = new QHBoxLayout;
	saveRow->addStretch();
	saveRow->addWidget(m_save);

	auto* right = new QVBoxLayout;
	right->addLayout(form);
	right->addWidget(m_usage);
	right->addStretch();
	right->addLayout(saveRow);

	auto* columns = new QHBoxLayout;
	columns->addLayout(left, 2);
	columns->addLayout(right, 3);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(columns);
	layout->addWidget(buttons);

	connect(m_list, &QListWidget::currentRowChanged, this, &ManageDialog::showSelected);
	connect(newButton, &QPushButton::clicked, this, &ManageDialog::startNew);
	connect(m_save, &QPushButton::clicked, this, &ManageDialog::saveCurrent);
	connect(m_delete, &QPushButton::clicked, this, [this] { deleteCurrent(true); });

	reload();
	resize(640, 420);
}

void ManageDialog::reload(const QString& selectId)
{
	// Rebuilding the list fires currentRowChanged for every row; block it so
	// the form is filled once, for the right row, at the end.
	m_list->blockSignals(true);
	m_list->clear();
	runReportingErrors(this, windowTitle(), [&] {
		if (isAuthors()) {
			for (const Author& author : AuthorRepository(m_database).all()) {
				auto* item = new QListWidgetItem(author.name, m_list);
				item->setData(IdRole, author.id);
			}
		} else {
			for (const LookupItem& lookup : LookupRepository(m_database, *m_kind).all()) {
				auto* item = new QListWidgetItem(lookup.name, m_list);
				item->setData(IdRole, lookup.id);
			}
		}
	});
	int row = m_list->count() > 0 ? 0 : -1;
	for (int i = 0; i < m_list->count(); ++i) {
		if (m_list->item(i)->data(IdRole).toString() == selectId)
			row = i;
	}
	m_list->setCurrentRow(row);
	m_list->blockSignals(false);
	showSelected();
}

void ManageDialog::showSelected()
{
	const QListWidgetItem* item = m_list->currentItem();
	if (!item) {
		startNew();
		return;
	}
	m_currentId = item->data(IdRole).toString();
	runReportingErrors(this, windowTitle(), [&] {
		int usage = 0;
		if (isAuthors()) {
			const AuthorRepository repository(m_database);
			const auto author = repository.find(m_currentId);
			if (!author)
				return;
			m_name->setText(author->name);
			m_birthDate->setText(author->birthDate);
			m_description->setPlainText(author->bio);
			usage = repository.usageCount(m_currentId);
		} else {
			const LookupRepository repository(m_database, *m_kind);
			const auto lookup = repository.find(m_currentId);
			if (!lookup)
				return;
			m_name->setText(lookup->name);
			if (m_description)
				m_description->setPlainText(lookup->description);
			usage = repository.usageCount(m_currentId);
		}
		m_usage->setText(tr("Used by %n book(s).", nullptr, usage));
	});
	m_delete->setEnabled(true);
}

void ManageDialog::startNew()
{
	m_list->blockSignals(true);
	m_list->setCurrentRow(-1);
	m_list->blockSignals(false);
	m_currentId.clear();
	m_name->clear();
	if (m_description)
		m_description->clear();
	if (m_birthDate)
		m_birthDate->clear();
	m_usage->setText(tr("New %1: fill in the details and press Save.").arg(singular().toLower()));
	m_delete->setEnabled(false);
	m_name->setFocus();
}

bool ManageDialog::saveCurrent()
{
	if (m_name->text().trimmed().isEmpty()) {
		QMessageBox::warning(this, windowTitle(), tr("Please enter a name."));
		return false;
	}
	QString savedId = m_currentId;
	const bool ok = runReportingErrors(this, windowTitle(), [&] {
		if (isAuthors()) {
			const AuthorRepository repository(m_database);
			Author author{m_currentId, m_name->text().trimmed(), m_description->toPlainText().trimmed(),
				m_birthDate->text().trimmed()};
			if (m_currentId.isEmpty())
				savedId = repository.insert(author);
			else
				repository.update(author);
		} else {
			const LookupRepository repository(m_database, *m_kind);
			LookupItem lookup{m_currentId, m_name->text().trimmed(),
				m_description ? m_description->toPlainText().trimmed() : QString()};
			if (m_currentId.isEmpty())
				savedId = repository.insert(lookup);
			else
				repository.update(lookup);
		}
	});
	if (ok) {
		m_changed = true;
		reload(savedId);
	}
	return ok;
}

bool ManageDialog::deleteCurrent(bool confirm)
{
	if (m_currentId.isEmpty())
		return false;
	if (confirm) {
		int usage = 0;
		runReportingErrors(this, windowTitle(), [&] {
			usage = isAuthors() ? AuthorRepository(m_database).usageCount(m_currentId)
								: LookupRepository(m_database, *m_kind).usageCount(m_currentId);
		});
		QString question = tr("Delete the %1 \"%2\"?").arg(singular().toLower(), m_name->text());
		if (usage > 0)
			question += QLatin1Char(' ')
				+ tr("%n book(s) use it and will no longer be linked to it. "
					"The books themselves are not deleted.", nullptr, usage);
		if (QMessageBox::question(this, windowTitle(), question) != QMessageBox::Yes)
			return false;
	}
	const bool ok = runReportingErrors(this, windowTitle(), [&] {
		if (isAuthors())
			AuthorRepository(m_database).remove(m_currentId);
		else
			LookupRepository(m_database, *m_kind).remove(m_currentId);
	});
	if (ok) {
		m_changed = true;
		reload();
	}
	return ok;
}

} // namespace mbt
