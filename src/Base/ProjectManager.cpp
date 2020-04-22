/**
   @author Shin'ichiro Nakaoka
*/

#include "ProjectManager.h"
#include "RootItem.h"
#include "ItemManager.h"
#include "ViewManager.h"
#include "MessageView.h"
#include "ToolBar.h"
#include "Archive.h"
#include "ItemTreeArchiver.h"
#include "ExtensionManager.h"
#include "OptionManager.h"
#include "MenuManager.h"
#include "AppConfig.h"
#include "LazyCaller.h"
#include "FileDialog.h"
#include <cnoid/MainWindow>
#include <cnoid/YAMLReader>
#include <cnoid/YAMLWriter>
#include <cnoid/FileUtil>
#include <cnoid/ExecutablePath>
#include <QCoreApplication>
#include <QMessageBox>
#include <string>
#include <vector>
#include <fmt/format.h>
#include "gettext.h"

using namespace std;
using namespace cnoid;
namespace filesystem = cnoid::stdx::filesystem;
using fmt::format;

namespace {

ProjectManager* instance_ = nullptr;
int projectBeingLoadedCounter = 0;
Action* perspectiveCheck = nullptr;
MainWindow* mainWindow = nullptr;
MessageView* mv = nullptr;

Signal<void(int recursiveLevel)> sigProjectAboutToBeLoaded;
Signal<void(int recursiveLevel)> sigProjectLoaded;

}

namespace cnoid {

class ProjectManager::Impl
{
public:
    Impl(ProjectManager* self);
    Impl(ProjectManager* self, ExtensionManager* ext);

    void setCurrentProjectFile(const string& filename);
    void clearCurrentProjectFile();
        
    template <class TObject>
    bool restoreObjectStates(
        Archive* projectArchive, Archive* states, const vector<TObject*>& objects, const char* nameSuffix);
        
    ItemList<> loadProject(const string& filename, Item* parentItem, bool isInvokingApplication);

    template<class TObject>
    bool storeObjects(Archive& parentArchive, const char* key, vector<TObject*> objects);
        
    void saveProject(const string& filename, Item* item = nullptr);
    void overwriteCurrentProject();
        
    void onProjectOptionsParsed(boost::program_options::variables_map& v);
    void onInputFileOptionsParsed(std::vector<std::string>& inputFiles);
    void openDialogToLoadProject();
    void openDialogToSaveProject();
    std::string getSaveFilename(FileDialog& dialog);
    bool onSaveDialogAboutToFinished(FileDialog& dialog, int result);

    void onPerspectiveCheckToggled(bool on);
        
    void connectArchiver(
        const std::string& name,
        std::function<bool(Archive&)> storeFunction,
        std::function<void(const Archive&)> restoreFunction);

    ProjectManager* self;
    ItemTreeArchiver itemTreeArchiver;
    string currentProjectName;
    string currentProjectFile;
    string currentProjectDirectory;

    struct ArchiverInfo {
        std::function<bool(Archive&)> storeFunction;
        std::function<void(const Archive&)> restoreFunction;
    };
    typedef map<string, ArchiverInfo> ArchiverMap;
    typedef map<string, ArchiverMap> ArchiverMapMap;
    ArchiverMapMap archivers;

    MappingPtr config;
    MappingPtr managerConfig;
};

}


ProjectManager* ProjectManager::instance()
{
    return instance_;
}


void ProjectManager::initializeClass(ExtensionManager* ext)
{
    if(!instance_){
        instance_ = ext->manage(new ProjectManager(ext));
    }
}


ProjectManager::ProjectManager()
{
    impl = new Impl(this);
}


ProjectManager::Impl::Impl(ProjectManager* self)
    : self(self)
{
    
}


ProjectManager::ProjectManager(ExtensionManager* ext)
{
    impl = new Impl(this, ext);
}


// The constructor for the main instance
ProjectManager::Impl::Impl(ProjectManager* self, ExtensionManager* ext)
    : Impl(self)
{
    config = AppConfig::archive();
    managerConfig = config->openMapping("ProjectManager");
    
    MenuManager& mm = ext->menuManager();

    mm.setPath("/File");

    mm.addItem(_("Open Project"))
        ->sigTriggered().connect([&](){ openDialogToLoadProject(); });
    mm.addItem(_("Save Project"))
        ->sigTriggered().connect([&](){ overwriteCurrentProject(); });
    mm.addItem(_("Save Project As"))
        ->sigTriggered().connect([&](){ openDialogToSaveProject(); });

    mm.setPath(N_("Project File Options"));

    perspectiveCheck = mm.addCheckItem(_("Perspective"));
    perspectiveCheck->setChecked(managerConfig->get("store_perspective", true));
    perspectiveCheck->sigToggled().connect([&](bool on){ onPerspectiveCheckToggled(on); });

    mm.setPath("/File");
    mm.addSeparator();

    OptionManager& om = ext->optionManager();
    om.addOption("project", boost::program_options::value<vector<string>>(), "load a project file");
    om.sigInputFileOptionsParsed().connect(
        [&](std::vector<std::string>& inputFiles){ onInputFileOptionsParsed(inputFiles); });
    om.sigOptionsParsed().connect(
        [&](boost::program_options::variables_map& v){ onProjectOptionsParsed(v); });

    mainWindow = MainWindow::instance();
    mv = MessageView::instance();
}


ProjectManager::~ProjectManager()
{
    delete impl;
}


SignalProxy<void(int recursiveLevel)> ProjectManager::sigProjectAboutToBeLoaded()
{
    return ::sigProjectAboutToBeLoaded;
}


SignalProxy<void(int recursiveLevel)> ProjectManager::sigProjectLoaded()
{
    return ::sigProjectLoaded;
}


bool ProjectManager::isLoadingProject() const
{
    return projectBeingLoadedCounter > 0;
}


void ProjectManager::setCurrentProjectName(const std::string& name)
{
    impl->currentProjectName = name;
    mainWindow->setProjectTitle(name);

    if(!impl->currentProjectFile.empty()){
        auto path = filesystem::path(impl->currentProjectFile);
        impl->currentProjectFile = (path.parent_path() / (name + ".cnoid")).string();
    }
}
        

void ProjectManager::Impl::setCurrentProjectFile(const string& filename)
{
    auto name = getBasename(filename);
    currentProjectName = name;
    mainWindow->setProjectTitle(name);

    // filesystem::canonical can only be used with C++17
    auto path = getCompactPath(filesystem::absolute(filename));

    currentProjectFile = path.string();
    currentProjectDirectory = path.parent_path().string();
}


void ProjectManager::Impl::clearCurrentProjectFile()
{
    currentProjectFile.clear();
}


std::string ProjectManager::currentProjectFile() const
{
    return impl->currentProjectFile;
}


std::string ProjectManager::currentProjectDirectory() const
{
    return impl->currentProjectDirectory;
}


template <class TObject>
bool ProjectManager::Impl::restoreObjectStates
(Archive* projectArchive, Archive* states, const vector<TObject*>& objects, const char* nameSuffix)
{
    bool restored = false;
    for(size_t i=0; i < objects.size(); ++i){
        TObject* object = objects[i];
        const string name = object->objectName().toStdString();
        Archive* state = states->findSubArchive(name);
        if(state->isValid()){
            state->inheritSharedInfoFrom(*projectArchive);
            try {
                if(object->restoreState(*state)){
                    restored = true;
                }
            } catch(const ValueNode::Exception& ex){
                mv->putln(
                    format(_("The state of the \"{0}\" {1} was not completely restored.\n{2}"),
                    name, nameSuffix, ex.message()),
                    MessageView::WARNING);
            }
        }
    }
    return restored;
}


ItemList<> ProjectManager::loadProject(const std::string& filename, Item* parentItem)
{
    return impl->loadProject(filename, parentItem, false);
}


ItemList<> ProjectManager::Impl::loadProject(const std::string& filename, Item* parentItem, bool isInvokingApplication)
{
    ItemList<> loadedItems;
    
    ::sigProjectAboutToBeLoaded(projectBeingLoadedCounter);
    
    ++projectBeingLoadedCounter;
    
    bool loaded = false;
    YAMLReader reader;
    reader.setMappingClass<Archive>();

    try {
        mv->notify(format(_("Loading project file \"{}\" ..."), filename));
        if(!isInvokingApplication){
            mv->flush();
        }

        int numArchivedItems = 0;
        int numRestoredItems = 0;
        
        if(!reader.load(filename)){
            mv->put(reader.errorMessage() + "\n");

        } else if(reader.numDocuments() == 0){
            mv->putln(_("The project file is empty."), MessageView::WARNING);

        } else {
            Archive* archive = static_cast<Archive*>(reader.document()->toMapping());
            archive->initSharedInfo(filename);

            std::set<string> optionalPlugins;
            Listing& optionalPluginsNode = *archive->findListing("optionalPlugins");
            if(optionalPluginsNode.isValid()){
                for(int i=0; i < optionalPluginsNode.size(); ++i){
                    optionalPlugins.insert(optionalPluginsNode[i].toString());
                }
            }

            ViewManager::ViewStateInfo viewStateInfo;
            if(ViewManager::restoreViews(archive, "views", viewStateInfo)){
                loaded = true;
            }

            MainWindow* mainWindow = MainWindow::instance();
            if(isInvokingApplication){
                if(perspectiveCheck->isChecked()){
                    mainWindow->setInitialLayout(archive);
                }
                mainWindow->show();
                mv->flush();
                mainWindow->repaint();
            } else {
                if(perspectiveCheck->isChecked()){
                    mainWindow->restoreLayout(archive);
                }
            }

            ArchiverMapMap::iterator p;
            for(p = archivers.begin(); p != archivers.end(); ++p){
                const string& moduleName = p->first;
                Archive* moduleArchive = archive->findSubArchive(moduleName);
                if(moduleArchive->isValid()){
                    ArchiverMap::iterator q;
                    for(q = p->second.begin(); q != p->second.end(); ++q){
                        const string& objectName = q->first;
                        Archive* objArchive;
                        if(objectName.empty()){
                            objArchive = moduleArchive;
                        } else {
                            objArchive = moduleArchive->findSubArchive(objectName);
                        }
                        if(objArchive->isValid()){
                            ArchiverInfo& info = q->second;
                            objArchive->inheritSharedInfoFrom(*archive);
                            info.restoreFunction(*objArchive);
                            loaded = true;
                        }
                    }
                }
            }

            if(ViewManager::restoreViewStates(viewStateInfo)){
                loaded = true;
            } else {
                // load the old format (version 1.4 or earlier)
                Archive* viewStates = archive->findSubArchive("views");
                if(viewStates->isValid()){
                    if(restoreObjectStates(archive, viewStates, ViewManager::allViews(), "view")){
                        loaded = true;
                    }
                }
            }

            Archive* barStates = archive->findSubArchive("toolbars");
            if(barStates->isValid()){
                vector<ToolBar*> toolBars;
                mainWindow->getAllToolBars(toolBars);
                if(restoreObjectStates(archive, barStates, toolBars, "bar")){
                    loaded = true;
                }
            }

            bool isSubProject = parentItem != nullptr;
            if(!isSubProject){
                parentItem = RootItem::instance();
            }
                    
            itemTreeArchiver.reset();
            Archive* items = archive->findSubArchive("items");
            if(items->isValid()){
                items->inheritSharedInfoFrom(*archive);

                loadedItems = itemTreeArchiver.restore(items, parentItem, optionalPlugins);
                
                numArchivedItems = itemTreeArchiver.numArchivedItems();
                numRestoredItems = itemTreeArchiver.numRestoredItems();
                mv->putln(format(_("{0} / {1} item(s) have been loaded."), numRestoredItems, numArchivedItems));

                if(numRestoredItems < numArchivedItems){
                    mv->putln(
                        format(_("{} item(s) were not loaded."), (numArchivedItems - numRestoredItems)),
                        MessageView::WARNING);
                }
                
                if(numRestoredItems > 0){
                    loaded = true;
                }
            }

            if(loaded){
                if(!isSubProject){
                    if(archive->get("isNewProjectTemplate", false)){
                        clearCurrentProjectFile();
                    } else {
                        setCurrentProjectFile(filename);
                    }
                }

                mv->flush();
                
                archive->callPostProcesses();

                if(numRestoredItems == numArchivedItems){
                    mv->notify(format(_("Project \"{}\" has been completely loaded."), filename));
                } else {
                    mv->notify(format(_("Project \"{}\" has been partially loaded."), filename));
                }
            }
        }
    } catch (const ValueNode::Exception& ex){
        mv->put(ex.message());
    }

    if(!loaded){                
        mv->notify(
            format(_("Loading project \"{}\" failed. Any valid objects were not loaded."), filename),
            MessageView::ERROR);
        clearCurrentProjectFile();
    }

    --projectBeingLoadedCounter;

    ::sigProjectLoaded(projectBeingLoadedCounter);
    
    return loadedItems;
}


template<class TObject> bool ProjectManager::Impl::storeObjects
(Archive& parentArchive, const char* key, vector<TObject*> objects)
{
    bool result = true;
    
    if(!objects.empty()){
        MappingPtr archives = new Mapping();
        archives->setKeyQuoteStyle(DOUBLE_QUOTED);
        for(size_t i=0; i < objects.size(); ++i){
            TObject* object = objects[i];
            string name = object->objectName().toStdString();
            if(!name.empty()){
                ArchivePtr archive = new Archive();
                archive->inheritSharedInfoFrom(parentArchive);
                if(object->storeState(*archive) && !archive->empty()){
                    archives->insert(name, archive);
                }
            }
        }
        if(!archives->empty()){
            parentArchive.insert(key, archives);
            result = true;
        }
    }

    return result;
}


void ProjectManager::saveProject(const string& filename, Item* item)
{
    impl->saveProject(filename, item);
}


void ProjectManager::Impl::saveProject(const string& filename, Item* item)
{
    YAMLWriter writer(filename);
    if(!writer.isFileOpen()){
        mv->put(
            format(_("Can't open file \"{}\" for writing.\n"), filename),
            MessageView::ERROR);
        return;
    }

    bool isSubProject;
    if(item){
        isSubProject = true;
    } else {
        item = RootItem::mainInstance();
        isSubProject = false;
    }
    
    mv->putln();
    if(isSubProject){
        mv->notify(format(_("Saving sub project {0} as \"{1}\" ..."), item->name(), filename));
    } else {
        mv->notify(format(_("Saving the project as \"{}\" ..."), filename));
    }
    mv->flush();
    
    itemTreeArchiver.reset();
    
    ArchivePtr archive = new Archive();
    archive->initSharedInfo(filename);

    ArchivePtr itemArchive = itemTreeArchiver.store(archive, item);

    if(itemArchive){
        archive->insert("items", itemArchive);
    }

    bool stored = ViewManager::storeViewStates(archive, "views");

    vector<ToolBar*> toolBars;
    mainWindow->getAllToolBars(toolBars);
    stored |= storeObjects(*archive, "toolbars", toolBars);

    ArchiverMapMap::iterator p;
    for(p = archivers.begin(); p != archivers.end(); ++p){
        ArchivePtr moduleArchive = new Archive();
        moduleArchive->setKeyQuoteStyle(DOUBLE_QUOTED);
        ArchiverMap::iterator q;
        for(q = p->second.begin(); q != p->second.end(); ++q){
            const string& objectName = q->first;
            ArchivePtr objArchive;
            if(objectName.empty()){
                objArchive = moduleArchive;
            } else {
                objArchive = new Archive();
            }
            objArchive->inheritSharedInfoFrom(*archive);
            ArchiverInfo& info = q->second;
            if(info.storeFunction(*objArchive)){
                if(!objectName.empty()){
                    moduleArchive->insert(objectName, objArchive);
                }
            }
        }
        if(!moduleArchive->empty()){
            const string& moduleName = p->first;
            archive->insert(moduleName, moduleArchive);
            stored = true;
        }
    }

    if(perspectiveCheck->isChecked() && !isSubProject){
        mainWindow->storeLayout(archive);
        stored = true;
    }

    if(stored){
        writer.setKeyOrderPreservationMode(true);
        writer.putNode(archive);
        mv->notify(_("Saving the project file has been finished."));
        if(!isSubProject){
            setCurrentProjectFile(filename);
        }
    } else {
        mv->notify(_("Saving the project file failed."), MessageView::ERROR);
        clearCurrentProjectFile();
    }
}


void ProjectManager::overwriteCurrentProject()
{
    impl->overwriteCurrentProject();
}


void ProjectManager::Impl::overwriteCurrentProject()
{
    if(currentProjectFile.empty()){
        openDialogToSaveProject();
    } else {
        saveProject(currentProjectFile);
    } 
}

    
void ProjectManager::Impl::onProjectOptionsParsed(boost::program_options::variables_map& v)
{
    if(v.count("project")){
        vector<string> projectFileNames = v["project"].as<vector<string>>();
        for(size_t i=0; i < projectFileNames.size(); ++i){
            loadProject(toActualPathName(projectFileNames[i]), nullptr, true);
        }
    }
}


void ProjectManager::Impl::onInputFileOptionsParsed(std::vector<std::string>& inputFiles)
{
    auto iter = inputFiles.begin();
    while(iter != inputFiles.end()){
        if(getExtension(*iter) == "cnoid"){
            loadProject(toActualPathName(*iter), nullptr, true);
            iter = inputFiles.erase(iter);
        } else {
            ++iter;
        }
    }
}


void ProjectManager::Impl::openDialogToLoadProject()
{
    FileDialog dialog(MainWindow::instance());
    dialog.setWindowTitle(_("Open a Choreonoid project file"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setViewMode(QFileDialog::List);
    dialog.setLabelText(QFileDialog::Accept, _("Open"));
    dialog.setLabelText(QFileDialog::Reject, _("Cancel"));

    QStringList filters;
    filters << _("Project files (*.cnoid)");
    filters << _("Any files (*)");
    dialog.setNameFilters(filters);

    dialog.updatePresetDirectories();
    
    if(dialog.exec()){
        string filename = getNativePathString(filesystem::path(dialog.selectedFiles().front().toStdString()));
        loadProject(filename, nullptr, false);
    }
}


void ProjectManager::Impl::openDialogToSaveProject()
{
    FileDialog dialog(MainWindow::instance());
    dialog.setWindowTitle(_("Save a choreonoid project file"));
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setViewMode(QFileDialog::List);
    dialog.setLabelText(QFileDialog::Accept, _("Save"));
    dialog.setLabelText(QFileDialog::Reject, _("Cancel"));
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    
    QStringList filters;
    filters << _("Project files (*.cnoid)");
    filters << _("Any files (*)");
    dialog.setNameFilters(filters);

    dialog.updatePresetDirectories();

    if(!dialog.selectFilePath(currentProjectFile)){
        dialog.selectFile(currentProjectName);
    }

    dialog.sigAboutToFinished().connect(
        [&](int result){ return onSaveDialogAboutToFinished(dialog, result); });

    if(dialog.exec() == QDialog::Accepted){
        saveProject(getSaveFilename(dialog));
    }
    
}


std::string ProjectManager::Impl::getSaveFilename(FileDialog& dialog)
{
    std::string filename;
    auto filenames = dialog.selectedFiles();
    if(!filenames.isEmpty()){
        filename = filenames.front().toStdString();
        filesystem::path path(filename);
        //string filename = getNativePathString(path);
        string ext = path.extension().string();
        if(ext != ".cnoid"){
            filename += ".cnoid";
        }
    }
    return filename;
}


bool ProjectManager::Impl::onSaveDialogAboutToFinished(FileDialog& dialog, int result)
{
    bool finished = true;
    if(result == QFileDialog::Accepted){
        auto filename = getSaveFilename(dialog);
        if(filesystem::exists(filename)){
            dialog.fileDialog()->show();
            QString file(filesystem::path(filename).filename().native().c_str());
            QString message(QString(_("%1 already exists. Do you want to replace it? ")).arg(file));
            auto button =
                QMessageBox::warning(&dialog, dialog.windowTitle(), message, QMessageBox::Ok | QMessageBox::Cancel);
            if(button == QMessageBox::Cancel){
                finished = false;
            }
        }
    }
    return finished;
}


void ProjectManager::Impl::onPerspectiveCheckToggled(bool on)
{
    managerConfig->write("store_perspective", perspectiveCheck->isChecked());
}


void ProjectManager::setArchiver(
    const std::string& moduleName,
    const std::string& name,
    std::function<bool(Archive&)> storeFunction,
    std::function<void(const Archive&)> restoreFunction)
{
    Impl::ArchiverInfo& info = impl->archivers[moduleName][name];
    info.storeFunction = storeFunction;
    info.restoreFunction = restoreFunction;
}


void ProjectManager::resetArchivers(const std::string& moduleName)
{
    impl->archivers.erase(moduleName);
}
