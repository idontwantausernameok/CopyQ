/*
    Copyright (c) 2016, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "configurationmanager.h"
#include "ui_configurationmanager.h"

#include "common/appconfig.h"
#include "common/command.h"
#include "common/common.h"
#include "common/config.h"
#include "common/log.h"
#include "common/mimetypes.h"
#include "common/option.h"
#include "common/settings.h"
#include "gui/iconfactory.h"
#include "gui/icons.h"
#include "gui/pluginwidget.h"
#include "gui/tabicons.h"
#include "gui/windowgeometryguard.h"
#include "item/clipboardmodel.h"
#include "item/itemdelegate.h"
#include "item/itemfactory.h"
#include "item/itemwidget.h"
#include "platform/platformnativeinterface.h"

#include <QDesktopWidget>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QTranslator>

#ifdef Q_OS_WIN
#   define DEFAULT_EDITOR "notepad %1"
#elif defined(Q_OS_MAC)
#   define DEFAULT_EDITOR "open -t %1"
#else
#   define DEFAULT_EDITOR "gedit %1"
#endif

namespace {

class PluginItem : public ItemOrderList::Item {
public:
    explicit PluginItem(ItemLoaderInterface *loader)
        : m_loader(loader)
    {
    }

private:
    QWidget *createWidget(QWidget *parent) const
    {
        return new PluginWidget(m_loader, parent);
    }

    ItemLoaderInterface *m_loader;
};

QString defaultClipboardTabName()
{
    return ConfigurationManager::tr(
                "&clipboard", "Default name of the tab that automatically stores new clipboard content");
}

QString nativeLanguageName(const QString &localeName)
{
    // Traditional Chinese
    if (localeName == "zh_TW")
        return QString::fromUtf8("\xe6\xad\xa3\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87");

    // Simplified Chinese
    if (localeName == "zh_CN")
        return QString::fromUtf8("\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87");

    return QLocale(localeName).nativeLanguageName();
}

} // namespace

// singleton
ConfigurationManager *ConfigurationManager::m_Instance = 0;

ConfigurationManager *ConfigurationManager::instance()
{
    Q_ASSERT(m_Instance != NULL);
    return m_Instance;
}

ConfigurationManager::ConfigurationManager(ItemFactory *itemFactory, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ConfigurationManager)
    , m_options()
    , m_itemFactory(itemFactory)
    , m_optionWidgetsLoaded(false)
{
    ui->setupUi(this);
    setWindowIcon(appIcon());

    if ( !m_itemFactory->hasLoaders() )
        ui->tabItems->deleteLater();

    initOptions();

    connect(m_itemFactory, SIGNAL(error(QString)), SIGNAL(error(QString)));
}

ConfigurationManager::~ConfigurationManager()
{
    m_Instance = NULL;
    delete ui;
}

QString ConfigurationManager::defaultTabName() const
{
    const QString tab = AppConfig().option("clipboard_tab").toString();
    return tab.isEmpty() ? defaultClipboardTabName() : tab;
}

void ConfigurationManager::initTabIcons()
{
    QTabWidget *tw = ui->tabWidget;
    if ( !tw->tabIcon(0).isNull() )
        return;

    tw->setTabIcon( tw->indexOf(ui->tabGeneral), getIcon("", IconWrench) );
    tw->setTabIcon( tw->indexOf(ui->tabLayout), getIcon("", IconColumns) );
    tw->setTabIcon( tw->indexOf(ui->tabHistory), getIcon("", IconListAlt) );
    tw->setTabIcon( tw->indexOf(ui->tabItems), getIcon("", IconThList) );
    tw->setTabIcon( tw->indexOf(ui->tabTray), getIcon("", IconInbox) );
    tw->setTabIcon( tw->indexOf(ui->tabNotifications), getIcon("", IconInfoSign) );
    tw->setTabIcon( tw->indexOf(ui->tabShortcuts), getIcon("", IconKeyboard) );
    tw->setTabIcon( tw->indexOf(ui->tabAppearance), getIcon("", IconPicture) );
}

void ConfigurationManager::initPluginWidgets()
{
    if (!m_itemFactory->hasLoaders())
        return;

    ui->itemOrderListPlugins->clearItems();

    foreach ( ItemLoaderInterface *loader, m_itemFactory->loaders() ) {
        ItemOrderList::ItemPtr pluginItem(new PluginItem(loader));
        const QIcon icon = getIcon(loader->icon());
        ui->itemOrderListPlugins->appendItem(
                    loader->name(), m_itemFactory->isLoaderEnabled(loader), false, icon, pluginItem );
    }
}

void ConfigurationManager::initLanguages()
{
    ui->comboBoxLanguage->addItem("English");
    ui->comboBoxLanguage->setItemData(0, "en");

    const QString currentLocale = QLocale().name();
    bool currentLocaleFound = false; // otherwise not found or partial match ("uk" partially matches locale "uk_UA")
    QSet<QString> languages;

    foreach ( const QString &path, qApp->property("CopyQ_translation_directories").toStringList() ) {
        foreach ( const QString &item, QDir(path).entryList(QStringList("copyq_*.qm")) ) {
            const int i = item.indexOf('_');
            const QString locale = item.mid(i + 1, item.lastIndexOf('.') - i - 1);
            const QString language = nativeLanguageName(locale);

            if (!language.isEmpty()) {
                languages.insert(language);
                const int index = ui->comboBoxLanguage->count();
                ui->comboBoxLanguage->addItem(language);
                ui->comboBoxLanguage->setItemData(index, locale);

                if (!currentLocaleFound) {
                    currentLocaleFound = (locale == currentLocale);
                    if (currentLocaleFound || currentLocale.startsWith(locale + "_"))
                        ui->comboBoxLanguage->setCurrentIndex(index);
                }
            }
        }
    }

    ui->comboBoxLanguage->setSizeAdjustPolicy(QComboBox::AdjustToContents);
}

void ConfigurationManager::updateAutostart()
{
    PlatformPtr platform = createPlatformNativeInterface();

    if ( platform->canAutostart() ) {
        bind("autostart", ui->checkBoxAutostart, platform->isAutostartEnabled());
    } else {
        ui->checkBoxAutostart->hide();
    }
}

void ConfigurationManager::setAutostartEnable()
{
    PlatformPtr platform = createPlatformNativeInterface();
    platform->setAutostartEnabled( AppConfig().option("autostart").toBool() );
}

void ConfigurationManager::initOptions()
{
    /* general options */
    bind("autostart", ui->checkBoxAutostart, false);
    bind("clipboard_tab", ui->comboBoxClipboardTab->lineEdit(), defaultClipboardTabName());
    bind("maxitems", ui->spinBoxItems, 200);
    bind("expire_tab", ui->spinBoxExpireTab, 0);
    bind("editor", ui->lineEditEditor, DEFAULT_EDITOR);
    bind("item_popup_interval", ui->spinBoxNotificationPopupInterval, 0);
    bind("notification_position", ui->comboBoxNotificationPosition, 3);
    bind("clipboard_notification_lines", ui->spinBoxClipboardNotificationLines, 0);
    bind("notification_horizontal_offset", ui->spinBoxNotificationHorizontalOffset, 10);
    bind("notification_vertical_offset", ui->spinBoxNotificationVerticalOffset, 10);
    bind("notification_maximum_width", ui->spinBoxNotificationMaximumWidth, 300);
    bind("notification_maximum_height", ui->spinBoxNotificationMaximumHeight, 100);
    bind("edit_ctrl_return", ui->checkBoxEditCtrlReturn, true);
    bind("move", ui->checkBoxMove, true);
    bind("check_clipboard", ui->checkBoxClip, true);
    bind("confirm_exit", ui->checkBoxConfirmExit, true);
    bind("vi", ui->checkBoxViMode, false);
    bind("save_filter_history", ui->checkBoxSaveFilterHistory, false);
    bind("always_on_top", ui->checkBoxAlwaysOnTop, false);
    bind("open_windows_on_current_screen", ui->checkBoxOpenWindowsOnCurrentScreen, true);
    bind("transparency_focused", ui->spinBoxTransparencyFocused, 0);
    bind("transparency", ui->spinBoxTransparencyUnfocused, 0);
    bind("hide_tabs", ui->checkBoxHideTabs, false);
    bind("hide_toolbar", ui->checkBoxHideToolbar, false);
    bind("hide_toolbar_labels", ui->checkBoxHideToolbarLabels, true);
    bind("disable_tray", ui->checkBoxDisableTray, false);
    bind("hide_main_window", ui->checkBoxHideWindow, false);
    bind("tab_tree", ui->checkBoxTabTree, false);
    bind("show_tab_item_count", ui->checkBoxShowTabItemCount, false);
    bind("text_wrap", ui->checkBoxTextWrap, true);

    bind("activate_closes", ui->checkBoxActivateCloses, true);
    bind("activate_focuses", ui->checkBoxActivateFocuses, false);
    bind("activate_pastes", ui->checkBoxActivatePastes, false);

    bind("tray_items", ui->spinBoxTrayItems, 5);
    bind("tray_item_paste", ui->checkBoxPasteMenuItem, true);
    bind("tray_commands", ui->checkBoxTrayShowCommands, true);
    bind("tray_tab_is_current", ui->checkBoxMenuTabIsCurrent, true);
    bind("tray_images", ui->checkBoxTrayImages, true);
    bind("tray_tab", ui->comboBoxMenuTab->lineEdit(), "");

    /* other options */
    bind("command_history_size", 100);
#ifdef COPYQ_WS_X11
    /* X11 clipboard selection monitoring and synchronization */
    bind("check_selection", ui->checkBoxSel, false);
    bind("copy_clipboard", ui->checkBoxCopyClip, false);
    bind("copy_selection", ui->checkBoxCopySel, false);
#else
    ui->checkBoxCopySel->hide();
    ui->checkBoxSel->hide();
    ui->checkBoxCopyClip->hide();
#endif

    // values of last submitted action
    bind("action_has_input", false);
    bind("action_has_output", false);
    bind("action_separator", "\\n");
    bind("action_output_tab", "");
}

void ConfigurationManager::bind(const char *optionKey, QCheckBox *obj, bool defaultValue)
{
    m_options[optionKey] = Option(defaultValue, "checked", obj);
}

void ConfigurationManager::bind(const char *optionKey, QSpinBox *obj, int defaultValue)
{
    m_options[optionKey] = Option(defaultValue, "value", obj);
}

void ConfigurationManager::bind(const char *optionKey, QLineEdit *obj, const QString &defaultValue)
{
    m_options[optionKey] = Option(defaultValue, "text", obj);
}

void ConfigurationManager::bind(const char *optionKey, QComboBox *obj, int defaultValue)
{
    m_options[optionKey] = Option(defaultValue, "currentIndex", obj);
}

void ConfigurationManager::bind(const char *optionKey, const QVariant &defaultValue)
{
    m_options[optionKey] = Option(defaultValue);
}

void ConfigurationManager::updateTabComboBoxes()
{
    initTabComboBox(ui->comboBoxClipboardTab);
    initTabComboBox(ui->comboBoxMenuTab);
}

QStringList ConfigurationManager::options() const
{
    QStringList options;
    foreach ( const QString &option, m_options.keys() ) {
        if ( m_options[option].value().canConvert(QVariant::String)
             && !optionToolTip(option).isEmpty() )
        {
            options.append(option);
        }
    }

    return options;
}

QString ConfigurationManager::optionValue(const QString &name) const
{
    return m_options.value(name).value().toString();
}

QString ConfigurationManager::optionToolTip(const QString &name) const
{
    return m_options[name].tooltip();
}

void ConfigurationManager::loadSettings()
{
    QSettings settings;

    settings.beginGroup("Options");
    foreach ( const QString &key, m_options.keys() ) {
        if ( settings.contains(key) ) {
            QVariant value = settings.value(key);
            if ( !value.isValid() || !m_options[key].setValue(value) )
                log( tr("Invalid value for option \"%1\"").arg(key), LogWarning );
        } else {
            m_options[key].reset();
        }
    }
    settings.endGroup();

    settings.beginGroup("Shortcuts");
    tabShortcuts()->loadShortcuts(settings);
    settings.endGroup();

    settings.beginGroup("Theme");
    ui->configTabAppearance->loadTheme(settings);
    settings.endGroup();

    ui->configTabAppearance->setEditor( AppConfig().option("editor").toString() );

    // load settings for each plugin
    settings.beginGroup("Plugins");
    foreach ( ItemLoaderInterface *loader, m_itemFactory->loaders() ) {
        settings.beginGroup(loader->id());

        QVariantMap s;
        foreach (const QString &name, settings.allKeys()) {
            s[name] = settings.value(name);
        }
        loader->loadSettings(s);
        m_itemFactory->setLoaderEnabled( loader, settings.value("enabled", true).toBool() );

        settings.endGroup();
    }
    settings.endGroup();

    // load plugin priority
    const QStringList pluginPriority =
            settings.value("plugin_priority", QStringList()).toStringList();
    m_itemFactory->setPluginPriority(pluginPriority);

    on_checkBoxMenuTabIsCurrent_stateChanged( ui->checkBoxMenuTabIsCurrent->checkState() );

    updateTabComboBoxes();

    updateAutostart();
}

void ConfigurationManager::on_buttonBox_clicked(QAbstractButton* button)
{
    int answer;

    switch( ui->buttonBox->buttonRole(button) ) {
    case QDialogButtonBox::ApplyRole:
        apply();
        break;
    case QDialogButtonBox::AcceptRole:
        accept();
        break;
    case QDialogButtonBox::RejectRole:
        reject();
        break;
    case QDialogButtonBox::ResetRole:
        // ask before resetting values
        answer = QMessageBox::question(
                    this,
                    tr("Reset preferences?"),
                    tr("This action will reset all your preferences (in all tabs) to default values.<br /><br />"
                       "Do you really want to <strong>reset all preferences</strong>?"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            foreach ( const QString &key, m_options.keys() ) {
                m_options[key].reset();
            }
        }
        break;
    default:
        return;
    }
}

ConfigTabShortcuts *ConfigurationManager::tabShortcuts() const
{
    return ui->configTabShortcuts;
}

void ConfigurationManager::setVisible(bool visible)
{
    QDialog::setVisible(visible);

    if (visible) {
        initTabIcons();
        initPluginWidgets();
        initLanguages();
        m_optionWidgetsLoaded = true;
    }
}

ConfigurationManager *ConfigurationManager::createInstance(ItemFactory *itemFactory, QWidget *parent)
{
    Q_ASSERT(m_Instance == NULL);
    m_Instance = new ConfigurationManager(itemFactory, parent);
    m_Instance->loadSettings();
    WindowGeometryGuard::create(m_Instance);
    return m_Instance;
}

void ConfigurationManager::apply()
{
    Settings settings;

    settings.beginGroup("Options");
    foreach ( const QString &key, m_options.keys() ) {
        settings.setValue( key, m_options[key].value() );
    }
    settings.endGroup();

    // Save configuration without command line alternatives only if option widgets are initialized
    // (i.e. clicked OK or Apply in configuration dialog).
    if (m_optionWidgetsLoaded) {
        settings.beginGroup("Shortcuts");
        tabShortcuts()->saveShortcuts(*settings.settingsData());
        settings.endGroup();

        settings.beginGroup("Theme");
        ui->configTabAppearance->saveTheme(*settings.settingsData());
        settings.endGroup();

        // save settings for each plugin
        if ( m_itemFactory->hasLoaders() ) {
            settings.beginGroup("Plugins");
            for (int i = 0; i < ui->itemOrderListPlugins->itemCount(); ++i) {
                bool isPluginEnabled = ui->itemOrderListPlugins->isItemChecked(i);
                QWidget *w = ui->itemOrderListPlugins->widget(i);
                if (w) {
                    PluginWidget *pluginWidget = qobject_cast<PluginWidget *>(w);
                    pluginWidget->applySettings(settings.settingsData(), isPluginEnabled);
                    m_itemFactory->setLoaderEnabled(pluginWidget->loader(), isPluginEnabled);
                }
            }
            settings.endGroup();

            // save plugin priority
            QStringList pluginPriority;
            for (int i = 0; i <  ui->itemOrderListPlugins->itemCount(); ++i)
                pluginPriority.append( ui->itemOrderListPlugins->itemLabel(i) );
            settings.setValue("plugin_priority", pluginPriority);
            m_itemFactory->setPluginPriority(pluginPriority);
        }
    }

    ui->configTabAppearance->setEditor( AppConfig().option("editor").toString() );

    setAutostartEnable();

    // Language changes after restart.
    const int newLocaleIndex = ui->comboBoxLanguage->currentIndex();
    const QString newLocaleName = ui->comboBoxLanguage->itemData(newLocaleIndex).toString();
    settings.setValue("Options/language", newLocaleName);
    const QLocale oldLocale;
    if (QLocale(newLocaleName).name() != oldLocale.name()) {
        QMessageBox::information( this, tr("Restart Required"),
                                  tr("Language will be changed after application is restarted.") );
    }

    emit configurationChanged();
}

void ConfigurationManager::done(int result)
{
    if (result == QDialog::Accepted) {
        apply();
    } else {
        loadSettings();
    }

    QDialog::done(result);

    if (!isVisible()) {
        m_optionWidgetsLoaded = false;
        if ( m_itemFactory->hasLoaders() )
            ui->itemOrderListPlugins->clearItems();

        ui->comboBoxLanguage->clear();
    }
}

void ConfigurationManager::on_checkBoxMenuTabIsCurrent_stateChanged(int state)
{
    ui->comboBoxMenuTab->setEnabled(state == Qt::Unchecked);
}

void ConfigurationManager::on_spinBoxTrayItems_valueChanged(int value)
{
    ui->checkBoxPasteMenuItem->setEnabled(value > 0);
}
