// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "easyboardwidget.h"
#include "easyboardtr.h"
#include "easyboardbrowser.h"
#include "easyboardmodel.h"
#include "boardsWidget/t113s.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/coreicons.h>
#include <coreplugin/icore.h>
#include <coreplugin/iwelcomepage.h>
#include <coreplugin/plugininstallwizard.h>
#include <coreplugin/welcomepagehelper.h>
#include <coreplugin/minisplitter.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <extensionsystem/pluginview.h>

#include <QtTaskTree/QNetworkReplyWrapper>
#include <QtTaskTree/QSingleTaskTreeRunner>

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
#include <utils/styledbar.h>
// #include <utils/fancymainwindow.h>

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
#include <QDockWidget>
#include <QSplitter>

using namespace Core;
using namespace Utils;
using namespace Utils::StyleHelper;
using namespace Utils::StyleHelper::SpacingTokens;
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
    widget->setContentsMargins(PaddingHXxl, PaddingVXxl, PaddingHXxl, PaddingVXxl);
    widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);

    auto scrollArea = new QScrollArea;
    scrollArea->setWidget(widget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFrameStyle(QFrame::NoFrame);

    return scrollArea;
};

class TopArea : public QWidget
{
    Q_OBJECT

public:
    TopArea(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

        constexpr TextFormat welcomeTF {Theme::Token_Text_Default, StyleHelper::UiElementH2};

        auto ideIconLabel = new QLabel;
        {
            const QPixmap logo = Core::Icons::QTCREATORLOGO_BIG.pixmap();
            const int size = logo.width();
            const QRect cropR = size == 128 ? QRect(9, 22, 110, 84) : QRect(17, 45, 222, 166);
            const QPixmap croppedLogo = logo.copy(cropR);
            const int lineHeight = welcomeTF.lineHeight();
            const QPixmap scaledCroppedLogo =
                croppedLogo.scaledToHeight((lineHeight - 12) * croppedLogo.devicePixelRatioF(),
                                           Qt::SmoothTransformation);
            ideIconLabel->setPixmap(scaledCroppedLogo);
            ideIconLabel->setFixedHeight(lineHeight);
        }

        auto welcomeLabel = new QLabel(Tr::tr("%1 Configs")
                                           .arg(QGuiApplication::applicationDisplayName()));
        {
            welcomeLabel->setFont(welcomeTF.font());
            QPalette pal = palette();
            pal.setColor(QPalette::WindowText, welcomeTF.color());
            welcomeLabel->setPalette(pal);
        }

        using namespace Layouting;

        Column {
            Row {
                ideIconLabel,
                welcomeLabel,
                st,
                spacing(GapHXxl),
                customMargins(PaddingHXxl, PaddingVXl, PaddingHXxl, PaddingVXl),
            },
            createRule(Qt::Horizontal),
            noMargin, spacing(0),
        }.attachTo(this);
    }
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
        // m_divider = new QLabel;
        // m_divider->setFixedSize(1, dividerH);
        // WelcomePageHelpers::setBackgroundColor(m_divider, dlTF.themeColor);

        m_details = tfLabel(detailsTF,false);

        m_name = tfLabel(detailsTF);
        m_displayName = tfLabel(detailsTF);
        m_ip = tfLabel(detailsTF);
        m_version = tfLabel(detailsTF);
        m_date = tfLabel(detailsTF);
        m_type = tfLabel(detailsTF);
        m_isDefault = tfLabel(detailsTF);
        m_online = tfLabel(detailsTF);

        // m_details = tfLabel(contentTF, false);
        m_details->setWordWrap(true);

        using namespace Layouting;

        auto contain_details = new QWidget;

        Row{
            Space(GapHXxl),
            m_details,
        }.attachTo(contain_details);

        auto m_pStackedWidget = new QStackedWidget;
        // QWidget *scrollWidget = new QWidget;
        QScrollArea *scrollArea = new QScrollArea;

        // scrollArea->setStyleSheet("QWidget { background-color: #bb229d; }");
        // scrollWidget->setStyleSheet("QWidget { background-color: #bb229d; }");

        scrollArea->setWidget(contain_details);  // 将内部部件设置为滚动区域的widget
        scrollArea->setWidgetResizable(true); // 允许滚动区域的widget根据内容调整大小
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 水平滚动条始终关闭
        scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);  // 垂直滚动条按需显示
        scrollArea->setFrameStyle(QFrame::NoFrame);

        // Row {
        //     scrollArea,
        // }.attachTo(scrollWidget);

        // auto sc = toScrollableColumn(scrollWidget);
        m_pStackedWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        m_pStackedWidget->addWidget(scrollArea);
        // m_pStackedWidget->addWidget(scrollWidget);
        m_pStackedWidget->setMinimumWidth(2);
        // m_pStackedWidget->setFixedSize(imgBgSize);
        // scrollWidget->setFixedSize(imgBgSize);

        // m_pStackedWidget->setCurrentIndex(0);

        auto detileWidget = new QWidget;

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
        }.attachTo(detileWidget);

        detileWidget->setMinimumWidth(180);

        auto paneSplitter = new MiniSplitter(Qt::Horizontal);
        paneSplitter->insertWidget(0, detileWidget);
        paneSplitter->insertWidget(1, m_pStackedWidget);
        paneSplitter->setCollapsible(0, false);

        // paneSplitter->setStretchFactor(1, 1);

        Row {
            m_icon,
            noMargin,
            paneSplitter,
            noMargin, spacing(GapHL),
        }.attachTo(this);

        setMaximumHeight(200);

        // m_column->setMinimumWidth(180);
        // m_column->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
        setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Maximum);

        // FancyMainWindow * test = new Utils::FancyMainWindow;
        // test->addDockForWidget(detileWidget,true);
        // test->addDockForWidget(paneSplitter,true);

        // setFocusProxy(m_easyboardBrowser);

        // m_dlCountItems->setVisible(false);

        // connect(installButton, &QAbstractButton::pressed,
        //         this, &HeadingWidget::pluginInstallationRequested);
        // connect(m_vendor, &QAbstractButton::pressed, this, [this]() {
        //     emit vendorClicked(m_currentVendor);
        // });


        // Row {
        //     m_icon,
        //     noMargin,
        //     test,
        //     noMargin, spacing(SpacingTokens::ExPaddingGapL),
        // }.attachTo(this);

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
        if(name.contains("t113",Qt::CaseInsensitive)){//color:#909090;center
            const QString placeText = Tr::tr("<html><body style=\"color:#909090,font-size:12px\">"
                                                   "<div align='left'>"
                                                   "<div style=\"font-size:14px\">BingPi-M2开发板</div>"
                                                   "<table><tr><td>"
                                                   "<hr/>"
                                                   "<div style=\"margin-top: 5px\">&bull; CPU：全志T113-S3,双核Cortex-A7,最高1.2GHz</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; 内存：集成128M DDR3</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; 存储：128MB Nand Flash</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; USB：一路USB OTG,3路USB HOST</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; 网络：百兆以太网&WIFI</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; 通信：CAN、RS232、RS485</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; 触摸：电容、电阻触摸</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; 其它：USB串口控制台、按键、LED</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; 音频：输入、输出</div>"
                                                   "<div style=\"margin-top: 5px\">&bull; 显示：最高1920*1200（支持MIPI）</div>"
                                                   "<div style=\"margin-left: 1em\">- 40p 4.3寸、5寸屏</div>"
                                                   "<div style=\"margin-left: 1em\">- 50p 7寸屏</div>"
                                                   "</td></tr></table>"
                                                   "</div>"
                                                   "</body></html>");

            m_details->setText(placeText);
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
    // QLabel *m_divider;
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

class EasyBoardWidget final : public Core::ResizeSignallingWidget
{
public:
    EasyBoardWidget();

private:
    void updateView(const QModelIndex &current);
    void fetchAndInstallPlugin(const QUrl &url);
    void fetchAndDisplayImage(const QUrl &url);

    TopArea *m_topArea;
    QString m_currentItemName;
    EasyBoardBrowser *m_easyboardBrowser;
    QWidget *m_descriptionColumns;
    HeadingWidget *m_headingWidget;
    QStackedWidget *m_stackWidget;
    QWidget *m_primaryContent;
    t113s *m_primary;
    // QWidget *m_secondaryContent;
    QLabel *m_description;
    QLabel *m_linksTitle;
    QLabel *m_links;
    // QLabel *m_imageTitle;
    // QLabel *m_image;
    QBuffer m_imageDataBuffer;
    QMovie m_imageMovie;
    QLabel *m_compatVersionTitle;
    QLabel *m_compatVersion;
    QLabel *m_platformsTitle;
    QLabel *m_platforms;
    QLabel *m_dependenciesTitle;
    QLabel *m_dependencies;
    QLabel *m_packExtensionsTitle;
    QLabel *m_packExtensions;
    QtTaskTree::QSingleTaskTreeRunner m_dlTaskTreeRunner;
    QtTaskTree::QSingleTaskTreeRunner m_imgTaskTreeRunner;
};

EasyBoardWidget::EasyBoardWidget()
{
    m_easyboardBrowser = new EasyBoardBrowser;
    m_descriptionColumns = new QWidget;//QWidget;
    // m_secondaryDescriptionWidget = new CollapsingWidget;

    m_headingWidget = new HeadingWidget;
    m_description = tfLabel(contentTF, false);
    m_description->setWordWrap(true);
    m_linksTitle = sectionTitle(h6CapitalTF, Tr::tr("More information"));
    m_links = tfLabel(contentTF, false);
    m_links->setOpenExternalLinks(true);
    // m_imageTitle = sectionTitle(h6CapitalTF, {});
    // m_image = new QLabel;
    m_imageMovie.setDevice(&m_imageDataBuffer);

    const QString placeholderText = Tr::tr("<html><body style=\"color:#909090; font-size:14px\">"
                                           "<div align='center'>"
                                           "<div style=\"font-size:20px\">Select a board</div>"
                                           "<table><tr><td>"
                                           "<hr/>"
                                           "<div style=\"margin-top: 5px\">&bull; Config > Set Peripheral Functions</div>"
                                           "<div style=\"margin-top: 5px\">&bull; Config > View Peripheral Status</div>"
                                           "<div style=\"margin-top: 5px\">&bull; Board > Manage Devices</div>"
                                           "<div style=\"margin-left: 1em\">- view development board information</div>"
                                           "<div style=\"margin-left: 1em\">- check the status of the development board</div>"
                                           "<div style=\"margin-left: 1em\">- select one of the board and set relevant parameters</div>"
                                           "<div style=\"margin-top: 5px\">&bull; Some other functions here</div>"
                                           "</td></tr></table>"
                                           "</div>"
                                           "</body></html>");

    m_description->setText(placeholderText);

    using namespace Layouting;

    m_primary = new t113s;
    // primary->setStyleSheet("QWidget { background-color: #bb229d; }"); // 设置背景颜色为红色
    m_primaryContent = new QWidget;
    const auto spL = spacing(GapVXl);
    Column {
        st,
        m_description,
        // Column { m_linksTitle, m_links, spL },
        // Column { m_imageTitle, m_image, spL },
        st,
        noMargin, spacing(GapVXxl),
    }.attachTo(m_primaryContent);
    // m_primaryContent = toScrollableColumn(temp);

    // m_tagsTitle = sectionTitle(h6TF, Tr::tr("Tags"));
    // m_tags = new TagList;
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
                    customMargins(PaddingHXxl, PaddingVXxl, PaddingHXxl, PaddingVXxl),
                },
                m_primary,//m_primaryContent,
            },
        },
        noMargin, spacing(0),
    }.attachTo(m_descriptionColumns);

    m_stackWidget = new QStackedWidget;
    m_stackWidget->addWidget(m_primaryContent);
    m_stackWidget->addWidget(m_descriptionColumns);

    // auto m_left = new QDockWidget;
    // this->setWidget();

    // auto paneSplitter = new MiniSplitter(Qt::Horizontal);
    // paneSplitter->insertWidget(0, m_easyboardBrowser);
    // paneSplitter->insertWidget(1, m_stackWidget);

    // m_easyboardBrowser->setMinimumWidth(0);
    // Row {
    //     Space(SpacingTokens::ExVPaddingGapXl),
    //     m_easyboardBrowser,
    //     WelcomePageHelpers::createRule(Qt::Vertical),
    //     m_stackWidget,//descriptionColumns,
    //     noMargin, spacing(0),
    // }.attachTo(this);

    m_topArea = new TopArea;

    Column {
        // new StyledBar,
        m_topArea,
        Row {
            Space(GapHXxl),
            m_easyboardBrowser,
            WelcomePageHelpers::createRule(Qt::Vertical),
            m_stackWidget,//descriptionColumns,
            noMargin, spacing(0),
        },
        noMargin,
        spacing(0),
    }.attachTo(this);
    // SplitterWidget *p = new SplitterWidget;



    // QHBoxLayout layout;
    // layout.addWidget(p);
    // setLayout(&layout);

    WelcomePageHelpers::setBackgroundColor(this, Theme::Token_Background_Default);

    // const int intendedBrowserColumnWidth = size.width() - 580;
    m_easyboardBrowser->adjustToWidth(280);

    connect(m_easyboardBrowser, &EasyBoardBrowser::itemChanged,
            this,&EasyBoardWidget::updateView);
    connect(m_easyboardBrowser, &EasyBoardBrowser::itemSelected,
            this, &EasyBoardWidget::updateView);
    // connect(this, &ResizeSignallingWidget::resized, this, [this](const QSize &size) {
        // const bool secondaryDescriptionVisible = size.width() > 970;
        // const int secondaryDescriptionWidth = secondaryDescriptionVisible ? 264 : 0;
        // m_descriptionColumns->setWidth(size.width()-m_easyboardBrowser->size().width());
    // });
    // connect(m_headingWidget, &HeadingWidget::pluginInstallationRequested, this, [this](){
    //     fetchAndInstallPlugin(QUrl::fromUserInput(m_currentItemPlugins.constFirst().second));
    // });
    // connect(m_tags, &TagList::tagSelected, m_easyboardBrowser, &EasyBoardBrowser::setFilter);
    connect(m_headingWidget, &HeadingWidget::vendorClicked,
            m_easyboardBrowser, &EasyBoardBrowser::setFilter);


    connect(this, &ResizeSignallingWidget::resized,
            this, [this](const QSize &size, const QSize &) {
                // const QSize sideAreaS = m_sideArea->size();
                const QSize topAreaS = m_topArea->size();
                const QSize mainWindowS = ICore::mainWindow()->size();

                // const bool showSideArea = sideAreaS.width() < size.width() / 4;
                const bool showTopArea = topAreaS.height() < mainWindowS.height() / 8.85;
                const bool showLinks = true;

                // m_sideArea->m_links->setVisible(showLinks);
                // m_sideArea->setVisible(showSideArea);
                m_topArea->setVisible(showTopArea);
            });

    updateView({});
}

void EasyBoardWidget::updateView(const QModelIndex &current)
{
    m_headingWidget->update(current);
    m_primary->update(current);
    const bool showContent = current.isValid();

    // m_primaryContent->setVisible(showContent);
    // m_headingWidget->setVisible(showContent);

    if (!showContent)
        return;
    if(current.data(EasyBoardModel::RoleName).toString().contains("T113",Qt::CaseInsensitive)){
        m_stackWidget->setCurrentIndex(1);
    }
    else{
        m_stackWidget->setCurrentIndex(0);
    }


    m_currentItemName = current.data().toString();
    // const bool isPack = current.data(RoleItemType) == ItemTypePack;
    // m_pluginStatus->setPluginName(isPack ? QString() : m_currentItemName);
    // m_currentItemPlugins = current.data(RolePlugins).value<PluginsData>();


}

void EasyBoardWidget::fetchAndInstallPlugin(const QUrl &url)
{
    using namespace QtTaskTree;

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

    const auto onQuerySetup = [url, storage](QNetworkReplyWrapper &query) {
        storage->url = url;
        query.setRequest(QNetworkRequest(url));
        query.setNetworkAccessManager(NetworkAccessManager::instance());
    };
    const auto onQueryDone = [storage](const QNetworkReplyWrapper &query, DoneWith result) {
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
        if (const Result<> result = saver.finalize())
            executePluginInstallWizard(saver.filePath());
        else
            FileUtils::showError(result.error());
    };

    Group group{
        storage,
        QNetworkReplyWrapperTask(onQuerySetup, onQueryDone),
        onGroupDone(onPluginInstallation),
    };

    m_dlTaskTreeRunner.start(group);
}

void EasyBoardWidget::fetchAndDisplayImage(const QUrl &url)
{
    using namespace QtTaskTree;

    struct StorageStruct
    {
        QByteArray imageData;
        QUrl url;
    };
    Storage<StorageStruct> storage;

    const auto onFetchSetup = [url, storage](QNetworkReplyWrapper &query) {
        storage->url = url;
        query.setRequest(QNetworkRequest(url));
        query.setNetworkAccessManager(NetworkAccessManager::instance());
        qCDebug(widgetLog).noquote() << "Sending image request:" << url.toDisplayString();
    };
    const auto onFetchDone = [storage](const QNetworkReplyWrapper &query, DoneWith result) {
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
        // if (animated) {
        //     m_image->setMovie(&m_imageMovie);
        //     m_imageMovie.start();
        // } else {
        //     const QPixmap pixmap = QPixmap::fromImage(reader.read());
        //     m_image->setPixmap(pixmap);
        // }
        qCDebug(widgetLog) << "Image dimensions:" << reader.size();
        qCDebug(widgetLog) << "Image is animated:" << animated;
    };

    Group group{
        storage,
        QNetworkReplyWrapperTask(onFetchSetup, onFetchDone),
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
