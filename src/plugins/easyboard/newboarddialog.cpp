#include "newboarddialog.h"
#include "easyboardtr.h"

#include <utils/algorithm.h>
#include <utils/layoutbuilder.h>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QValidator>

namespace EasyBoard::Internal {


NewBoardDialog::NewBoardDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("EasyBoard.NewBoardDialog");
    resize(550, 400);
    setWindowTitle(Tr::tr("Board Manager"));

    // auto sessionView = new SessionView(this);
    // sessionView->setObjectName("sessionView");
    // sessionView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // sessionView->setActivationMode(Utils::DoubleClickActivation);

    auto createNewButton = new QPushButton(Tr::tr("&New..."));
    createNewButton->setObjectName("btCreateNew");

    m_openButton = new QPushButton(Tr::tr("&Open"));
    m_openButton->setObjectName("btOpen");
    m_renameButton = new QPushButton(Tr::tr("&Rename..."));
    m_cloneButton = new QPushButton(Tr::tr("C&lone..."));
    m_deleteButton = new QPushButton(Tr::tr("&Delete..."));

    m_autoLoadCheckBox = new QCheckBox(Tr::tr("Restore last session on startup"));

    auto buttonBox = new QDialogButtonBox(this);
    buttonBox->setStandardButtons(QDialogButtonBox::Close);

    m_openButton->setDefault(true);

    auto whatsASessionLabel = new QLabel(QString("<a href=\"qthelp://org.qt-project.qtcreator/doc/"
                                                 "creator-project-managing-sessions.html\">%1</a>")
                                             .arg(Tr::tr("What is a Session?")));
    whatsASessionLabel->setOpenExternalLinks(true);

    using namespace Layouting;

    Column {
           Row {
               // sessionView,
               Column {
                   createNewButton,
                   m_openButton,
                   m_renameButton,
                   m_cloneButton,
                   m_deleteButton,
                   st
               }
           },
           m_autoLoadCheckBox,
           hr,
           Row { whatsASessionLabel, buttonBox },
           }.attachTo(this);

    // connect(createNewButton, &QAbstractButton::clicked,
    //         sessionView, &SessionView::createNewSession);
    // connect(m_openButton, &QAbstractButton::clicked,
    //         sessionView, &SessionView::switchToCurrentSession);
    // connect(m_renameButton, &QAbstractButton::clicked,
    //         sessionView, &SessionView::renameCurrentSession);
    // connect(m_cloneButton, &QAbstractButton::clicked,
    //         sessionView, &SessionView::cloneCurrentSession);
    // connect(m_deleteButton, &QAbstractButton::clicked,
    //         sessionView, &SessionView::deleteSelectedSessions);
    // connect(sessionView, &SessionView::sessionActivated,
    //         sessionView, &SessionView::switchToCurrentSession);

    // connect(sessionView, &SessionView::sessionsSelected,
    //         this, &SessionDialog::updateActions);
    // connect(sessionView, &SessionView::sessionSwitched,
    //         this, &QDialog::reject);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
}


}
