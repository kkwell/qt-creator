// Copyright (C) 2026 Kvell

#include "ethercatprojectwizard.h"

#include "ethercatprojectconstants.h"
#include "ethercatprojectformat.h"
#include "ethercatprojecttr.h"

#include <coreplugin/basefilewizard.h>
#include <coreplugin/iwizardfactory.h>

#include <projectexplorer/customwizard/customwizard.h>
#include <projectexplorer/projectexplorericons.h>

#include <utils/filewizardpage.h>

#include <QCoreApplication>
#include <QGuiApplication>

namespace EtherCAT::Project::Internal {

class EtherCATProjectWizard final : public Core::BaseFileWizard
{
    Q_OBJECT

public:
    explicit EtherCATProjectWizard(const Core::BaseFileWizardFactory *factory)
        : Core::BaseFileWizard(factory, {})
        , m_projectPage(new Utils::FileWizardPage)
    {
        setWindowTitle(Tr::tr("New EtherCAT Project"));
        m_projectPage->setTitle(Tr::tr("Project Name and Location"));
        m_projectPage->setFileNameLabel(Tr::tr("Project name:"));
        m_projectPage->setPathLabel(Tr::tr("Location:"));
        addPage(m_projectPage);
    }

    void setDefaultPath(const Utils::FilePath &path) { m_projectPage->setFilePath(path); }
    Utils::FilePath projectPath() const { return m_projectPage->filePath(); }
    QString projectName() const { return m_projectPage->fileName().trimmed(); }

private:
    Utils::FileWizardPage *m_projectPage;
};

class EtherCATProjectWizardFactory final : public Core::BaseFileWizardFactory
{
public:
    EtherCATProjectWizardFactory()
    {
        setSupportedProjectTypes({Constants::PROJECT_ID});
        setIcon(ProjectExplorer::Icons::WIZARD_IMPORT_AS_PROJECT.icon());
        setDisplayName(Tr::tr("EtherCAT Engineering Project"));
        setId(Constants::WIZARD_ID);
        setDescription(Tr::tr("Creates an offline EtherCAT engineering project for %1.")
                           .arg(QGuiApplication::applicationDisplayName()));
        setCategory(Constants::WIZARD_CATEGORY);
        setDisplayCategory(Tr::tr("EtherCAT"));
        setFlags(Core::IWizardFactory::PlatformIndependent);
    }

private:
    Core::BaseFileWizard *create(const Core::WizardDialogParameters &parameters) const final
    {
        auto *wizard = new EtherCATProjectWizard(this);
        wizard->setDefaultPath(parameters.defaultPath());
        for (QWizardPage *page : wizard->extensionPages())
            wizard->addPage(page);
        return wizard;
    }

    Utils::Result<Core::GeneratedFiles> generateFiles(const QWizard *wizard) const final
    {
        const auto *projectWizard = qobject_cast<const EtherCATProjectWizard *>(wizard);
        if (!projectWizard || projectWizard->projectName().isEmpty())
            return Utils::ResultError(Tr::tr("Project name cannot be empty."));

        const Utils::FilePath projectFile = projectWizard->projectPath().pathAppended(
            projectWizard->projectName() + '.' + Constants::PROJECT_SUFFIX);
        const QString createdBy
            = QString("%1 %2")
                  .arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion())
                  .trimmed();
        const Data::ProjectSnapshot snapshot
            = createProjectSnapshot(projectWizard->projectName(), createdBy);

        Core::GeneratedFile file(projectFile);
        file.setBinaryContents(serializeProject(snapshot));
        file.setAttributes(Core::GeneratedFile::OpenProjectAttribute);
        return Core::GeneratedFiles{file};
    }

    Utils::Result<> postGenerateFiles(const QWizard *, const Core::GeneratedFiles &files) const final
    {
        return ProjectExplorer::CustomProjectWizard::postGenerateOpen(files);
    }
};

void setupEtherCATProjectWizard()
{
    Core::IWizardFactory::registerFactoryCreator([] { return new EtherCATProjectWizardFactory; });
}

} // namespace EtherCAT::Project::Internal

#include "ethercatprojectwizard.moc"
