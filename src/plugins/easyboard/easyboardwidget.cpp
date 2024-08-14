// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "easyboardwidget.h"
#include "easyboardtr.h"
#include "easyboardbrowser.h"
#include "easyboardmodel.h"
#include "boardsWidget/t113s.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/iwelcomepage.h>
#include <coreplugin/plugininstallwizard.h>
#include <coreplugin/welcomepagehelper.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <extensionsystem/pluginview.h>

#include <solutions/tasking/networkquery.h>
#include <solutions/tasking/tasktree.h>
#include <solutions/tasking/tasktreerunner.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/icon.h>
#include <utils/infolabel.h>
#include <utils/layoutbuilder.h>
#include <utils/networkaccessmanager.h>
#include <utils/stylehelper.h>
#include <utils/temporarydirectory.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QMessageBox>
#include <QMovie>
#include <QPainter>
#include <QProgressDialog>
#include <QScrollArea>
#include <QSignalMapper>

using namespace Core;
using namespace Utils;
using namespace StyleHelper;
using namespace WelcomePageHelpers;

namespace EasyBoard::Internal {

Q_LOGGING_CATEGORY(widgetLog, "qtc.easyboard.widget", QtWarningMsg)

constexpr TextFormat h5TF
    {Theme::Token_Text_Default, UiElement::UiElementH5};
constexpr TextFormat h6TF
    {h5TF.themeColor, UiElement::UiElementH6};
constexpr TextFormat h6CapitalTF
    {Theme::Token_Text_Muted, UiElement::UiElementH6Capital};
constexpr TextFormat contentTF
    {Theme::Token_Text_Default, UiElement::UiElementBody2};

static QLabel *sectionTitle(const TextFormat &tf, const QString &title)
{
    QLabel *label = tfLabel(tf, true);
    label->setText(title);
    return label;
};

static QWidget *toScrollableColumn(QWidget *widget)
{
    widget->setContentsMargins(SpacingTokens::ExVPaddingGapXl, SpacingTokens::ExVPaddingGapXl,
                               SpacingTokens::ExVPaddingGapXl, SpacingTokens::ExVPaddingGapXl);
    widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

    auto scrollArea = new QScrollArea;
    scrollArea->setWidget(widget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameStyle(QFrame::NoFrame);

    return scrollArea;
};

class CollapsingWidget : public QWidget
{
public:
    explicit CollapsingWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    }

    void setWidth(int width)
    {
        m_width = width;
        setVisible(width > 0);
        updateGeometry();
    }

    QSize sizeHint() const override
    {
        return {m_width, 0};
    }

private:
    int m_width = 100;
};

class HeadingWidget : public QWidget
{
    static constexpr int dividerH = 100;

    Q_OBJECT

public:
    explicit HeadingWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_icon = new QLabel;
        m_icon->setFixedSize(imgBgSize);

        static const TextFormat titleTF
            {Theme::Token_Text_Default, UiElementH4};
        static const TextFormat vendorTF
            {Theme::Token_Text_Accent, UiElementLabelMedium};
        static const TextFormat dlTF
            {Theme::Token_Text_Muted, vendorTF.uiElement};
        static const TextFormat detailsTF
            {Theme::Token_Text_Default, UiElementBody2};

        m_title = tfLabel(titleTF);
        m_vendor = new Button({}, Button::SmallLink);
        m_vendor->setContentsMargins({});
        m_divider = new QLabel;
        m_divider->setFixedSize(1, dividerH);
        WelcomePageHelpers::setBackgroundColor(m_divider, dlTF.themeColor);

        m_details = tfLabel(detailsTF);
        m_name = tfLabel(detailsTF);
        m_displayName = tfLabel(detailsTF);
        m_ip = tfLabel(detailsTF);
        m_version = tfLabel(detailsTF);
        m_date = tfLabel(detailsTF);
        m_type = tfLabel(detailsTF);
        m_isDefault = tfLabel(detailsTF);
        m_online = tfLabel(detailsTF);

        // installButton = new Button(Tr::tr("Default"), Button::MediumPrimary);
        // installButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        // installButton->hide();

        using namespace Layouting;
        Row {
            m_icon,
            Column {
                m_title,
                st,
                m_version,
                m_ip,
                st,
                m_date,
                m_type,
                m_isDefault,
                m_online,
                spacing(0),
            },
            m_details,
            // Column {
            //     m_details,
            //     st,
            // },
            noMargin, spacing(SpacingTokens::ExPaddingGapL),
        }.attachTo(this);

        setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);
        // m_dlCountItems->setVisible(false);

        // connect(installButton, &QAbstractButton::pressed,
        //         this, &HeadingWidget::pluginInstallationRequested);
        // connect(m_vendor, &QAbstractButton::pressed, this, [this]() {
        //     emit vendorClicked(m_currentVendor);
        // });

        update({});
    }

    void update(const QModelIndex &current)
    {
        if (!current.isValid())
            return;

        m_icon->setPixmap(boardIcon(current, SizeImge));//itemIcon SizeBig
        const QString dispname = current.data(EasyBoardModel::RoleDisplayName).toString();
        const QString name = current.data(EasyBoardModel::RoleName).toString();

        m_title->setText(dispname.isEmpty()?name:dispname+"("+name+")");

        m_ip->setText(current.data(EasyBoardModel::RoleIp).toString());
        m_version->setText(current.data(EasyBoardModel::RoleVersion).toString());
        m_date->setText(current.data(EasyBoardModel::RoleDate).toString());
        m_type->setText(current.data(EasyBoardModel::RoleItemType).value<ItemType>()==ItemTypeLocal?Tr::tr("Local"):Tr::tr("Network"));
        m_isDefault->setText(current.data(EasyBoardModel::RoleDefault).toBool()?Tr::tr("Default"):Tr::tr(""));
        m_online->setText(current.data(EasyBoardModel::RoleState).toBool()?Tr::tr("OnLine"):Tr::tr("OffLine"));

        // m_currentVendor = current.data(EasyBoardModel::RoleDate).toString();
        // m_vendor->setText(m_currentVendor);
        if(name.contains("t113",Qt::CaseInsensitive)){
            m_details->setText("T113 Test Value");
        }


        const ItemType itemType = current.data(EasyBoardModel::RoleItemType).value<ItemType>();
        const bool isPack = itemType == ItemTypeLocal;
        const bool isRemotePlugin = false;//!(isPack || pluginSpecForName(name));
        // installButton->setVisible(true);
        // if (installButton->isVisible())
        //     installButton->setToolTip("Set As Default");
    }

signals:
    void pluginInstallationRequested();
    void vendorClicked(const QString &vendor);

private:
    QLabel *m_icon;
    QLabel *m_title;
    Button *m_vendor;
    QLabel *m_divider;
    QLabel *m_compatVersion;
    QLabel *m_copyright;
    QLabel *m_id;
    QLabel *m_license;
    QLabel *m_name;
    QLabel *m_displayName;
    QLabel *m_ip;
    QLabel *m_version;
    QLabel *m_date;
    QLabel *m_type;
    QLabel *m_isDefault;
    QLabel *m_online;

    QLabel *m_details;
    // QAbstractButton *installButton;
    // QString m_currentVendor;
};

class TagList : public QWidget
{
    Q_OBJECT

public:
    explicit TagList(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        QHBoxLayout *layout = new QHBoxLayout(this);
        setLayout(layout);
        layout->setContentsMargins({});
        m_signalMapper = new QSignalMapper(this);
        connect(m_signalMapper, &QSignalMapper::mappedString, this, &TagList::tagSelected);
    }

    void setTags(const QStringList &tags)
    {
        if (m_container) {
            delete m_container;
            m_container = nullptr;
        }

        if (!tags.empty()) {
            m_container = new QWidget(this);
            layout()->addWidget(m_container);

            using namespace Layouting;
            Flow flow {};
            flow.setNoMargins();
            flow.setSpacing(SpacingTokens::HGapXs);

            for (const QString &tag : tags) {
                QAbstractButton *tagButton = new Button(tag, Button::Tag);
                connect(tagButton, &QAbstractButton::clicked,
                        m_signalMapper, qOverload<>(&QSignalMapper::map));
                m_signalMapper->setMapping(tagButton, tag);
                flow.addItem(tagButton);
            }

            flow.attachTo(m_container);
        }

        updateGeometry();
    }

signals:
    void tagSelected(const QString &tag);

private:
    QWidget *m_container = nullptr;
    QSignalMapper *m_signalMapper;
};

class EasyBoardWidget final : public Core::ResizeSignallingWidget
{
public:
    EasyBoardWidget();

private:
    void updateView(const QModelIndex &current);
    void fetchAndInstallPlugin(const QUrl &url);
    void fetchAndDisplayImage(const QUrl &url);

    QString m_currentItemName;
    EasyBoardBrowser *m_easyboardBrowser;
    // CollapsingWidget *m_secondaryDescriptionWidget;
    HeadingWidget *m_headingWidget;
    QWidget *m_primaryContent;
    // QWidget *m_secondaryContent;
    QLabel *m_description;
    QLabel *m_linksTitle;
    QLabel *m_links;
    QLabel *m_imageTitle;
    QLabel *m_image;
    QBuffer m_imageDataBuffer;
    QMovie m_imageMovie;
    QLabel *m_tagsTitle;
    TagList *m_tags;
    QLabel *m_compatVersionTitle;
    QLabel *m_compatVersion;
    QLabel *m_platformsTitle;
    QLabel *m_platforms;
    QLabel *m_dependenciesTitle;
    QLabel *m_dependencies;
    QLabel *m_packExtensionsTitle;
    QLabel *m_packExtensions;
    Tasking::TaskTreeRunner m_dlTaskTreeRunner;
    Tasking::TaskTreeRunner m_imgTaskTreeRunner;
};

EasyBoardWidget::EasyBoardWidget()
{
    m_easyboardBrowser = new EasyBoardBrowser;
    auto descriptionColumns = new QWidget;
    // m_secondaryDescriptionWidget = new CollapsingWidget;

    m_headingWidget = new HeadingWidget;
    m_description = tfLabel(contentTF, false);
    m_description->setWordWrap(true);
    m_linksTitle = sectionTitle(h6CapitalTF, Tr::tr("More information"));
    m_links = tfLabel(contentTF, false);
    m_links->setOpenExternalLinks(true);
    m_imageTitle = sectionTitle(h6CapitalTF, {});
    m_image = new QLabel;
    m_imageMovie.setDevice(&m_imageDataBuffer);

    using namespace Layouting;

    auto primary = new t113s;
    primary->setStyleSheet("QWidget { background-color: #bb229d; }"); // 设置背景颜色为红色

    const auto spL = spacing(SpacingTokens::VPaddingL);
    Column {
        m_description,
        Column { m_linksTitle, m_links, spL },
        Column { m_imageTitle, m_image, spL },
        st,
        noMargin, spacing(SpacingTokens::ExVPaddingGapXl),
    }.attachTo(primary);
    m_primaryContent = toScrollableColumn(primary);

    m_tagsTitle = sectionTitle(h6TF, Tr::tr("Tags"));
    m_tags = new TagList;
    m_compatVersionTitle = sectionTitle(h6TF, Tr::tr("Compatibility"));
    m_compatVersion = tfLabel(contentTF, false);
    m_platformsTitle = sectionTitle(h6TF, Tr::tr("Platforms"));
    m_platforms = tfLabel(contentTF, false);
    m_dependenciesTitle = sectionTitle(h6TF, Tr::tr("Dependencies"));
    m_dependencies = tfLabel(contentTF, false);
    m_packExtensionsTitle = sectionTitle(h6TF, Tr::tr("Extensions in pack"));
    m_packExtensions = tfLabel(contentTF, false);
    // m_pluginStatus = new PluginStatusWidget;

    Row {
        WelcomePageHelpers::createRule(Qt::Vertical),
        Row {
            Column {
                Column {
                    m_headingWidget,
                    customMargins(SpacingTokens::ExVPaddingGapXl, SpacingTokens::ExVPaddingGapXl,
                                  SpacingTokens::ExVPaddingGapXl, SpacingTokens::ExVPaddingGapXl),
                },
                m_primaryContent,
            },
        },
        noMargin, spacing(0),
    }.attachTo(descriptionColumns);

    Row {
        Space(SpacingTokens::ExVPaddingGapXl),
        m_easyboardBrowser,
        WelcomePageHelpers::createRule(Qt::Vertical),
        descriptionColumns,
        noMargin, spacing(0),
    }.attachTo(this);

    WelcomePageHelpers::setBackgroundColor(this, Theme::Token_Background_Default);

    // const int intendedBrowserColumnWidth = size.width() - 580;
    m_easyboardBrowser->adjustToWidth(300);

    connect(m_easyboardBrowser, &EasyBoardBrowser::itemChanged,
            this,&EasyBoardWidget::updateView);
    connect(m_easyboardBrowser, &EasyBoardBrowser::itemSelected,
            this, &EasyBoardWidget::updateView);
    connect(this, &ResizeSignallingWidget::resized, this, [this](const QSize &size) {
        // const bool secondaryDescriptionVisible = size.width() > 970;
        // const int secondaryDescriptionWidth = secondaryDescriptionVisible ? 264 : 0;
        // m_secondaryDescriptionWidget->setWidth(secondaryDescriptionWidth);
    });
    // connect(m_headingWidget, &HeadingWidget::pluginInstallationRequested, this, [this](){
    //     fetchAndInstallPlugin(QUrl::fromUserInput(m_currentItemPlugins.constFirst().second));
    // });
    connect(m_tags, &TagList::tagSelected, m_easyboardBrowser, &EasyBoardBrowser::setFilter);
    connect(m_headingWidget, &HeadingWidget::vendorClicked,
            m_easyboardBrowser, &EasyBoardBrowser::setFilter);

    updateView({});
}

void EasyBoardWidget::updateView(const QModelIndex &current)
{
    m_headingWidget->update(current);

    const bool showContent = current.isValid();

    // m_primaryContent->setVisible(showContent);
    // m_headingWidget->setVisible(showContent);


    if (!showContent)
        return;

    m_currentItemName = current.data().toString();
    // const bool isPack = current.data(RoleItemType) == ItemTypePack;
    // m_pluginStatus->setPluginName(isPack ? QString() : m_currentItemName);
    // m_currentItemPlugins = current.data(RolePlugins).value<PluginsData>();


}

void EasyBoardWidget::fetchAndInstallPlugin(const QUrl &url)
{
    using namespace Tasking;

    struct StorageStruct
    {
        StorageStruct() {
            progressDialog.reset(new QProgressDialog(
                Tr::tr("Downloading..."), Tr::tr("Cancel"), 0, 0, ICore::dialogParent()));
            progressDialog->setWindowTitle(Tr::tr("Download Extension"));
            progressDialog->setWindowModality(Qt::ApplicationModal);
            progressDialog->setFixedSize(progressDialog->sizeHint());
            progressDialog->setAutoClose(false);
            progressDialog->show(); // TODO: Should not be needed. Investigate possible QT_BUG
        }
        std::unique_ptr<QProgressDialog> progressDialog;
        QByteArray packageData;
        QUrl url;
    };
    Storage<StorageStruct> storage;

    const auto onQuerySetup = [url, storage](NetworkQuery &query) {
        storage->url = url;
        query.setRequest(QNetworkRequest(url));
        query.setNetworkAccessManager(NetworkAccessManager::instance());
    };
    const auto onQueryDone = [storage](const NetworkQuery &query, DoneWith result) {
        storage->progressDialog->close();
        if (result == DoneWith::Success) {
            storage->packageData = query.reply()->readAll();
        } else {
            QMessageBox::warning(
                ICore::dialogParent(),
                Tr::tr("Download Error"),
                Tr::tr("Cannot download extension") + "\n\n" + storage->url.toString() + "\n\n"
                    + Tr::tr("Code: %1.").arg(query.reply()->error()));
        }
    };

    const auto onPluginInstallation = [storage]() {
        if (storage->packageData.isEmpty())
            return;
        const FilePath source = FilePath::fromUrl(storage->url);
        TempFileSaver saver(TemporaryDirectory::masterDirectoryPath()
                            + "/XXXXXX" + source.fileName());

        saver.write(storage->packageData);
        if (saver.finalize(ICore::dialogParent()))
            executePluginInstallWizard(saver.filePath());;
    };

    Group group{
        storage,
        NetworkQueryTask{onQuerySetup, onQueryDone},
        onGroupDone(onPluginInstallation),
    };

    m_dlTaskTreeRunner.start(group);
}

void EasyBoardWidget::fetchAndDisplayImage(const QUrl &url)
{
    using namespace Tasking;

    struct StorageStruct
    {
        QByteArray imageData;
        QUrl url;
    };
    Storage<StorageStruct> storage;

    const auto onFetchSetup = [url, storage](NetworkQuery &query) {
        storage->url = url;
        query.setRequest(QNetworkRequest(url));
        query.setNetworkAccessManager(NetworkAccessManager::instance());
        qCDebug(widgetLog).noquote() << "Sending image request:" << url.toDisplayString();
    };
    const auto onFetchDone = [storage](const NetworkQuery &query, DoneWith result) {
        qCDebug(widgetLog) << "Got image QNetworkReply:" << query.reply()->error();
        if (result == DoneWith::Success)
            storage->imageData = query.reply()->readAll();
    };

    const auto onShowImage = [storage, this]() {
        if (storage->imageData.isEmpty())
            return;
        m_imageDataBuffer.setData(storage->imageData);
        qCDebug(widgetLog).noquote() << "Image reponse size:"
                                     << QLocale::system().formattedDataSize(
                                            m_imageDataBuffer.size());
        if (!m_imageDataBuffer.open(QIODevice::ReadOnly))
            return;
        QImageReader reader(&m_imageDataBuffer);
        const bool animated = reader.supportsAnimation();
        if (animated) {
            m_image->setMovie(&m_imageMovie);
            m_imageMovie.start();
        } else {
            const QPixmap pixmap = QPixmap::fromImage(reader.read());
            m_image->setPixmap(pixmap);
        }
        qCDebug(widgetLog) << "Image dimensions:" << reader.size();
        qCDebug(widgetLog) << "Image is animated:" << animated;
    };

    Group group{
        storage,
        NetworkQueryTask{onFetchSetup, onFetchDone},
        onGroupDone(onShowImage),
    };

    m_imgTaskTreeRunner.start(group);
}

QWidget *createEasyBoardWidget()
{
    return new EasyBoardWidget;
}

} // EasyBoard::Internal

#include "easyboardwidget.moc"
