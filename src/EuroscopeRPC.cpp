#include "EuroscopeRPC.h"
#include <numeric>
#include <chrono>
#include <algorithm>
#include <array>
#include <string_view>

#include "Version.h"

using namespace rpc;

rpc::EuroscopeRPC* myPluginInstance = nullptr;

namespace {
    void OnDiscordReady(const DiscordUser* user)
    {
        //if (user != nullptr && myPluginInstance != nullptr) {
        //    myPluginInstance->DisplayMessage("Connected to Discord as " + std::string(user->username) + "#" + std::string(user->discriminator), "Discord");
        //}
    }

    void OnDiscordDisconnected(int errcode, const char* message)
    {
        //if (myPluginInstance != nullptr) {
        //    myPluginInstance->DisplayMessage("Disconnected from Discord: " + std::to_string(errcode) + " - " + std::string(message ? message : ""), "Discord");
        //}
    }

    void OnDiscordErrored(int errcode, const char* message)
    {
        if (myPluginInstance != nullptr) {
            myPluginInstance->DisplayMessage("Discord error: " + std::to_string(errcode) + " - " + std::string(message ? message : ""), "Discord");
        }
    }

    DiscordEventHandlers CreateDiscordHandlers()
    {
        DiscordEventHandlers handlers{};
        handlers.ready = OnDiscordReady;
        handlers.disconnected = OnDiscordDisconnected;
        handlers.errored = OnDiscordErrored;
        return handlers;
    }
}

EuroscopeRPC::EuroscopeRPC() : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, "EuroscopeRPC", PLUGIN_VERSION, "Alexis Balzano", "Open Source")
{
    Initialize();
};
EuroscopeRPC::~EuroscopeRPC()
{
	Shutdown();
};

void __declspec (dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance)
{
    // create the instance
    *ppPlugInInstance = myPluginInstance = new rpc::EuroscopeRPC();
}


void __declspec (dllexport) EuroScopePlugInExit()
{
    // delete the instance
    delete myPluginInstance;
    myPluginInstance = nullptr;
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
    // No thread of our own: Discord is driven from OnTimer, on EuroScope's main thread,
    // because the plugin API OnTimer relies on is not thread safe
    discordSetup();
	//DisplayMessage("EuroscopeRPC initialized successfully", "Status");
}

void EuroscopeRPC::Shutdown()
{
    if (initialized_)
    {
        initialized_ = false;
        trackedCallsigns_.clear();
    }
    Discord_Shutdown();

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
    DiscordEventHandlers handlers = CreateDiscordHandlers();
    Discord_Initialize(APPLICATION_ID, &handlers, 1, nullptr);
}

void rpc::EuroscopeRPC::changeIdlingText()
{
    static int counter;
	counter++;
    static constexpr std::array<std::string_view, 41> idlingTexts = {
        "Waiting for traffic",
        "Monitoring frequencies",
        "Checking FL5000 for conflicts",
        "Watching the skies",
        "Searching for binoculars",
        "Listening to ATC chatter",
        "Scanning for aircrafts",
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
        "Answering radio checks",
        "Trying to contact UNICOM",
        "Arguing that France is not on strike",
        "Waiting for a readback",
        "Trying to find the strip printer",
        "Explaining wake turbulence again",
        "Reading the wrong scratchpad",
        "Wondering where the handoff went",
        "Assigning random headings professionally",
        "Resolving TCAS diplomacy",
        "Waiting for CPDLC to replace humanity",
        "Watching pilots miss MIDDLE1",
        "Monitoring questionable shortcuts",
        "Spacing arrivals with hope and optimism",
        "Politely denying shortcuts",
        "Watching VFRs discover weather",
        "Watching the approach sequence collapse",
        "Trying to avoid paperwork",
        "Trying to contact that one aircraft again",
        "Ensuring everyone survives the merge",
        "Explaining: standby MEANS STANDBY",
        "Silently judging non-standard phraseology",
        "Preparing emotionally for the next VFR"
    };

    idlingText_ = std::string(idlingTexts[counter % idlingTexts.size()]);
}

void rpc::EuroscopeRPC::updatePresence()
{
    if (!m_presence || connectionType_ == State::PROXY) {
        Discord_ClearPresence();
        return;
    }

    std::string controller = idlingText_;
	std::string state = "Idling";

    switch (connectionType_) {
    case State::CONTROLLING:
        controller = "Controlling " + currentController_ + " " + currentFrequency_;
        state = "Aircraft tracked: " + std::to_string(aircraftTracked_) + " of " + std::to_string(totalAircrafts_);
        break;
    case State::OBSERVING:
        controller = "Observing as " + currentController_;
        state = "Aircraft in range: " + std::to_string(totalAircrafts_);
        break;
    case State::SWEATBOX:
        controller = "In Sweatbox";
        state = "Aircraft tracked: (" + std::to_string(aircraftTracked_) + " of " + std::to_string(totalAircrafts_) + ")";
        break;
    case State::PLAYBACK:
        controller = "In Playback";
        state = "Aircraft in range: " + std::to_string(totalAircrafts_);
        break;
    default:
        break;
    }

    std::string imageKey = "";
	std::string imageText = "";

    switch (tier_) {
        case Tier::SILVER:
            imageKey = "silver";
            imageText = "On a " + std::to_string(onlineTime_) + " hour streak";
            break;
        case Tier::GOLD:
            if (imageKey.empty()) imageKey = "gold";
            imageText = "On a " + std::to_string(onlineTime_) + " hour streak";
			break;
        case Tier::NONE:
        default:
            imageKey = "main";
            imageText = "French VACC";
			break;
    }

    if (isOnFire_) {
        imageKey += "fire";
        if (!imageText.empty()) imageText += " ";
        imageText += "On Fire!";
	}

    if (imageKey.empty()) imageKey = "main";
	if (imageText.empty()) imageText = "French VACC";

    static thread_local std::string stateStorage;
    static thread_local std::string detailsStorage;
    static thread_local std::string largeImageKeyStorage;
    static thread_local std::string largeImageTextStorage;
    static thread_local std::string smallImageTextStorage;

    stateStorage = state;
    detailsStorage = controller;
    largeImageKeyStorage = imageKey;
    largeImageTextStorage = imageText;
    smallImageTextStorage = "Total Tracks: " + std::to_string(totalTracks_);

    DiscordRichPresence presence{};
    presence.state = stateStorage.c_str();
    presence.details = detailsStorage.c_str();
    presence.largeImageKey = largeImageKeyStorage.c_str();
    presence.largeImageText = largeImageTextStorage.c_str();
    presence.smallImageKey = (connectionType_ == State::CONTROLLING || connectionType_ == State::SWEATBOX) ? "radarlogo" : nullptr;
    presence.smallImageText = smallImageTextStorage.c_str();
    presence.startTimestamp = StartTime;
    presence.instance = 1;

    Discord_UpdatePresence(&presence);
}

void rpc::EuroscopeRPC::updateData()
{
	updateConnectionType();
	getAicraftCount();

	if (std::time(nullptr) - StartTime > 2 * HOUR_THRESHOLD) tier_ = Tier::GOLD;
    else if (std::time(nullptr) - StartTime > HOUR_THRESHOLD) tier_ = Tier::SILVER;
	else tier_ = Tier::NONE;

	onlineTime_ = static_cast<int>((std::time(nullptr) - StartTime) / 3600); // in hours
    isOnFire_ = (aircraftTracked_ >= ONFIRE_THRESHOLD);
}

void rpc::EuroscopeRPC::updateConnectionType()
{
	connectionType_ = State::IDLE;
    CController selfController = myPluginInstance->ControllerMyself();
    int euroscopeConnectionType = myPluginInstance->GetConnectionType();
    switch (euroscopeConnectionType) {
    case CONNECTION_TYPE_NO:
        connectionType_ = State::IDLE;
        break;
    case CONNECTION_TYPE_DIRECT:
        if (selfController.IsController()) {
            connectionType_ = State::CONTROLLING;
            std::string freq = std::to_string(selfController.GetPrimaryFrequency());
            currentFrequency_ = freq.substr(0, freq.length() - 3);
        }
        else connectionType_ = State::OBSERVING;
        currentController_ = selfController.GetCallsign();
		std::transform(currentController_.begin(), currentController_.end(), currentController_.begin(), ::toupper);
        break;
    case CONNECTION_TYPE_SWEATBOX:
        connectionType_ = State::SWEATBOX;
        break;
    case CONNECTION_TYPE_PLAYBACK:
        connectionType_ = State::PLAYBACK;
        break;
	case CONNECTION_TYPE_VIA_PROXY:
        connectionType_ = State::PROXY;
        break;
    default:
        // Checked every 5 seconds: report each unknown type once instead of flooding the chat
        if (euroscopeConnectionType != lastUnknownConnectionType_) {
            lastUnknownConnectionType_ = euroscopeConnectionType;
            DisplayMessage("Unknown connection type: " + std::to_string(euroscopeConnectionType), "Error");
        }
        connectionType_ = State::IDLE;
        break;
    }
}

void rpc::EuroscopeRPC::getAicraftCount()
{
	totalAircrafts_ = 0;
	aircraftTracked_ = 0;
    CRadarTarget target = myPluginInstance->RadarTargetSelectFirst();

    while (target.IsValid()) {
		++totalAircrafts_;
        if (target.GetCorrelatedFlightPlan().GetTrackingControllerIsMe()) {
            ++aircraftTracked_;
            if (trackedCallsigns_.insert(target.GetCallsign()).second) {
                ++totalTracks_;
            }
        }
        target = myPluginInstance->RadarTargetSelectNext(target);
	}
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
    Discord_RunCallbacks();
}