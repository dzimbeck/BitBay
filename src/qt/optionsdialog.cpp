#include "optionsdialog.h"
#include "ui_optionsdialog.h"

#include "bitcoinunits.h"
#include "guiutil.h"
#include "monitoreddatamapper.h"
#include "netbase.h"
#include "optionsmodel.h"
#include "txdb-leveldb.h"

#include <QDir>
#include <QIntValidator>
#include <QLocale>
#include <QMessageBox>

OptionsDialog::OptionsDialog(QWidget* parent)
    : QDialog(parent),
      ui(new Ui::OptionsDialog),
      model(0),
      mapper(0),
      fRestartWarningDisplayed_Proxy(false),
      fRestartWarningDisplayed_Lang(false),
      fProxyIpValid(true) {
	ui->setupUi(this);
	GUIUtil::SetBitBayFonts(this);

	/* Network elements init */
#ifndef USE_UPNP
	ui->mapPortUpnp->setEnabled(false);
#endif

	ui->proxyIp->setEnabled(false);
	ui->proxyPort->setEnabled(false);
	ui->proxyPort->setValidator(new QIntValidator(1, 65535, this));

	connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->proxyIp, SLOT(setEnabled(bool)));
	connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->proxyPort, SLOT(setEnabled(bool)));
	connect(ui->connectSocks, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning_Proxy()));

	ui->proxyIp->installEventFilter(this);

	/* Window elements init */
#ifdef Q_OS_MAC
	ui->tabWindow->setVisible(false);
#endif

	/* Display elements init */
	QDir translations(":translations");
	ui->lang->addItem(QString("(") + tr("default") + QString(")"), QVariant(""));
	foreach (const QString& langStr, translations.entryList()) {
		QLocale locale(langStr);

		/** check if the locale name consists of 2 parts (language_country) */
		if (langStr.contains("_")) {
#if QT_VERSION >= 0x040800
			/** display language strings as "native language - native country (locale name)", e.g.
			 * "Deutsch - Deutschland (de)" */
			ui->lang->addItem(locale.nativeLanguageName() + QString(" - ") +
			                      locale.nativeCountryName() + QString(" (") + langStr +
			                      QString(")"),
			                  QVariant(langStr));
#else
			/** display language strings as "language - country (locale name)", e.g. "German -
			 * Germany (de)" */
			ui->lang->addItem(QLocale::languageToString(locale.language()) + QString(" - ") +
			                      QLocale::countryToString(locale.country()) + QString(" (") +
			                      langStr + QString(")"),
			                  QVariant(langStr));
#endif
		} else {
#if QT_VERSION >= 0x040800
			/** display language strings as "native language (locale name)", e.g. "Deutsch (de)" */
			ui->lang->addItem(locale.nativeLanguageName() + QString(" (") + langStr + QString(")"),
			                  QVariant(langStr));
#else
			/** display language strings as "language (locale name)", e.g. "German (de)" */
			ui->lang->addItem(QLocale::languageToString(locale.language()) + QString(" (") +
			                      langStr + QString(")"),
			                  QVariant(langStr));
#endif
		}
	}

	ui->unit->setModel(new BitcoinUnits(this));

	{
		ui->fontScale->clear();
		ui->fontScale->addItem(tr("50%"), 50);
		ui->fontScale->addItem(tr("75%"), 75);
		ui->fontScale->addItem(tr("100%"), 100);
		ui->fontScale->addItem(tr("125%"), 125);
		ui->fontScale->addItem(tr("150%"), 150);
		ui->fontScale->addItem(tr("175%"), 175);
		ui->fontScale->addItem(tr("200%"), 200);
	}

	/* Widget-to-option mapper */
	mapper = new MonitoredDataMapper(this);
	mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
	mapper->setOrientation(Qt::Vertical);

	/* enable apply button when data modified */
	connect(mapper, SIGNAL(viewModified()), this, SLOT(enableApplyButton()));
	/* disable apply button when new data loaded */
	connect(mapper, SIGNAL(currentIndexChanged(int)), this, SLOT(disableApplyButton()));
	/* setup/change UI elements when proxy IP is invalid/valid */
	connect(this, SIGNAL(proxyIpValid(QValidatedLineEdit*, bool)), this,
	        SLOT(handleProxyIpValid(QValidatedLineEdit*, bool)));

	{
		LOCK(cs_main);
		CTxDB txdb("r");
		bool  fPegPruneEnabled = true;
		if (!txdb.ReadPegPruneEnabled(fPegPruneEnabled)) {
			fPegPruneEnabled = true;
		}
		ui->prunePegInfo->setChecked(fPegPruneEnabled);
#ifdef ENABLE_EXCHANGE
		ui->prunePegInfo->setEnabled(false);
#endif
#ifdef ENABLE_EXPLORER
		ui->prunePegInfo->setEnabled(false);
#endif
	}
	connect(ui->prunePegInfo, SIGNAL(toggled(bool)), this, SLOT(changePegPrune(bool)));
}

OptionsDialog::~OptionsDialog() {
	delete ui;
}

void OptionsDialog::setModel(OptionsModel* model) {
	this->model = model;

	if (model) {
		connect(model, SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
		connect(model, SIGNAL(fontScaleChanged(int)), this, SLOT(updateFontScale()));

		mapper->setModel(model);
		setMapper();
		mapper->toFirst();
	}

	/* update the display unit, to not use the default ("BTC") */
	updateDisplayUnit();

	updateFontScale();

	/* warn only when language selection changes by user action (placed here so init via mapper
	 * doesn't trigger this) */
	connect(ui->lang, SIGNAL(valueChanged()), this, SLOT(showRestartWarning_Lang()));

	/* disable apply button after settings are loaded as there is nothing to save */
	disableApplyButton();
}

void OptionsDialog::setMapper() {
	/* Main */
	mapper->addMapping(ui->transactionFee, OptionsModel::Fee);
	mapper->addMapping(ui->reserveBalance, OptionsModel::ReserveBalance);
	mapper->addMapping(ui->bitcoinAtStartup, OptionsModel::StartAtStartup);

	/* Network */
	mapper->addMapping(ui->mapPortUpnp, OptionsModel::MapPortUPnP);

	mapper->addMapping(ui->connectSocks, OptionsModel::ProxyUse);
	mapper->addMapping(ui->proxyIp, OptionsModel::ProxyIP);
	mapper->addMapping(ui->proxyPort, OptionsModel::ProxyPort);

	/* Window */
#ifndef Q_OS_MAC
	mapper->addMapping(ui->minimizeToTray, OptionsModel::MinimizeToTray);
	mapper->addMapping(ui->minimizeOnClose, OptionsModel::MinimizeOnClose);
#endif

	/* Display */
	mapper->addMapping(ui->lang, OptionsModel::Language);
	mapper->addMapping(ui->unit, OptionsModel::DisplayUnit);
	mapper->addMapping(ui->fontScale, OptionsModel::FontScale);
	mapper->addMapping(ui->coinControlFeatures, OptionsModel::CoinControlFeatures);
	mapper->addMapping(ui->displayNotifications, OptionsModel::Notifications);
}

void OptionsDialog::enableApplyButton() {
	ui->applyButton->setEnabled(true);
}

void OptionsDialog::disableApplyButton() {
	ui->applyButton->setEnabled(false);
}

void OptionsDialog::enableSaveButtons() {
	/* prevent enabling of the save buttons when data modified, if there is an invalid proxy address
	 * present */
	if (fProxyIpValid)
		setSaveButtonState(true);
}

void OptionsDialog::disableSaveButtons() {
	setSaveButtonState(false);
}

void OptionsDialog::setSaveButtonState(bool fState) {
	ui->applyButton->setEnabled(fState);
	ui->okButton->setEnabled(fState);
}

void OptionsDialog::on_okButton_clicked() {
	mapper->submit();
	{
		LOCK(cs_main);
		CTxDB txdb("r+");
		txdb.WritePegPruneEnabled(ui->prunePegInfo->isChecked());
	}
	accept();
}

void OptionsDialog::on_cancelButton_clicked() {
	reject();
}

void OptionsDialog::on_applyButton_clicked() {
	mapper->submit();
	{
		LOCK(cs_main);
		CTxDB txdb("r+");
		txdb.WritePegPruneEnabled(ui->prunePegInfo->isChecked());
	}
	disableApplyButton();
}

void OptionsDialog::showRestartWarning_Proxy() {
	if (!fRestartWarningDisplayed_Proxy) {
		QMessageBox::warning(this, tr("Warning"),
		                     tr("This setting will take effect after restarting BitBay."),
		                     QMessageBox::Ok);
		fRestartWarningDisplayed_Proxy = true;
	}
}

void OptionsDialog::showRestartWarning_Lang() {
	if (!fRestartWarningDisplayed_Lang) {
		QMessageBox::warning(this, tr("Warning"),
		                     tr("This setting will take effect after restarting BitBay."),
		                     QMessageBox::Ok);
		fRestartWarningDisplayed_Lang = true;
	}
}

void OptionsDialog::updateDisplayUnit() {
	if (model) {
		/* Update transactionFee with the current unit */
		ui->transactionFee->setDisplayUnit(model->getDisplayUnit());
	}
}

void OptionsDialog::updateFontScale() {
	if (model) {
		ui->fontScale->setCurrentText(QString::number(model->getFontScale()) + "%");
	}
}

void OptionsDialog::handleProxyIpValid(QValidatedLineEdit* object, bool fState) {
	// this is used in a check before re-enabling the save buttons
	fProxyIpValid = fState;

	if (fProxyIpValid) {
		enableSaveButtons();
		ui->statusLabel->clear();
	} else {
		disableSaveButtons();
		object->setValid(fProxyIpValid);
		ui->statusLabel->setStyleSheet("QLabel { color: red; }");
		ui->statusLabel->setText(tr("The supplied proxy address is invalid."));
	}
}

bool OptionsDialog::eventFilter(QObject* object, QEvent* event) {
	if (event->type() == QEvent::FocusOut) {
		if (object == ui->proxyIp) {
			CService addr;
			/* Check proxyIp for a valid IPv4/IPv6 address and emit the proxyIpValid signal */
			emit proxyIpValid(ui->proxyIp,
			                  LookupNumeric(ui->proxyIp->text().toStdString().c_str(), addr));
		}
	}
	return QDialog::eventFilter(object, event);
}

void OptionsDialog::changePegPrune(bool f) {
	Q_UNUSED(f);
}
