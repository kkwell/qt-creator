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
#include <qcombobox.h>

namespace EasyBoard::Internal {


NewBoardDialog::NewBoardDialog(QWidget *parent,Board *board) : QDialog(parent)
{
    setObjectName("EasyBoard.NewBoardDialog");
    resize(550, 160);
    setWindowTitle(Tr::tr("Board Manager"));

    m_board = board;

    m_nameLineEdit = new FancyLineEdit(this);
    m_nameLineEdit->setHistoryCompleter("DeviceName");

    m_tip = new QLabel();
    QPalette palette;
    palette.setColor(QPalette::WindowText, Qt::red);
    m_tip->setPalette(palette);

    m_hostNameLineEdit = new FancyLineEdit(this);
    m_hostNameLineEdit->setHistoryCompleter("HostName");

    m_typeBox = new QComboBox;
    // m_sshPortSpinBox = new QSpinBox(this);
    m_typeBox->setObjectName("languageBox");
    m_typeBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_typeBox->setMinimumContentsLength(20);
    m_typeBox->setToolTip("set this config to local or network(auto change throuth Auto Search)");
    m_typeBox->addItem(Tr::tr("TypeLocal"));
    m_typeBox->addItem(Tr::tr("TypeNetwork"));

    auto buttonBox = new QDialogButtonBox(this);
    buttonBox->setStandardButtons(QDialogButtonBox::Save|QDialogButtonBox::Cancel);

    // m_ok = new QPushButton(Tr::tr("&OK"));
    // m_cancel = new QPushButton(Tr::tr("&CANCEL"));

    using namespace Layouting;
    Form {
        st, br,
        Tr::tr("The name to identify this configuration:"), m_nameLineEdit, br,
        Tr::tr("The device's host name or IP address:"), m_hostNameLineEdit, st, br,
        Tr::tr("The ItemType:"), m_typeBox, st, br,br,
        m_tip,
        br,br,
        buttonBox
    }.attachTo(this);
    initializePage();

    auto acceptModel = [this] {
        if(isComplete()){
            // if(m_board->name!=getName())
            //     m_board->displayName = getName();
            // m_board->ip = getIp();
            // if(m_typeBox->currentIndex()==0)
            //     m_board->type = ItemTypeLocal;
            // else
            //     m_board->type = ItemTypeNetwork;
            QDialog::accept();
        }else{
            m_tip->setText(Tr::tr("Please Complete Set parameters."));
        }
    };

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::accepted, this, acceptModel);
}

QString NewBoardDialog::getIp()
{
    return m_hostNameLineEdit->text();
}

QString NewBoardDialog::getName()
{
    return m_nameLineEdit->text();
}

ItemType NewBoardDialog::getType()
{
    return m_typeBox->currentIndex()==0?ItemTypeLocal:ItemTypeNetwork;
}

void NewBoardDialog::initializePage() {
    QString displayName = m_board->displayName;
    QString name = m_board->name;

    m_nameLineEdit->setText(displayName.isEmpty()?name:displayName);
    m_hostNameLineEdit->setText(m_board->ip);

    if(name.isEmpty()){
        m_typeBox->setCurrentIndex(0);
        m_typeBox->setDisabled(true);
    }
    else{
        if(m_board->type==ItemTypeLocal)
            m_typeBox->setCurrentIndex(0);
        else
            m_typeBox->setCurrentIndex(1);
    }

    // m_sshPortSpinBox->setValue(22);
    // m_sshPortSpinBox->setRange(1, 65535);
}

bool NewBoardDialog::isComplete() const {
    return !m_nameLineEdit->text().trimmed().isEmpty()
    && !m_hostNameLineEdit->text().trimmed().isEmpty();
}

}
