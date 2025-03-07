#include "optionsmodel.h"

#include "bitcoinunits.h"
#include "guiutil.h"
#include "init.h"
#include "wallet.h"
#include "walletdb.h"

#include <QSettings>

OptionsModel::OptionsModel(QObject* parent) : QAbstractListModel(parent) {
	Init();
}

bool static ApplyProxySettings() {
	QSettings settings;
	CService  addrProxy(settings.value("addrProxy", "127.0.0.1:9050").toString().toStdString());
	if (!settings.value("fUseProxy", false).toBool()) {
		addrProxy = CService();
		return false;
	}
	if (!addrProxy.IsValid())
		return false;
	if (!IsLimited(NET_IPV4))
		SetProxy(NET_IPV4, addrProxy);
	if (!IsLimited(NET_IPV6))
		SetProxy(NET_IPV6, addrProxy);
	SetNameProxy(addrProxy);
	return true;
}

void OptionsModel::Init() {
	QSettings settings;

	// These are Qt-only settings:
	nDisplayUnit         = settings.value("nDisplayUnit", BitcoinUnits::BTC).toInt();
	fMinimizeToTray      = settings.value("fMinimizeToTray", false).toBool();
	fMinimizeOnClose     = settings.value("fMinimizeOnClose", false).toBool();
	fCoinControlFeatures = settings.value("fCoinControlFeatures", false).toBool();
	fNotifications       = settings.value("fNotifications", true).toBool();
	nTransactionFee      = settings.value("nTransactionFee").toLongLong();
	nNoStakeBalance      = settings.value("nNoStakeBalance").toLongLong();
	nFontScale           = settings.value("nFontScale", 100).toInt();
	language             = settings.value("language", "").toString();

	if (nFontScale < 50 || nFontScale > 200) {
		nFontScale = 100;
	}
	// These are shared with core Bitcoin; we want
	// command-line options to override the GUI settings:
	if (settings.contains("fUseUPnP"))
		SoftSetBoolArg("-upnp", settings.value("fUseUPnP").toBool());
	if (settings.contains("addrProxy") && settings.value("fUseProxy").toBool())
		SoftSetArg("-proxy", settings.value("addrProxy").toString().toStdString());
	if (!language.isEmpty())
		SoftSetArg("-lang", language.toStdString());
}

int OptionsModel::rowCount(const QModelIndex& parent) const {
	return OptionIDRowCount;
}

QVariant OptionsModel::data(const QModelIndex& index, int role) const {
	if (role == Qt::EditRole) {
		QSettings settings;
		switch (index.row()) {
			case StartAtStartup:
				return QVariant(GUIUtil::GetStartOnSystemStartup());
			case MinimizeToTray:
				return QVariant(fMinimizeToTray);
			case MapPortUPnP:
				return settings.value("fUseUPnP", GetBoolArg("-upnp", true));
			case MinimizeOnClose:
				return QVariant(fMinimizeOnClose);
			case ProxyUse:
				return settings.value("fUseProxy", false);
			case ProxyIP: {
				proxyType proxy;
				if (GetProxy(NET_IPV4, proxy))
					return QVariant(QString::fromStdString(proxy.ToStringIP()));
				else
					return QVariant(QString::fromStdString("127.0.0.1"));
			}
			case ProxyPort: {
				proxyType proxy;
				if (GetProxy(NET_IPV4, proxy))
					return QVariant(proxy.GetPort());
				else
					return QVariant(9050);
			}
			case Fee:
				return QVariant((qint64)nTransactionFee);
			case ReserveBalance:
				return QVariant((qint64)nNoStakeBalance);
			case DisplayUnit:
				return QVariant(nDisplayUnit);
			case Language:
				return settings.value("language", "");
			case CoinControlFeatures:
				return QVariant(fCoinControlFeatures);
			case Notifications:
				return QVariant(fNotifications);
			case FontScale:
				return QVariant(nFontScale);
			default:
				return QVariant();
		}
	}
	return QVariant();
}

bool OptionsModel::setData(const QModelIndex& index, const QVariant& value, int role) {
	bool successful = true; /* set to false on parse error */
	if (role == Qt::EditRole) {
		QSettings settings;
		switch (index.row()) {
			case StartAtStartup:
				successful = GUIUtil::SetStartOnSystemStartup(value.toBool());
				break;
			case MinimizeToTray:
				fMinimizeToTray = value.toBool();
				settings.setValue("fMinimizeToTray", fMinimizeToTray);
				break;
			case MapPortUPnP:
				settings.setValue("fUseUPnP", value.toBool());
				MapPort(value.toBool());
				break;
			case MinimizeOnClose:
				fMinimizeOnClose = value.toBool();
				settings.setValue("fMinimizeOnClose", fMinimizeOnClose);
				break;
			case ProxyUse:
				settings.setValue("fUseProxy", value.toBool());
				ApplyProxySettings();
				break;
			case ProxyIP: {
				proxyType proxy;
				proxy = CService("127.0.0.1", 9050);
				GetProxy(NET_IPV4, proxy);

				CNetAddr addr(value.toString().toStdString());
				proxy.SetIP(addr);
				settings.setValue("addrProxy", proxy.ToStringIPPort().c_str());
				successful = ApplyProxySettings();
			} break;
			case ProxyPort: {
				proxyType proxy;
				proxy = CService("127.0.0.1", 9050);
				GetProxy(NET_IPV4, proxy);

				proxy.SetPort(value.toInt());
				settings.setValue("addrProxy", proxy.ToStringIPPort().c_str());
				successful = ApplyProxySettings();
			} break;
			case Fee:
				nTransactionFee = value.toLongLong();
				settings.setValue("nTransactionFee", (qint64)nTransactionFee);
				emit transactionFeeChanged(nTransactionFee);
				break;
			case ReserveBalance:
				nNoStakeBalance = value.toLongLong();
				settings.setValue("nNoStakeBalance", (qint64)nNoStakeBalance);
				emit nostakeBalanceChanged(nNoStakeBalance);
				break;
			case DisplayUnit:
				nDisplayUnit = value.toInt();
				settings.setValue("nDisplayUnit", nDisplayUnit);
				emit displayUnitChanged(nDisplayUnit);
				break;
			case Language:
				settings.setValue("language", value);
				break;
			case CoinControlFeatures: {
				fCoinControlFeatures = value.toBool();
				settings.setValue("fCoinControlFeatures", fCoinControlFeatures);
				emit coinControlFeaturesChanged(fCoinControlFeatures);
			} break;
			case Notifications: {
				fNotifications = value.toBool();
				settings.setValue("fNotifications", fNotifications);
				emit notificationsChanged(fNotifications);
			} break;
			case FontScale: {
				QString v = value.toString();
				if (v.endsWith("%")) {
					v = v.left(v.length() - 1);
				}
				nFontScale = v.toInt();
				settings.setValue("nFontScale", nFontScale);
				emit fontScaleChanged(nFontScale);
			} break;
			default:
				break;
		}
	}
	emit dataChanged(index, index);

	return successful;
}

qint64 OptionsModel::getTransactionFee() {
	return nTransactionFee;
}

qint64 OptionsModel::getReserveBalance() {
	return nNoStakeBalance;
}

bool OptionsModel::getCoinControlFeatures() {
	return fCoinControlFeatures;
}

bool OptionsModel::getNotificationsOn() {
	return fNotifications;
}

bool OptionsModel::getMinimizeToTray() {
	return fMinimizeToTray;
}

bool OptionsModel::getMinimizeOnClose() {
	return fMinimizeOnClose;
}

int OptionsModel::getDisplayUnit() {
	return nDisplayUnit;
}

int OptionsModel::getFontScale() {
	return nFontScale;
}
