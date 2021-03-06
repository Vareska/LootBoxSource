#define _CRT_SECURE_NO_WARNINGS

#include <SQLiteCpp/Database.h>

#include <API/ARK/Ark.h>
#include <IApiUtils.h>
#include <API/UE/Math/ColorList.h>

#include "../../json.hpp"

#include "LootDatabase.h"
#include "RandomRewards.h"
#include <fstream>
#include <iostream>
#include "LootBoxes.h"

#pragma comment(lib, "ArkApi.lib")


nlohmann::json LootBoxes::config;

void replace_string_in_place(std::string& subject, const std::string& search, const std::string& replace) {
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos) {
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
}

FString getConfigMessage(const std::string& key) {
	return FString(ArkApi::Tools::ConvertToAnsiStr(ArkApi::Tools::Utf8Decode((LootBoxes::config["Messages"].value(key, "Unable to find key \"" + key + "\" in config file!")))));
}


void GiveLootBox(APlayerController * sender, FString* message, bool) {

	TArray<FString> parsed;
	message->ParseIntoArray(parsed, L" ", true);

	AShooterPlayerController * player = static_cast<AShooterPlayerController *>(sender);

	// Console / RCON --> GiveLootbox <steamId> <lootbox> <amount>
	if (parsed.IsValidIndex(3)) {
		if (parsed[1].ToString().find_first_not_of("0123456789") != std::string::npos || parsed[3].ToString().find_first_not_of("0123456789") != std::string::npos) {
			ArkApi::GetApiUtils().SendChatMessage(player,  *getConfigMessage("prefix"), *FString("Please use GiveLootbox <steamId> <lootbox> <amount>"));
			return;
		}

		uint64 steamId = std::stoull(parsed[1].ToString(), 0, 10);
		FString lootboxname = parsed[2];
		int amount = std::stoull(parsed[3].ToString(), 0, 10);

		LootDatabase::addLootBox(steamId, lootboxname, amount);
		ArkApi::GetApiUtils().SendChatMessage(player, *getConfigMessage("prefix"), *FString("Success!"));
		return;
	}
	ArkApi::GetApiUtils().SendChatMessage(player, *getConfigMessage("prefix"), *FString("Please use GiveLootbox <steamId> <lootbox> <amount>"));
}
 
void use_lootbox(APlayerController* player_controller, FString* message, bool) {

	AShooterPlayerController* sender = static_cast<AShooterPlayerController*>(player_controller);

	TArray<FString> parsed;
	message->ParseIntoArray(parsed, L" ", true);

	uint64 steamId = ArkApi::GetApiUtils().GetSteamIdFromController(sender);

	if (!parsed.IsValidIndex(1)) {
		// /LootBox --> List all available lootboxes

		auto remaining = LootDatabase::getRemainingLootBoxesList(steamId);
		if (LootDatabase::getRemainingLootBoxesList(steamId).size() == 0) {
			ArkApi::GetApiUtils().SendChatMessage(sender, *getConfigMessage("prefix"), *FString("You don't have any Boxes lefts"));
			return;
		}
		ArkApi::GetApiUtils().SendChatMessage(sender, *getConfigMessage("prefix"), *FString("Available Boxes:"));

		for (auto const& current : remaining) {
			ArkApi::GetApiUtils().SendChatMessage(sender, *FString(current.first), *FString(std::to_string(current.second) + " Boxes left!"));
		}
		return;
	}

	if (parsed.IsValidIndex(1)) {

		FString lootboxname = parsed[1];

		nlohmann::basic_json lootbox = LootBoxes::config["LootBoxes"][lootboxname.ToString()];
		if (lootbox == 0 || lootbox.empty()) {

			std::string message = getConfigMessage("NotExisting").ToString();
			replace_string_in_place(message, "%lootbox%", lootboxname.ToString());
			ArkApi::GetApiUtils().SendChatMessage(sender, *getConfigMessage("prefix"), *FString(message));
			return;
		}

		if (LootDatabase::hasLootBox(steamId, lootboxname)) {
			UShooterCheatManager* cheatManager = static_cast<UShooterCheatManager*>(sender->CheatManagerField());

			uint64 playerId = sender->LinkedPlayerIDField();

			RandomRewards::generateAndGiveRewards(sender, &lootboxname);
			LootDatabase::decreaseLootBox(steamId, lootboxname);

			std::string message = getConfigMessage("BoxReceived").ToString();
			replace_string_in_place(message, "%lootbox%", lootboxname.ToString());

			ArkApi::GetApiUtils().SendChatMessage(sender, *getConfigMessage("prefix"), *FString(message));
			return;
		}
		std::string nobox = getConfigMessage("NoBox").ToString();
		replace_string_in_place(nobox, "%lootbox%", lootboxname.ToString());
		ArkApi::GetApiUtils().SendChatMessage(sender, *getConfigMessage("prefix"), *FString(nobox));
		return;
	}
	ArkApi::GetApiUtils().SendChatMessage(sender, *getConfigMessage("prefix"), *getConfigMessage("Usage"));
}


void GiveLootBoxRcon(RCONClientConnection* rcon_connection, RCONPacket* rcon_packet, UWorld*)
{
	FString reply;
	TArray<FString> parsed;
	rcon_packet->Body.ParseIntoArray(parsed, L" ", true);

	// Console / RCON --> GiveLootbox <steamId> <lootbox> <amount>
	if (parsed.IsValidIndex(3)) {
		if (parsed[1].ToString().find_first_not_of("0123456789") != std::string::npos || parsed[3].ToString().find_first_not_of("0123456789") != std::string::npos) {
			reply = "Please use GiveLootbox <steamId> <lootbox> <amount>";
			return;
		}

		uint64 steamId = std::stoull(parsed[1].ToString(), 0, 10);
		FString lootboxname = parsed[2];
		int amount = std::stoull(parsed[3].ToString(), 0, 10);

		LootDatabase::addLootBox(steamId, lootboxname, amount);
		reply = "Success!";
		return;
	}
	reply = "Please use GiveLootbox <steamId> <lootbox> <amount>";
	rcon_connection->SendMessageW(rcon_packet->Id, 0, &reply);
}


void load_config()
{
	const std::string config_path = ArkApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/LootBoxes/config.json";
	std::ifstream file(config_path);
	if (!file.is_open())
		throw std::runtime_error("Can't open config.json");

	file >> LootBoxes::config;
	file.close();
}


void Load() {
	Log::Get().Init("LootBoxes");

	load_config();
	LootDatabase::InitDatabase();

	ArkApi::GetCommands().AddChatCommand("/box", &use_lootbox);
	ArkApi::GetCommands().AddConsoleCommand("GiveLootbox", &GiveLootBox);
	ArkApi::GetCommands().AddRconCommand("GiveLootbox", &GiveLootBoxRcon);
}

void Unload() {
	ArkApi::GetCommands().RemoveChatCommand("/box");
	ArkApi::GetCommands().RemoveConsoleCommand("GiveLootbox");
	ArkApi::GetCommands().RemoveRconCommand("GiveLootbox");
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		Load();
		break;
	case DLL_PROCESS_DETACH:
		Unload();
		break;
	}
	return TRUE;
}