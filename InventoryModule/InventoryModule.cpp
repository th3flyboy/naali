// For conditions of distribution and use, see copyright notice in license.txt

/**
 *  @file InventoryModule.cpp
 *  @brief Inventory module.
 */

#include "StableHeaders.h"
#include "InventoryModule.h"
#include "RexLogicModule.h"
#include "InventoryWindow.h"
#include "NetworkEvents.h"
#include "Inventory/InventoryEvents.h"
#include "AssetUploader.h"
#include "QtUtils.h"

namespace Inventory
{

InventoryModule::InventoryModule() :
    ModuleInterfaceImpl(Foundation::Module::MT_Inventory),
    networkStateEventCategory_(0), frameworkEventCategory_(0), inventoryWindow_(0)
{
}

// virtual
InventoryModule::~InventoryModule()
{
}

void InventoryModule::Load()
{
    LogInfo("System " + Name() + " loaded.");
}

void InventoryModule::Unload()
{
    LogInfo("System " + Name() + " unloaded.");
}

void InventoryModule::Initialize()
{
    eventManager_ = framework_->GetEventManager();
    inventoryEventCategory_ = eventManager_->RegisterEventCategory("Inventory");

    rexLogic_ = dynamic_cast<RexLogic::RexLogicModule *>(framework_->GetModuleManager()->GetModule(
        Foundation::Module::MT_WorldLogic).lock().get());
  
    boost::shared_ptr<Console::CommandService> console = framework_->GetService<Console::CommandService>
        (Foundation::Service::ST_ConsoleCommand).lock();
    if (console)
    {
        console->RegisterCommand(Console::CreateCommand("Upload",
            "Upload an asset. Usage: Upload(AssetType, Name, Description)",
            Console::Bind(this, &Inventory::InventoryModule::UploadAsset)));

        console->RegisterCommand(Console::CreateCommand("MultiUpload", "Upload multiple assets.",
            Console::Bind(this, &Inventory::InventoryModule::UploadMultipleAssets)));
    }

    assetUploader_ = AssetUploaderPtr(new AssetUploader(framework_, rexLogic_));

    inventoryWindow_ = new InventoryWindow(framework_, rexLogic_);

    //!REMOVE TEST CODE FOR WEBDAV
    //std::string idurl("http://192.168.0.199:8010/test");
    //std::string hosturl("192.168.0.199:8002");
    //inventoryWindow_->InitWebDavInventoryTreeModel(idurl, hosturl);

    LogInfo("System " + Name() + " initialized.");
}

void InventoryModule::PostInitialize()
{
    frameworkEventCategory_ = eventManager_->QueryEventCategory("Framework");
    if (frameworkEventCategory_ == 0)
        LogError("Failed to query \"Framework\" event category");

}

void InventoryModule::Uninitialize()
{
    assetUploader_.reset();
    SAFE_DELETE(inventoryWindow_);
    LogInfo("System " + Name() + " uninitialized.");
}

void InventoryModule::SubscribeToNetworkEvents(boost::weak_ptr<ProtocolUtilities::ProtocolModuleInterface> currentProtocolModule)
{
    networkStateEventCategory_ = eventManager_->QueryEventCategory("NetworkState");
    if (networkStateEventCategory_ == 0)
        LogError("Failed to query \"NetworkState\" event category");
    else
        LogInfo("System " + Name() + " subscribed to [NetworkIn]");
}

void InventoryModule::Update(Core::f64 frametime)
{
}

bool InventoryModule::HandleEvent(Core::event_category_id_t category_id, Core::event_id_t event_id,
    Foundation::EventDataInterface* data)
{
    if (category_id == networkStateEventCategory_)
    {
        if (event_id == ProtocolUtilities::Events::EVENT_SERVER_CONNECTED)
        {
            ProtocolUtilities::AuthenticationEventData *auth_data = dynamic_cast<ProtocolUtilities::AuthenticationEventData *>(data);
            if (!auth_data)
                return false;

            switch(auth_data->type)
            {
            case ProtocolUtilities::AT_Taiga:
                // Check if python module is loaded and has taken care of PythonQt::init()
                if (framework_->GetModuleManager()->HasModule(Foundation::Module::MT_PythonScript))
                    inventoryWindow_->InitWebDavInventoryTreeModel(auth_data->identityUrl, auth_data->hostUrl);
                break;
            case ProtocolUtilities::AT_OpenSim:
            case ProtocolUtilities::AT_RealXtend:
                inventoryWindow_->InitOpenSimInventoryTreeModel(this);
                break;
            default:
                break;
            }

            return false;
        }

        if (event_id == ProtocolUtilities::Events::EVENT_SERVER_DISCONNECTED)
        {
            if (inventoryWindow_)
            {
                inventoryWindow_->Hide();
                inventoryWindow_->ResetInventoryTreeModel();
            }
        }

        return false;
    }

    if (category_id == inventoryEventCategory_)
    {
        if (event_id == Inventory::Events::EVENT_INVENTORY_DESCENDENT)
        {
            InventoryItemEventData *item_data = dynamic_cast<InventoryItemEventData *>(data);
            if (!item_data)
                return false;

            inventoryWindow_->HandleInventoryDescendent(item_data);
        }

        return false;
    }

    if (category_id == frameworkEventCategory_ && event_id == Foundation::NETWORKING_REGISTERED)
    {
        Foundation::NetworkingRegisteredEvent *event_data = dynamic_cast<Foundation::NetworkingRegisteredEvent *>(data);
        if (event_data)
            SubscribeToNetworkEvents(event_data->currentProtocolModule);
        return false;
    }

    return false;
}

Console::CommandResult InventoryModule::UploadAsset(const Core::StringVector &params)
{
    return Console::ResultFailure("Single upload disabled for now.");
/*
    using namespace RexTypes;
    using namespace OpenSimProtocol;

    std::string name = "(No Name)";
    std::string description = "(No Description)";

    if (params.size() < 1)
        return Console::ResultFailure("Invalid syntax. Usage: \"upload [asset_type] [name] [description]."
            "Name and description are optional. Supported asset types:\n"
            "Texture\nMesh\nSkeleton\nMaterialScript\nParticleScript\nFlashAnimation");

    asset_type_t asset_type = GetAssetTypeFromTypeName(params[0]);
    if (asset_type == -1)
        return Console::ResultFailure("Invalid asset type. Supported parameters:\n"
            "Texture\nMesh\nSkeleton\nMaterialScript\nParticleScript\nFlashAnimation");

    if (params.size() > 1)
        name = params[1];

    if (params.size() > 2)
        description = params[2];

    std::string filter = GetOpenFileNameFilter(asset_type);
    std::string filename = Foundation::QtUtils::GetOpenFileName(filter, "Open", Foundation::QtUtils::GetCurrentPath());
    if (filename == "")
        return Console::ResultFailure("No file chosen.");

    assetUploader_->UploadFile(asset_type, filename, name, description, folder_id)

    return Console::ResultSuccess();
*/
}

Console::CommandResult InventoryModule::UploadMultipleAssets(const Core::StringVector &params)
{
    Core::StringList filenames = Foundation::QtUtils::GetOpenRexFileNames(Foundation::QtUtils::GetCurrentPath());
    if (filenames.empty())
        return Console::ResultFailure("No files chosen.");

    assetUploader_->UploadFiles(filenames);

    return Console::ResultSuccess();
}

} // namespace Inventory

extern "C" void POCO_LIBRARY_API SetProfiler(Foundation::Profiler *profiler);
void SetProfiler(Foundation::Profiler *profiler)
{
    Foundation::ProfilerSection::SetProfiler(profiler);
}

using namespace Inventory;

POCO_BEGIN_MANIFEST(Foundation::ModuleInterface)
    POCO_EXPORT_CLASS(InventoryModule)
POCO_END_MANIFEST
