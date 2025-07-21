#include <fstream>
#include <algorithm>

#include "../NeoVSID.h"

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif

#include "DataManager.h"

DataManager::DataManager(vsid::NeoVSID* neoVSID)
	: neoVSID_(neoVSID) {
	aircraftAPI_ = neoVSID_->GetAircraftAPI();
	flightplanAPI_ = neoVSID_->GetFlightplanAPI();
	airportAPI_ = neoVSID_->GetAirportAPI();
	chatAPI_ = neoVSID_->GetChatAPI();
	controllerDataAPI_ = neoVSID_->GetControllerDataAPI();

	configPath_ = getDllDirectory();
	loadAircraftDataJson();
	activeAirports.clear();
}


std::filesystem::path DataManager::getDllDirectory()
{
#if defined(_WIN32)
	wchar_t buffer[MAX_PATH];
	HMODULE hModule = nullptr;
	// Use the address of this function to get the module handle
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&getDllDirectory), &hModule);
	GetModuleFileNameW(hModule, buffer, MAX_PATH);
	return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
	Dl_info dl_info;
	dladdr((void*)&getDllDirectory, &dl_info);
	return std::filesystem::path(dl_info.dli_fname).parent_path();
#elif defined(__linux__)
	Dl_info dl_info;
	dladdr((void*)&getDllDirectory, &dl_info);
	return std::filesystem::path(dl_info.dli_fname).parent_path();
#else
	return std::filesystem::path(); // Return an empty path for unsupported platforms
#endif
}

void DataManager::clearData()
{
	pilots.clear();
	activeAirports.clear();
	configJson_.clear();
	configPath_.clear();
	if (aircraftAPI_)
		aircraftAPI_ = nullptr;
	if (flightplanAPI_)
		flightplanAPI_ = nullptr;
	if (airportAPI_)
		airportAPI_ = nullptr;
}

void DataManager::DisplayMessageFromDataManager(const std::string& message, const std::string& sender)
{
	Chat::ClientTextMessageEvent textMessage;
	textMessage.sentFrom = "NeoVSID";
	(sender.empty()) ? textMessage.message = ": " + message : textMessage.message = sender + ": " + message;
	textMessage.useDedicatedChannel = true;

	chatAPI_->sendClientMessage(textMessage);
}

void DataManager::populateActiveAirports()
{
	std::vector<Airport::AirportConfig> allAirports = airportAPI_->getConfigurations();
	activeAirports.clear();

	for (const auto& airport : allAirports)
	{
		// Check if the airport is active
		if (airport.status == Airport::AirportStatus::Active)
		{
			activeAirports.push_back(airport.icao);
		}
	}
}

std::string DataManager::generateVRWY(const Flightplan::Flightplan& flightplan)
{
	/* TODO:
	* - generer la piste en fonction des pistes de departs actuellement configur�es sur le terrain de d�part du trafic
	* - generer la piste en fonction de la position du trafic par rapport au terrain de d�part si plusieurs pistes de d�part et config roulage mini
	* - generer la piste en fonction de la direction du depart si config croisement au sol
	*/ 
	return flightplan.route.suggestedDepRunway;
}

sidData DataManager::generateVSID(const Flightplan::Flightplan& flightplan, const std::string& depRwy)
{
	/* TODO:
	* - generer SID en fonction de la piste assign�e
	* - tester implementation actuelle
	*/
	DisplayMessageFromDataManager("Generating VSID for flightplan: " + flightplan.callsign, "DataManager");

	std::string suggestedSid = flightplan.route.suggestedSid;

	DisplayMessageFromDataManager("Suggested SID: " + suggestedSid, "DataManager");
	
	// Check if configJSON is already the right one, if not, retrieve it
	std::string oaci = flightplan.origin;
	if (!configJson_.contains(oaci) || configJson_.empty()) {
		if (retrieveConfigJson(oaci) == -1) {
			DisplayMessageFromDataManager("Error retrieving config JSON for OACI: " + oaci, "DataManager");
			return { suggestedSid, 0};
		}
	}

	// Extract waypoint only SID information
	std::transform(oaci.begin(), oaci.end(), oaci.begin(), ::toupper); //Convert to uppercase
	if (suggestedSid.empty() || suggestedSid.length() < 2) {
		return { suggestedSid, 0};
	}
	std::string waypoint = suggestedSid.substr(0, suggestedSid.length() - 2);
	std::string indicator = suggestedSid.substr(suggestedSid.length() - 2, 1);
	std::string letter = suggestedSid.substr(suggestedSid.length() - 1, 1);

	nlohmann::json waypointSidData;

	if (configJson_.contains(oaci) && configJson_[oaci]["sids"].contains(waypoint)) {
		waypointSidData = configJson_[oaci]["sids"][waypoint];
	} else {
		DisplayMessageFromDataManager("SID not found in config JSON for waypoint: " + waypoint + " for: " + flightplan.callsign, "DataManager");
		return { suggestedSid, 0};
	}

	//From here we have all the data necessary from oaci.json
	if (waypointSidData.contains(letter)) {
		if (waypointSidData[letter]["1"].contains("engineType")) {
			std::string aircraftType = flightplan.acType;
			std::string engineType = "J"; // Defaulting to Jet if no type is found
			if (aircraftDataJson_.contains(aircraftType))
				engineType = aircraftDataJson_[aircraftType]["engineType"].get<std::string>();
			std::string requiredEngineType = waypointSidData[letter]["1"]["engineType"].get<std::string>();
			if (requiredEngineType.find(engineType) != std::string::npos) {
				// Engine type matches, we can assign this SID and CFL
				int fetchedCfl = waypointSidData[letter]["1"]["initial"].get<int>();
				return { waypoint + indicator + letter, fetchedCfl };
			}
			else {
				if (waypointSidData.contains("2")) {
					// If there is a CFL for the other engine type, we assign it
					int fetchedCfl = waypointSidData[letter]["2"]["initial"].get<int>();
					return { waypoint + indicator + letter, fetchedCfl };
				}
				else {
				// Get the very next SID because it is the other engine type one
					auto it = waypointSidData.find(letter); //get iterator to the current letter
					if (it != waypointSidData.end()) {
						++it; // Move to the next key
						if (it != waypointSidData.end()) {
							std::string nextLetter = it.key();
							return { waypoint + indicator + nextLetter, it.value()["1"]["initial"].get<int>() };
						}
					}
					// If no next letter, we return the suggested SID with CFL 0
					return { suggestedSid, waypointSidData[letter]["1"]["initial"].get<int>() };
				}

			}
		}
		else {
			// If no engine restriction then we assign this SID and CFL
			int fetchedCfl = waypointSidData[letter]["1"]["initial"].get<int>();
			return { waypoint + indicator + letter, fetchedCfl };
		}
	}
	return { suggestedSid, 0 };
}

int DataManager::retrieveConfigJson(const std::string& oaci)
{
	std::string fileName = oaci + ".json";
	std::filesystem::path jsonPath = configPath_ / "NeoVSID" / fileName;

	std::ifstream config(jsonPath);
	if (!config.is_open()) {
		DisplayMessageFromDataManager("Could not open JSON file: " + jsonPath.string(), "DataManager");
		return -1;
	}

	try {
		config >> configJson_;
	}
	catch (...) {
		DisplayMessageFromDataManager("Error parsing JSON file: " + jsonPath.string(), "DataManager");
		return -1;
	}
	return 0; // Return 0 if the JSON file was successfully retrieved
}

void DataManager::loadAircraftDataJson()
{
	std::filesystem::path jsonPath = configPath_ / "NeoVSID" / "AircraftData.json";
	std::ifstream aircraftDataFile(jsonPath);
	if (!aircraftDataFile.is_open()) {
		DisplayMessageFromDataManager("Could not open aircraft data JSON file: " + jsonPath.string(), "DataManager");
		return;
	}
	try {
		aircraftDataJson_ = nlohmann::json::parse(aircraftDataFile);
	}
	catch (...) {
		DisplayMessageFromDataManager("Error parsing aircraft data JSON file: " + jsonPath.string(), "DataManager");
		return;
	}
}

Pilot DataManager::getPilotByCallsign(std::string callsign) const
{
	if (callsign.empty())
		return Pilot{};
	for (const auto& pilot : pilots)
	{
		if (pilot.callsign == callsign)
			return pilot;
	}
	return Pilot{};
}

std::vector<std::string> DataManager::getAllDepartureCallsigns() {
	std::vector<PluginSDK::Flightplan::Flightplan> flightplans = flightplanAPI_->getAll();
	std::vector<std::string> callsigns;

	for (const auto& flightplan : flightplans)
	{
		if (flightplan.callsign.empty())
			continue;

		if (!aircraftExists(flightplan.callsign))
			continue;

		if (!isDepartureAirport(flightplan.origin))
			continue;

		if (aircraftAPI_->getDistanceFromOrigin(flightplan.callsign) > 2)
			continue;

		if (controllerDataAPI_->getByCallsign(flightplan.callsign)->groundStatus == ControllerData::GroundStatus::Dep)
			continue;

		callsigns.push_back(flightplan.callsign);

		// Check if the pilot already exists in pilots vector
		if (std::find_if(pilots.begin(), pilots.end(), [&](const Pilot& p) { return p.callsign == flightplan.callsign; }) != pilots.end())
			continue;

		std::string vsidRwy = generateVRWY(flightplan);
		sidData vsidData = generateVSID(flightplan, vsidRwy);
		pilots.push_back(Pilot{ flightplan.callsign, vsidRwy, vsidData.sid, vsidData.cfl});
	}
	return callsigns;
}

bool DataManager::isDepartureAirport(const std::string& oaci) const
{
	if (oaci.empty())
		return false;

	for (const auto& airport : activeAirports)
	{
		if (oaci == airport)
			return true;
	}
	return false;
}

bool DataManager::aircraftExists(const std::string& callsign) const
{
	if (callsign.empty())
		return false;
	std::optional<PluginSDK::Aircraft::Aircraft> aircraft = aircraftAPI_->getByCallsign(callsign);
	if (aircraft.has_value())
		return true;
	return false;
}

bool DataManager::pilotExists(const std::string& callsign) const
{
	if (std::find_if(pilots.begin(), pilots.end(), [&](const Pilot& p) { return p.callsign == callsign; }) != pilots.end())
		return true;
	return false;
}

void DataManager::addPilot(const std::string& callsign)
{
	Flightplan::Flightplan flightplan = flightplanAPI_->getByCallsign(callsign).value();

	if (callsign.empty())
		return;
	// Check if the pilot already exists
	if (std::find_if(pilots.begin(), pilots.end(), [&](const Pilot& p) { return p.callsign == callsign; }) != pilots.end())
		return;
	if (!isDepartureAirport(flightplan.origin))
		return;

	std::string vsidRwy = generateVRWY(flightplan);
	sidData vsidData = generateVSID(flightplan, vsidRwy);
	pilots.push_back(Pilot{ flightplan.callsign, vsidRwy, vsidData.sid, vsidData.cfl });
}

void DataManager::removePilot(const std::string& callsign)
{
	if (callsign.empty())
		return;
	// Remove the pilot from the list
	pilots.erase(std::remove_if(pilots.begin(), pilots.end(), [&](const Pilot& p) { return p.callsign == callsign; }), pilots.end());
}
