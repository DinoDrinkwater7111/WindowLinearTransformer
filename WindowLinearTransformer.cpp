#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>
#include <cli/cli.h>
#include <cli/clifilesession.h>
#include <tabulate/table.hpp>
#include <Windows.h>
#include <Psapi.h>

using json = nlohmann::json;

namespace WLT {
	class exception :public std::exception {
	public:
		exception(std::string message) :std::exception(message.c_str()) {}
	};


	BOOL CALLBACK EnumFunc(HWND hwnd, LPARAM cbPtr) {
		const auto cb = (std::function<BOOL(HWND)>*)cbPtr;
		return (*cb)(hwnd);
	}

	std::vector<HWND> ListWindowHWND() {
		std::vector<HWND> windowHWNDs = {};
		std::function<BOOL(HWND)> enumCb = [&windowHWNDs](HWND hwnd) ->BOOL {
			windowHWNDs.push_back(hwnd);
			return TRUE;
		};
		const auto isSuccess = EnumWindows(EnumFunc, reinterpret_cast<LPARAM>(&enumCb));
		if (!isSuccess) {
			const auto errorCode = GetLastError();
			throw WLT::exception("ListWindowHWND Failed: " + std::system_category().message(errorCode));
		}
		return windowHWNDs;
	}

	std::string GetWindowTitile(HWND windowHWND) {
		constexpr auto BUFFER_SIZE = 4096;
		const auto buffer = std::make_unique<char[]>(BUFFER_SIZE);
		const auto isSuccess = GetWindowTextA(windowHWND, buffer.get(), BUFFER_SIZE);
		if (!isSuccess) {
			const auto errorCode = GetLastError();
			throw WLT::exception("GetWindowTitile Failed: " + std::system_category().message(errorCode));
		}
		return buffer.get();
	}

	DWORD GetWindowProcessId(HWND windowHWND) {
		DWORD dwProcessId;
		GetWindowThreadProcessId(windowHWND, &dwProcessId);
		return dwProcessId;
	}

	HANDLE GetProcessHandle(DWORD dwProcessId) {
		auto processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, dwProcessId);
		if (processHandle == NULL) {
			const auto errorCode = GetLastError();
			throw WLT::exception("GetProcessHandle Failed: " + std::system_category().message(errorCode));
		}
		return processHandle;
	}

	std::string GetProcessFileName(HANDLE processHandle) {
		constexpr auto BUFFER_SIZE = 4096;
		const auto buffer = std::make_unique<char[]>(BUFFER_SIZE);
		const auto fileNameLength = GetModuleBaseNameA(processHandle, NULL, buffer.get(), BUFFER_SIZE);
		if (fileNameLength == 0) {
			const auto errorCode = GetLastError();
			throw WLT::exception("GetProcessFileName Failed: " + std::system_category().message(errorCode));
		}
		return buffer.get();
	}

	void ListWindows(std::ostream& out, std::string titleRxStr)
	{
		try {
			const auto windowHWNDs = WLT::ListWindowHWND();
			tabulate::Table table;
			table.add_row({ "Title", "Process Name", "PID" });
			for (auto i = 0; i < 3; i++) {
				table[0][i].format()
					.font_color(tabulate::Color::yellow)
					.font_style({ tabulate::FontStyle::bold });
			}
			const auto titleRx = std::regex(titleRxStr);
			for (const auto& windowHWND : windowHWNDs) {
				try {
					const auto title = WLT::GetWindowTitile(windowHWND);
					if (!std::regex_search(title, titleRx)) continue;
					const auto dwProcessId = WLT::GetWindowProcessId(windowHWND);
					const auto processHandle = WLT::GetProcessHandle(dwProcessId);
					const auto processFileName = WLT::GetProcessFileName(processHandle);
					table.add_row({ title, processFileName, std::to_string(dwProcessId) });
				}
				catch (WLT::exception&) {
					continue;
				}
			}
			for (auto it = table.begin(); it != table.end(); ++it) {
				auto& row = *it;
				row[0].format().width(40).multi_byte_characters(true);
				row[1].format().width(40).multi_byte_characters(true);
				row[2].format().width(10).multi_byte_characters(true);
			}
			out << table << std::endl;
		}
		catch (WLT::exception& e) {
			out << e.what() << std::endl;
		}
	}

	void RunProfile(std::ostream& out, std::string id) {
		std::filesystem::path currentpath;
		{
			constexpr auto BUFFER_SIZE = 8192;
			const auto buffer = std::make_unique<char[]>(BUFFER_SIZE);
			const auto pathLength = GetModuleFileNameA(NULL, buffer.get(), BUFFER_SIZE);
			if (pathLength == 0) {
				const auto errorCode = GetLastError();
				out << "GetModuleFileNameA Error: " + std::system_category().message(errorCode);
				return;
			}
			currentpath = buffer.get();
		}

		constexpr auto profilesJsonFile = "profiles.json";
		const auto profilesJsonFileFullPath = (currentpath.parent_path() / profilesJsonFile).string();
		if (!std::filesystem::exists(profilesJsonFileFullPath)) {
			out << "File not exist: " + profilesJsonFileFullPath << std::endl;
			return;
		}

		json profilesJson;
		try
		{
			std::ifstream fStream(profilesJsonFileFullPath);
			fStream >> profilesJson;
		}
		catch (json::exception e) {
			out << e.what() << std::endl;
			return;
		}

		if (!profilesJson.is_array()) {
			out << "Error: Json is not array" << std::endl;
			return;
		}

		json profile;
		for (auto it = profilesJson.begin(); it != profilesJson.end(); ++it) {
			auto& _profile = *it;
			if (_profile.contains("id")) {
				const auto profileId = _profile["id"].get<std::string>();
				if (profileId == id) {
					profile = _profile;
					break;
				}
			}
		}

		if (profile == nullptr) {
			out << "Error: No profile with id " + id << std::endl;
			return;
		}

		if (!profile["condition"].is_object()) {
			out << "Error: condition is not object" << std::endl;
			return;
		}
		const json condition = profile["condition"];
		if (condition.size() == 0) {
			out << "Error: condition is empty" << std::endl;
			return;
		}


		std::regex windowTitleRx;
		std::regex processNameRx;
		DWORD pid = 0;
		if (condition.contains("windowTitle")) {
			windowTitleRx = std::regex(condition["windowTitle"].get<std::string>());
		}
		if (condition.contains("processName")) {
			processNameRx = std::regex(condition["processName"].get<std::string>());
		}
		if (condition.contains("pid")) {
			const auto _pid = condition["pid"].get<int>();
			if (_pid <= 0) {
				out << "Error: condition.pid must be positive" << std::endl;
				return;
			}
			pid = _pid;
		}

		if (!profile["pos"].is_object()) {
			out << "Error: pos is not object" << std::endl;
			return;
		}
		const json pos = profile["pos"];

		if (!pos["x"].is_number_integer()) {
			out << "Error: pos.x must be integer" << std::endl;
			return;
		}
		int x = pos["x"];

		if (!pos["y"].is_number_integer()) {
			out << "Error: pos.y must be integer" << std::endl;
			return;
		}
		int y = pos["y"];

		if (!pos["width"].is_number_integer()) {
			out << "Error: pos.width must be integer" << std::endl;
			return;
		}
		int width = pos["width"];

		if (!pos["height"].is_number_integer()) {
			out << "Error: pos.height must be integer" << std::endl;
			return;
		}
		int height = pos["height"];


		const auto windowHWNDs = WLT::ListWindowHWND();
		for (const auto& windowHWND : windowHWNDs) {
			try {
				const auto title = WLT::GetWindowTitile(windowHWND);
				const auto dwProcessId = WLT::GetWindowProcessId(windowHWND);
				const auto processHandle = WLT::GetProcessHandle(dwProcessId);
				const auto processFileName = WLT::GetProcessFileName(processHandle);
				if (!processNameRx._Empty() && !std::regex_search(title, processNameRx)) continue;
				if (!windowTitleRx._Empty() && !std::regex_search(title, windowTitleRx)) continue;
				if (pid > 0 && pid != dwProcessId) continue;
				auto uFlags = SWP_NOZORDER;
				if (x < 0 || y < 0) uFlags |= SWP_NOMOVE;
				if (width < 0 || height < 0) uFlags |= SWP_NOSIZE;
				const auto isSuccess = SetWindowPos(windowHWND, NULL, x, y, width, height, uFlags);
				if (!isSuccess) {
					const auto errorCode = GetLastError();
					out << "SetWindowPos Failed: " + std::system_category().message(errorCode) << std::endl;
				}
				return;
			}
			catch (WLT::exception&) {
				continue;
			}
		}

		out << "No matched window" << std::endl;
	}

	void StartCli()
	{
		SetConsoleOutputCP(65001);
		cli::SetColor();
		auto rootMenu = std::make_unique<cli::Menu>("cli");
		rootMenu->Insert(
			"LIST_WINDOWS",
			[](std::ostream& out, std::string titleRx) { WLT::ListWindows(out, titleRx); },
			"Usage: LIST_WINDOWS <titleRx>");
		rootMenu->Insert(
			"RUN_PROFILE",
			[](std::ostream& out, std::string id) { WLT::RunProfile(out, id); },
			"Usage: RUN_PROFILE <id>");

		cli::Cli cli(std::move(rootMenu));

		cli::CliFileSession input(cli);
		input.Start();
	}
}

int main()
{
	WLT::StartCli();
}