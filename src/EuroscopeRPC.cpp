#include "EuroscopeRPC.h"
#include <numeric>
#include <chrono>

#include "Version.h"

using namespace rpc;

EuroscopeRPC::EuroscopeRPC() : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, "EuroscopeRPC", PLUGIN_VERSION, "Alexis Balzano", "Open Source"), m_stop(false)
{
    Initialize();
};
EuroscopeRPC::~EuroscopeRPC()
{
	Shutdown();
};

rpc::EuroscopeRPC* myPluginInstance = nullptr;

void __declspec (dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    // create the instance
    *ppPlugInInstance = myPluginInstance = new rpc::EuroscopeRPC();
}


void __declspec (dllexport) EuroScopePlugInExit()
{
    // delete the instance
    delete myPluginInstance;
}

void EuroscopeRPC::Initialize()
{
    StartTime = time(nullptr);

    try
    {
        initialized_ = true;
    }
    catch (const std::exception& e)
    {
		DisplayMessage("Failed to initialize EuroscopeRPC: " + std::string(e.what()), "Error");
    }
    m_stop = false;
    m_thread = std::thread(&EuroscopeRPC::run, this);
	DisplayMessage("EuroscopeRPC initialized successfully", "Status");
}

void EuroscopeRPC::Shutdown()
{
    if (initialized_)
    {
        initialized_ = false;
        trackedCallsigns_.clear();
    }
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();

	DisplayMessage("EuroscopeRPC shutdown complete", "Status");
}

void rpc::EuroscopeRPC::Reset()
{
}

void EuroscopeRPC::DisplayMessage(const std::string &message, const std::string &sender) {
    DisplayUserMessage("EuroscopeRPC", sender.c_str(), message.c_str(), true, true, false, false, false);
}

void rpc::EuroscopeRPC::discordSetup()
{
    discord::RPCManager::get()
        .setClientID(APPLICATION_ID)
        .onReady([this](discord::User const& user) {
		DisplayMessage("Connected to Discord as " + user.username + "#" + user.discriminator, "Discord");
            })
        .onDisconnected([this](int errcode, std::string_view message) {
		DisplayMessage("Disconnected from Discord: " + std::to_string(errcode) + " - " + std::string(message), "Discord");
            })
        .onErrored([this](int errcode, std::string_view message) {
		DisplayMessage("Discord error: " + std::to_string(errcode) + " - " + std::string(message), "Discord");
            });
}

void rpc::EuroscopeRPC::changeIdlingText()
{
    static int counter;
	counter++;
    static constexpr std::array<std::string_view, 21> idlingTexts = {
        "Waiting for traffic",
        "Monitoring frequencies",
        "Checking FL5000 for conflicts",
        "Watching the skies",
        "Searching for binoculars",
        "Listening to ATC chatter",
        "Scanning for aircraft",
        "Awaiting calls",
        "Tracking airspace",
        "Possible pilot deviation, I have a number...",
        "Clearing ILS 22R",
        "Observing traffic flow",
        "Monitoring silence",
        "Awaiting handoffs",
        "Recording ATIS",
        "Radar scope screensaver",
		"Checking NOTAMs",
		"Deleting SIDs from Flight Plans",
        "Answering radio check",
        "Trying to contact UNICOM",
        "Arguing that France is not on strike"
    };

    idlingText_ = std::string(idlingTexts[counter % idlingTexts.size()]);
}

void rpc::EuroscopeRPC::updatePresence()
{
    auto& rpc = discord::RPCManager::get();
    if (!m_presence) {
        rpc.clearPresence();
        return;
    }

    std::string controller = idlingText_;
	std::string state = "Idling";

    if (isControllerATC_) {
        state = "Aircraft tracked: (" + std::to_string(aircraftTracked_) + " of " + std::to_string(totalAircrafts_) + ")";
        controller = "Controlling " + currentController_ + " " + currentFrequency_;
        rpc.getPresence().setSmallImageKey("radarlogo");
    }
    else if (isObserver_) {
        state = "Aircraft in range: " + std::to_string(totalAircrafts_);
        controller = "Observing as " + currentController_;
    }
    else {
        rpc.getPresence().setSmallImageKey("");
    }

    std::string imageKey = "";
	std::string imageText = "";

    if (isGolden_ && isOnFire_) {
        rpc.getPresence()
            .setLargeImageKey("both")
            .setLargeImageText(std::to_string(onlineTime_) + " hour streak, On Fire!");
    }

    if (isSilver_) {
        imageKey = "silver";
        imageText = "On a " + std::to_string(onlineTime_) + " hour streak";
	}

    if (isGolden_) {
        imageKey = "gold";
        imageText = "On a " + std::to_string(onlineTime_) + " hour streak";
    }

    if (isOnFire_) {
        imageKey += "fire";
        if (!imageText.empty()) imageText += " ";
        imageText += "On Fire!";
	}

    if (imageKey.empty()) imageKey = "main";
	if (imageText.empty()) imageText = "French VACC";


    rpc.getPresence()
        .setState(state)
		.setLargeImageKey(imageKey)
		.setLargeImageText(imageText)
        .setActivityType(discord::ActivityType::Game)
        .setStatusDisplayType(discord::StatusDisplayType::Name)
        .setDetails(controller)
        .setStartTimestamp(StartTime)
        .setSmallImageText("Total Tracks: " + std::to_string(totalTracks_))
        .setInstance(true)
        .refresh();
}

void rpc::EuroscopeRPC::updateData()
{
    isControllerATC_ = false;
	isObserver_ = true;
    currentController_ = "AB_OBS";
    currentFrequency_ = "";

    //GetConnectionType();
    /*const   int     CONNECTION_TYPE_NO = 0;
    const   int     CONNECTION_TYPE_DIRECT = 1;
    const   int     CONNECTION_TYPE_VIA_PROXY = 2;
    const   int     CONNECTION_TYPE_SIMULATOR_SERVER = 3;
    const   int     CONNECTION_TYPE_PLAYBACK = 4;
    const   int     CONNECTION_TYPE_SIMULATOR_CLIENT = 5;
    const   int     CONNECTION_TYPE_SWEATBOX = 6;*/





    /*auto connectionData = fsdAPI_->getConnection();
    if (connectionData) {
        if (connectionData->facility != Fsd::NetworkFacility::OBS) {
            isControllerATC_ = true;
            isObserver_ = false;
        }
        else {
            isControllerATC_ = false;
            isObserver_ = true;
        }

        currentController_ = connectionData->callsign;
        if (connectionData->frequencies.empty()) currentFrequency_ = "";
        else {
			std::string freq = std::to_string(connectionData->frequencies[0]);
			currentFrequency_ = freq.substr(0, freq.length() - 6) + "." + freq.substr(freq.length() - 6, 3);
        }
    }*/
	//CController controller = CController::CController();
    //isControllerATC_ = controller.IsController();

    //totalAircrafts_ = static_cast<uint32_t>(aircraftAPI_->getAll().size());

    aircraftTracked_ = 0;
	totalAircrafts_ = 5;
    totalTracks_ = 20;
    /*std::vector<ControllerData::ControllerDataModel> controllerDatas = controllerDataAPI_->getAll();
    for (const auto& controllerData : controllerDatas) {
        if (controllerData.ownedByMe) {
            ++aircraftTracked_;
            if (trackedCallsigns_.insert(controllerData.callsign).second) {
                ++totalTracks_;
            }
        }
    }*/

	isSilver_ = (std::time(nullptr) - StartTime > HOUR_THRESHOLD);
	isGolden_ = (std::time(nullptr) - StartTime > 2 * HOUR_THRESHOLD);
	onlineTime_ = static_cast<int>((std::time(nullptr) - StartTime) / 3600); // in hours
    isOnFire_ = (aircraftTracked_ >= ONFIRE_THRESHOLD);
}

void EuroscopeRPC::runUpdate() {
	this->updatePresence();
}

void EuroscopeRPC::OnTimer(int Counter) {
    if (Counter % 5 == 0) // Every 5 seconds
        updateData();
    if (Counter % 15 == 0) // Every 15 seconds
        changeIdlingText();
    this->runUpdate();
}

void EuroscopeRPC::run() {
    int counter = 1;
    discordSetup();
    discord::RPCManager::get().initialize();
    auto& rpc = discord::RPCManager::get();

    while (true) {
        counter += 1;
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (true == this->m_stop) {
            discord::RPCManager::get().shutdown();
            return;
        }
        
        this->OnTimer(counter);
    }
    return;
}