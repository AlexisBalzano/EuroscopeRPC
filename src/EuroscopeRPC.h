#pragma once
#include <memory>
#include <thread>
#include <vector>
#include <unordered_set>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <EuroScopePlugIn.h>
#include <discord-rpc.hpp>

using namespace EuroScopePlugIn;


namespace rpc {
    constexpr auto APPLICATION_ID = "1408567135428673546";
    static int64_t StartTime;
    static bool SendPresence = true;

	constexpr uint32_t ONFIRE_THRESHOLD = 10;
	constexpr uint32_t HOUR_THRESHOLD = 7200; // 2 hour

    class EuroscopeRPCCommandProvider;

    class EuroscopeRPC : public CPlugIn
    {
    public:
        EuroscopeRPC();
        ~EuroscopeRPC();

		// Plugin lifecycle methods
		void Initialize();
		void Shutdown();
        void Reset();

        // Radar commands
        void DisplayMessage(const std::string& message, const std::string& sender = "");
		
        // Scope events
        void OnTimer(int Counter);

        // Getters
		bool getPresence() const { return m_presence; }

		// Setters
		void setPresence(bool presence) { m_presence = presence; }

    private:
        void discordSetup();
        void changeIdlingText();
		void updatePresence();
		void updateData();
        void runUpdate();
        void run();

    private:
        // Plugin state
        bool initialized_ = false;
		bool m_stop;
		bool m_presence = true; // Send presence to Discord
		std::thread m_thread;

		bool isControllerATC_ = false;
		bool isObserver_ = false;
        bool isOnFire_ = false;
		bool isSilver_ = false;
        bool isGolden_ = false;
        std::string currentController_ = "";
        std::string currentFrequency_ = "";
		std::string idlingText_ = "Watching the skies";
		int onlineTime_ = 0; // in hours
		std::unordered_set<std::string> trackedCallsigns_;
        
		uint32_t totalTracks_ = 0;
		uint32_t totalAircrafts_ = 0;
		uint32_t aircraftTracked_ = 0;

    };
} // namespace rpc